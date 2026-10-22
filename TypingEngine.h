#pragma once

/**
 * @file TypingEngine.h
 * @brief Runs typing jobs on a background thread, with timing, start delays and cancellation.
 *
 * TypingEngine decides *when* each character is typed (speed, random variation, waiting for the hotkey to be
 * released). KeyboardSimulator decides *how* a character is turned into OS keystrokes. Keeping the two apart
 * means the timing logic is written once and shared by every platform.
 */

#include <QObject>
#include <QString>
#include <atomic>
#include <thread>

/**
 * @brief Parameters for a single typing run. Copied into the worker thread when the run starts.
 */
struct TypingOptions {
    unsigned int wordsPerMinute = 250; ///< Target speed. A "word" is 5 characters by convention. Must be > 0.
    unsigned int startDelayMs = 0;     ///< Pause before the first keystroke, to give the user time to focus the target.
    bool varyTiming = true;            ///< Randomize each delay between 50% and 150% of the average, so typing looks less mechanical.
};

/**
 * @brief Types text into whichever window has keyboard focus, on a dedicated background thread.
 *
 * Why a separate thread: typing 1,000 characters at 250 WPM takes about 48 seconds. Done on the GUI thread,
 * the event loop would be blocked the whole time: no repaints, no tray menu, and no way to receive the "stop"
 * hotkey. On a worker thread, the GUI thread stays free and can cancel the run at any moment.
 *
 * Cancellation uses C++20's cooperative model. `std::jthread` owns a `std::stop_source`, and Stop() calls
 * `request_stop()`. The worker gets the matching `std::stop_token`, checks it between keystrokes, and wakes
 * from its sleeps the moment a stop is requested (see SleepUnlessStopped in the .cpp).
 *
 * Cross-thread signals: TypingStarted and TypingFinished are emitted *from the worker thread*. Qt sees that
 * the receivers (TrayController) live in a different thread and automatically uses a queued connection. The
 * signal is packaged as an event and the slot runs later, on the GUI thread, so receivers can safely touch
 * widgets.
 *
 * Only one run can be active at a time.
 */
class TypingEngine : public QObject {
    Q_OBJECT

public:
    /** @brief How a typing run ended. */
    enum class Result {
        Completed, ///< Every character was sent.
        Cancelled, ///< Stop() was called before the text was finished (or before it started).
        Failed     ///< The platform rejected the simulated input. The accompanying message explains why.
    };
    // Q_ENUM registers the enum with Qt's meta-object system. Queued connections need that to copy a value of
    // this type into an event, and it also makes qDebug() print the names instead of numbers.
    Q_ENUM(Result)

    /** @param parent Standard QObject parent, for ownership. May be null. */
    explicit TypingEngine(QObject* parent = nullptr);

    /**
     * @brief Cancels any active run and blocks until the typing thread has exited.
     * @details Every wait in the worker is interruptible, so this returns within a few milliseconds.
     */
    ~TypingEngine() override;

    /**
     * @brief Starts typing @p text on a background thread and returns immediately.
     * @param text The text to type. Carriage returns are skipped (Windows CRLF line endings would otherwise
     *             press ENTER twice), newlines become ENTER, and tabs become TAB.
     * @param options Speed and timing for this run.
     * @return false if a run is already in progress or @p text is empty, true if typing started.
     * @note Before the first keystroke, the run waits out the start delay and then (for up to 3 seconds) for
     *       every key to be released. That stops a hotkey's still-held modifiers from combining with the typed
     *       characters.
     */
    bool Start(const QString& text, const TypingOptions& options);

    /** @brief Asks the active run to stop as soon as possible. Returns immediately. Safe to call when idle. */
    void Stop();

    /** @return true from a successful Start() until the worker finishes. Safe to call from any thread. */
    [[nodiscard]] bool IsTyping() const;

signals:
    /**
     * @brief Emitted from the typing thread right before the first keystroke, once the start delay has elapsed
     *        and all keys are released.
     * @note Not emitted if the run is cancelled or fails before typing begins.
     */
    void TypingStarted();

    /**
     * @brief Emitted from the typing thread when a run ends, for whatever reason.
     * @param result How the run ended.
     * @param errorMessage For Result::Failed, a user-facing explanation from the platform layer. Empty otherwise.
     */
    void TypingFinished(TypingEngine::Result result, const QString& errorMessage);

private:
    /**
     * @brief The background thread for the current or most recent run.
     * @details Unlike std::thread, std::jthread ("joining thread") joins automatically when destroyed and
     *          carries a built-in stop_source for cooperative cancellation.
     */
    std::jthread worker;

    /// Written by the worker when it finishes, read by the GUI thread. std::atomic makes that cross-thread
    /// access well-defined without a mutex.
    std::atomic<bool> typing {false};
};
