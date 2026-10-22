/**
 * @file TypingEngine.cpp
 * @brief Implementation of TypingEngine: the worker-thread loop, timing, and cancellation.
 */

#include "TypingEngine.h"

#include "KeyboardSimulator.h"

#include <chrono>
#include <cmath>
#include <condition_variable>
#include <mutex>
#include <random>
#include <stop_token>
#include <string>

namespace {

/// The longest the run waits for the user to release the hotkey. It's a cap rather than an indefinite wait
/// because a key can be reported as stuck (e.g. after switching windows while holding it).
constexpr auto KeyReleaseTimeout = std::chrono::seconds(3);

/// How often the keyboard state is polled while waiting for keys to be released.
constexpr auto KeyReleasePollInterval = std::chrono::milliseconds(15);

/**
 * @brief Sleeps for @p duration, but wakes immediately if a stop is requested.
 * @return true if the full duration elapsed, false if the sleep was cut short by a stop request.
 *
 * @details A plain std::this_thread::sleep_for can't be interrupted: at very slow speeds, or during a long
 *          start delay, Stop() would appear to do nothing for seconds. C++20 gave
 *          std::condition_variable_any an overload of wait_for that takes a std::stop_token. It registers a
 *          callback that wakes the wait as soon as stop is requested. The predicate always returns false, so
 *          the wait only ends on timeout or stop. The condition variable is never notified any other way.
 */
bool SleepUnlessStopped(const std::stop_token& stopToken, const std::chrono::milliseconds duration) {
    std::mutex mutex;
    std::condition_variable_any wakeUp;
    std::unique_lock lock(mutex);
    wakeUp.wait_for(lock, stopToken, duration, [] { return false; });
    return !stopToken.stop_requested();
}

/**
 * @brief Waits until the user lets go of every key, or until KeyReleaseTimeout elapses.
 * @details When typing is triggered by a hotkey such as Ctrl+Alt+V, those keys are usually still held down
 *          when the run starts. Typing right away would send Ctrl+Alt+<character> combinations to the target
 *          window instead of plain text.
 * @return false if a stop was requested while waiting.
 */
bool WaitForKeysReleased(const std::stop_token& stopToken, const KeyboardSimulator& keyboard) {
    const auto deadline = std::chrono::steady_clock::now() + KeyReleaseTimeout;
    while (keyboard.IsAnyKeyHeld() && std::chrono::steady_clock::now() < deadline) {
        if (!SleepUnlessStopped(stopToken, KeyReleasePollInterval)) {
            return false;
        }
    }
    return !stopToken.stop_requested();
}

/**
 * @brief Everything that happens before the first keystroke: the start delay, then waiting for keys to be released.
 * @return false if a stop was requested before typing could begin.
 */
bool WaitBeforeTyping(const std::stop_token& stopToken, const KeyboardSimulator& keyboard, const TypingOptions& options) {
    return SleepUnlessStopped(stopToken, std::chrono::milliseconds(options.startDelayMs))
        && WaitForKeysReleased(stopToken, keyboard);
}

/**
 * @brief Types every character of @p text with the requested timing.
 * @param stopToken Checked before every character, and interrupts the delay after it.
 * @param keyboard The platform simulator that sends each keystroke.
 * @param text The text, as Unicode code points (UTF-32), so each element is one whole character.
 * @param options Speed and timing variation.
 */
TypingEngine::Result TypeText(const std::stop_token& stopToken, KeyboardSimulator& keyboard,
                              const std::u32string& text, const TypingOptions& options) {
    // Words per minute -> milliseconds per character, using the convention of 5 characters per word:
    // 60,000 ms per minute / (WPM * 5 characters per word). At 250 WPM that's 48 ms per character.
    const double baseDelayMs = 60'000.0 / (options.wordsPerMinute * 5.0);

    // Mersenne Twister seeded from the OS entropy source. Quality doesn't matter much here, it just needs
    // delays that don't repeat in an obvious pattern.
    std::mt19937 generator {std::random_device {}()};
    std::uniform_real_distribution jitter(0.5, 1.5);

    for (const char32_t character : text) {
        if (stopToken.stop_requested()) {
            return TypingEngine::Result::Cancelled;
        }
        // Windows clipboard text uses CRLF ("\r\n") line endings. The '\n' alone already presses ENTER.
        if (character == U'\r') {
            continue;
        }
        if (!keyboard.TypeCharacter(character)) {
            return TypingEngine::Result::Failed;
        }

        const double delayMs = options.varyTiming ? baseDelayMs * jitter(generator) : baseDelayMs;
        if (!SleepUnlessStopped(stopToken, std::chrono::milliseconds(std::llround(delayMs)))) {
            return TypingEngine::Result::Cancelled;
        }
    }
    return TypingEngine::Result::Completed;
}

} // namespace

TypingEngine::TypingEngine(QObject* parent) : QObject(parent) {}

TypingEngine::~TypingEngine() {
    // std::jthread would do both of these in its own destructor, but doing them explicitly here guarantees the
    // thread has finished *before* the QObject part of this object is torn down. The worker emits signals on
    // `this`, so it must never outlive it.
    worker.request_stop();
    if (worker.joinable()) {
        worker.join();
    }
}

bool TypingEngine::Start(const QString& text, const TypingOptions& options) {
    // exchange() sets `typing` to true and returns the old value in a single atomic step. If it was already
    // true, another run is active, so refuse to start a second one.
    if (text.isEmpty() || typing.exchange(true)) {
        return false;
    }

    // Reap the previous worker. It has already finished (typing was false), so this returns at once. A
    // std::jthread must be joined, or moved from, before a new thread is assigned to it.
    if (worker.joinable()) {
        worker.join();
    }

    TypingOptions safeOptions = options;
    if (safeOptions.wordsPerMinute == 0) {
        safeOptions.wordsPerMinute = 1;  // TypeText divides by this.
    }

    // QString stores UTF-16, where characters above U+FFFF (most emoji) take two code units. Converting to
    // UTF-32 gives exactly one element per character, which is what KeyboardSimulator expects.
    const QList<uint> ucs4 = text.toUcs4();
    std::u32string codePoints(ucs4.cbegin(), ucs4.cend());

    // The lambda captures copies (text and options by value) so the worker never touches data owned by the
    // GUI thread. When a std::jthread's callable takes a std::stop_token as its first parameter, jthread
    // passes its own token in automatically.
    worker = std::jthread([this, codePoints = std::move(codePoints), safeOptions](const std::stop_token& stopToken) {
        // The simulator lives entirely on this thread, for exactly one run (see KeyboardSimulator.h for why).
        KeyboardSimulator keyboard;

        Result result = Result::Cancelled;
        if (!keyboard.IsReady()) {
            result = Result::Failed;
        } else if (WaitBeforeTyping(stopToken, keyboard, safeOptions)) {
            emit TypingStarted();
            result = TypeText(stopToken, keyboard, codePoints, safeOptions);
        }

        const QString errorMessage = result == Result::Failed ? keyboard.ErrorMessage() : QString();
        // Clear the flag *before* emitting, so a receiver that checks IsTyping() sees the run as over.
        typing = false;
        emit TypingFinished(result, errorMessage);
    });
    return true;
}

void TypingEngine::Stop() {
    // Only sets a flag (and wakes any interruptible sleep). The worker notices and exits on its own.
    worker.request_stop();
}

bool TypingEngine::IsTyping() const {
    return typing;
}
