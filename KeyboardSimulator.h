#pragma once

/**
 * @file KeyboardSimulator.h
 * @brief Platform-independent interface for sending fake keystrokes to the focused window.
 *
 * TypingEngine uses this class to do the actual typing. Each operating system needs a different
 * mechanism, so the class declares only *what* it can do, and one of these files provides *how*:
 * - `platform/windows/KeyboardSimulator_win.cpp`: Win32 `SendInput` with Unicode keyboard events.
 * - `platform/linux/KeyboardSimulator_x11.cpp`: the X11 XTest extension.
 *
 * CMake compiles exactly one of them, depending on the target platform.
 *
 * Platform state (an X11 connection, for example) is kept in a private `Impl` struct that each
 * implementation file defines for itself. This is the "pimpl" (pointer-to-implementation) idiom: this
 * header never needs to include `<Windows.h>` or `<X11/Xlib.h>`, so the rest of the code stays free of
 * those large, macro-heavy headers.
 */

#include <QString>
#include <memory>

/**
 * @brief Sends simulated keystrokes to whichever window currently has keyboard focus.
 *
 * Lifetime: create one instance per typing run, on the thread that will do the typing, and destroy it when
 * the run ends. Construction acquires platform resources (an X server connection on Linux, a 1 ms timer
 * resolution on Windows) and destruction releases them. This is RAII (Resource Acquisition Is
 * Initialization): the resource lives exactly as long as the object.
 *
 * @warning Not thread-safe: an instance must only be used by the thread that created it.
 */
class KeyboardSimulator {
public:
    /** @brief Acquires the platform resources needed to simulate input. Check IsReady() afterwards. */
    KeyboardSimulator();

    /** @brief Releases the platform resources and undoes any temporary keyboard changes. */
    ~KeyboardSimulator();

    // A simulator owns OS resources that must be released exactly once, so copying is disabled.
    KeyboardSimulator(const KeyboardSimulator&) = delete;
    KeyboardSimulator& operator=(const KeyboardSimulator&) = delete;

    /**
     * @return true if the simulator initialized successfully and can send input.
     * @note If false, ErrorMessage() explains why (for example, no X server is reachable).
     */
    [[nodiscard]] bool IsReady() const;

    /**
     * @brief Checks whether the user is physically holding down any keyboard key.
     * @return true if at least one key is down. Mouse buttons are ignored.
     * @details Used to wait until the keys of the triggering hotkey (e.g. Ctrl+Alt+V) are released before
     *          typing starts. Otherwise the held modifiers would combine with the typed characters.
     */
    [[nodiscard]] bool IsAnyKeyHeld() const;

    /**
     * @brief Types a single character by sending a key press followed by a key release.
     * @param character A Unicode code point. `\n` is sent as ENTER and `\t` as TAB.
     * @return false if the operating system rejected the input. ErrorMessage() then describes the problem.
     */
    bool TypeCharacter(char32_t character);

    /** @return A user-facing description of the most recent failure, or an empty string if nothing failed. */
    [[nodiscard]] QString ErrorMessage() const;

private:
    struct Impl;                 ///< Platform-specific state, defined in the platform's .cpp file.
    std::unique_ptr<Impl> impl;  ///< Always non-null after construction.
    QString errorMessage;        ///< Set by the constructor or TypeCharacter() when something goes wrong.
};
