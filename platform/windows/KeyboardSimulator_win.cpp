/**
 * @file KeyboardSimulator_win.cpp
 * @brief Windows implementation of KeyboardSimulator, built on the Win32 `SendInput` API.
 *
 * How it works:
 * `SendInput` injects input events into the same stream real hardware uses, so the focused window can't tell
 * the difference. With the `KEYEVENTF_UNICODE` flag, an event carries a UTF-16 code unit instead of a physical
 * key code. Windows delivers it to the target window as a `WM_CHAR` message for that exact character, whatever
 * the keyboard layout is. That's why any character, accents and emoji included, can be typed without working
 * out which keys would produce it.
 *
 * Limitation (UIPI): User Interface Privilege Isolation stops a normal process from sending input to a process
 * with a higher integrity level, such as an app "Run as administrator". Windows drops that input silently, and
 * may not even report a failure.
 */

#include "KeyboardSimulator.h"

#include <QDebug>
#include <iterator>
#include <Windows.h>
#include <timeapi.h>

/** @brief The Windows backend needs no per-run state: SendInput and GetAsyncKeyState are stateless calls. */
struct KeyboardSimulator::Impl {};

KeyboardSimulator::KeyboardSimulator() : impl(std::make_unique<Impl>()) {
    // Windows' default timer resolution is ~15.6 ms, so std::this_thread::sleep_for(5ms) actually sleeps ~15 ms.
    // That would cap the typing speed far below high WPM settings. timeBeginPeriod(1) asks for 1 ms resolution
    // for as long as this simulator (one typing run) exists. The destructor undoes it, which matters because a
    // high timer resolution costs battery.
    timeBeginPeriod(1);
}

KeyboardSimulator::~KeyboardSimulator() {
    timeEndPeriod(1);
}

bool KeyboardSimulator::IsReady() const {
    // SendInput is always available on desktop Windows.
    return true;
}

bool KeyboardSimulator::IsAnyKeyHeld() const {
    // Virtual-key codes 0x01-0x06 are mouse buttons, so start at VK_BACK (0x08). Ignoring the mouse means
    // clicking into the target window during the start delay doesn't hold up typing.
    for (int virtualKey = VK_BACK; virtualKey <= 0xFE; ++virtualKey) {
        // GetAsyncKeyState reads the physical key state right now. The high bit (0x8000) means "currently down".
        if (GetAsyncKeyState(virtualKey) & 0x8000) {
            return true;
        }
    }
    return false;
}

bool KeyboardSimulator::TypeCharacter(const char32_t character) {
    // Room for the worst case: a surrogate pair (2 code units) x (down + up) = 4 events.
    INPUT inputs[4] = {};
    UINT count = 0;

    // Appends a key-down/key-up pair for one event description.
    const auto addKeyPress = [&](const WORD virtualKey, const WORD unicodeUnit) {
        for (const DWORD upFlag : {DWORD {0}, DWORD {KEYEVENTF_KEYUP}}) {
            INPUT& input = inputs[count++];
            input.type = INPUT_KEYBOARD;
            input.ki.wVk = virtualKey;
            input.ki.wScan = unicodeUnit;
            input.ki.dwFlags = upFlag | (unicodeUnit != 0 ? KEYEVENTF_UNICODE : 0);
        }
    };

    if (character == U'\n') {
        // ENTER and TAB are sent as real virtual keys: many applications only react to the actual key,
        // not to a Unicode newline/tab character.
        addKeyPress(VK_RETURN, 0);
    } else if (character == U'\t') {
        addKeyPress(VK_TAB, 0);
    } else if (character <= 0xFFFF) {
        // Basic Multilingual Plane: the character fits in a single UTF-16 code unit.
        addKeyPress(0, static_cast<WORD>(character));
    } else {
        // Characters above U+FFFF (most emoji, for example) need two UTF-16 code units, a "surrogate pair".
        // Windows reassembles them on the receiving side from two consecutive WM_CHAR messages.
        const char32_t offset = character - 0x10000;
        addKeyPress(0, static_cast<WORD>(0xD800 + (offset >> 10)));   // High surrogate: top 10 bits.
        addKeyPress(0, static_cast<WORD>(0xDC00 + (offset & 0x3FF))); // Low surrogate: bottom 10 bits.
    }

    // SendInput returns how many events it inserted. Anything less than `count` means the input was blocked.
    // cbSize (last argument) is the size of a single INPUT struct, not of the whole array, as the API requires.
    const UINT sent = SendInput(count, inputs, sizeof(INPUT));
    if (sent != count) {
        qWarning("SendInput failed: 0x%lx", static_cast<unsigned long>(HRESULT_FROM_WIN32(GetLastError())));
        errorMessage = "Windows blocked the simulated keystrokes. If the target application runs as administrator, "
                       "AutoTextInput must run as administrator too.";
        return false;
    }
    return true;
}

QString KeyboardSimulator::ErrorMessage() const {
    return errorMessage;
}
