/**
 * @file KeyboardSimulator_x11.cpp
 * @brief Linux/X11 implementation of KeyboardSimulator, built on the XTest extension.
 *
 * How X11 keyboard input works (and why this file is longer than the Windows one):
 * X11 has no "type this Unicode character" event like Windows' KEYEVENTF_UNICODE. Every key event carries a
 * **keycode**, a number for a physical key (e.g. 38 is the "A" key on most PC keyboards). Each client turns
 * keycodes into characters using the server's **keyboard mapping**. That table lists, for each keycode, the
 * **keysyms** (symbolic character names such as XK_a, XK_A, XK_eacute) it produces at each shift **level**:
 * level 0 is unshifted, level 1 is with Shift, and higher levels come from AltGr and similar.
 *
 * So to type a character, this file:
 * 1. Converts the character into a keysym (KeysymForCharacter).
 * 2. Looks for a keycode that produces that keysym at level 0 or 1 in the active layout group, and presses
 *    it, holding Shift as well for level 1.
 * 3. If no key produces it (e.g. "é" on a US layout, or an emoji), temporarily rebinds a spare, unused keycode
 *    (the "scratch" keycode) to that keysym and presses that instead. xdotool uses the same trick. The binding
 *    is undone when the simulator is destroyed.
 *
 * Keystrokes are injected with XTestFakeKeyEvent. To the X server and to the receiving app they look exactly
 * like real hardware key presses.
 *
 * Threading: Xlib connections must not be shared between threads without extra locking, and TypingEngine runs
 * on its own thread. So this simulator opens a private X connection (XOpenDisplay) instead of borrowing Qt's.
 */

#include "KeyboardSimulator.h"

#include <QDebug>
#include <chrono>
#include <thread>

// X11 headers come last: they define macros such as None, Bool, Status, KeyPress and Always, which clash with
// identifiers used in Qt headers.
#include <X11/XKBlib.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/XTest.h>
#include <X11/keysym.h>

namespace {

/// After the scratch keycode is rebound, the receiving application still has to translate the previous
/// scratch keystroke with the *old* mapping. Rebinding again too quickly could make it read the wrong
/// character, so consecutive rebinds are spaced at least this far apart.
constexpr auto ScratchRebindInterval = std::chrono::milliseconds(30);

/**
 * @brief Converts a Unicode code point into the X11 keysym that represents it.
 * @details Keysym numbering rules:
 *          - Latin-1 characters (U+0020-U+007E and U+00A0-U+00FF) use their code point as the keysym, so 'a' is
 *            XK_a = 0x61.
 *          - Any other character can be written as 0x01000000 + code point. This "Unicode keysym" convention
 *            is understood by every modern X toolkit (GTK, Qt, Xlib's XLookupString).
 *          - Control characters map to their function keys.
 */
KeySym KeysymForCharacter(const char32_t character) {
    switch (character) {
        case U'\n': return XK_Return;
        case U'\t': return XK_Tab;
        case U'\b': return XK_BackSpace;
        default: break;
    }
    if ((character >= 0x20 && character <= 0x7E) || (character >= 0xA0 && character <= 0xFF)) {
        return static_cast<KeySym>(character);
    }
    return static_cast<KeySym>(0x01000000 | character);
}

/**
 * @return true if Caps Lock changes this keysym, i.e. it has distinct lowercase and uppercase forms.
 */
bool IsCaseSensitive(const KeySym keysym) {
    KeySym lower = NoSymbol;
    KeySym upper = NoSymbol;
    XConvertCase(keysym, &lower, &upper);
    return lower != upper;
}

} // namespace

/** @brief State for one typing run's X11 connection. */
struct KeyboardSimulator::Impl {
    Display* display = nullptr;          ///< Private connection to the X server. Owned; closed in the destructor.
    KeyCode shiftKeycode = 0;            ///< Keycode of Left Shift, pressed for level-1 (shifted) characters.
    KeyCode scratchKeycode = 0;          ///< A keycode with no keysyms, borrowed for unmapped characters. 0 = not found yet.
    KeySym scratchKeysym = NoSymbol;     ///< What scratchKeycode is currently bound to. NoSymbol = untouched.
    std::chrono::steady_clock::time_point lastScratchUse; ///< When the scratch keycode was last pressed.

    /**
     * @brief Searches the keyboard mapping for a keycode that produces no keysyms at all.
     * @return A free keycode, or 0 if every keycode is in use.
     * @details X servers report keycodes in a range (usually 8-255), and real keyboards leave many of them
     *          empty. The search runs from the top of the range down, because high keycodes are the least
     *          likely to belong to a physical key that the user might press during the run.
     */
    KeyCode FindScratchKeycode() const {
        int minKeycode = 0;
        int maxKeycode = 0;
        XDisplayKeycodes(display, &minKeycode, &maxKeycode);

        // XGetKeyboardMapping returns a flat array: keysymsPerKeycode entries for each keycode in the range.
        int keysymsPerKeycode = 0;
        KeySym* mapping = XGetKeyboardMapping(display, static_cast<KeyCode>(minKeycode),
                                              maxKeycode - minKeycode + 1, &keysymsPerKeycode);
        if (mapping == nullptr) {
            return 0;
        }

        KeyCode found = 0;
        for (int keycode = maxKeycode; keycode >= minKeycode && found == 0; --keycode) {
            const KeySym* entry = mapping + (keycode - minKeycode) * keysymsPerKeycode;
            bool empty = true;
            for (int level = 0; level < keysymsPerKeycode; ++level) {
                if (entry[level] != NoSymbol) {
                    empty = false;
                    break;
                }
            }
            if (empty) {
                found = static_cast<KeyCode>(keycode);
            }
        }
        XFree(mapping);  // Memory returned by Xlib must be released with XFree, not delete/free.
        return found;
    }

    /**
     * @brief Makes the scratch keycode produce @p keysym.
     * @return The scratch keycode, or 0 if no free keycode exists.
     */
    KeyCode BindToScratch(const KeySym keysym) {
        if (scratchKeycode == 0) {
            scratchKeycode = FindScratchKeycode();
            if (scratchKeycode == 0) {
                return 0;
            }
        }
        if (scratchKeysym != keysym) {
            std::this_thread::sleep_until(lastScratchUse + ScratchRebindInterval);

            // Bind the same keysym at level 0 and level 1, so the result is identical whether or not Shift or
            // Caps Lock happens to be active.
            KeySym keysyms[2] = {keysym, keysym};
            XChangeKeyboardMapping(display, scratchKeycode, 2, keysyms, 1);
            // XSync waits until the server has applied the change. The server then sends every client a
            // MappingNotify event *before* the key event that follows, so they refresh their mapping in time.
            XSync(display, False);
            scratchKeysym = keysym;
        }
        return scratchKeycode;
    }

    /** @brief Presses and releases @p keycode, optionally while holding Shift. */
    void Tap(const KeyCode keycode, const bool withShift) const {
        // CurrentTime (0) as the delay means "now". XTest can also schedule events in the future.
        if (withShift && shiftKeycode != 0) {
            XTestFakeKeyEvent(display, shiftKeycode, True, CurrentTime);
        }
        XTestFakeKeyEvent(display, keycode, True, CurrentTime);   // Key press.
        XTestFakeKeyEvent(display, keycode, False, CurrentTime);  // Key release.
        if (withShift && shiftKeycode != 0) {
            XTestFakeKeyEvent(display, shiftKeycode, False, CurrentTime);
        }
        // Xlib buffers requests client-side. XFlush sends them to the server now, rather than whenever the
        // buffer happens to fill up.
        XFlush(display);
    }
};

KeyboardSimulator::KeyboardSimulator() : impl(std::make_unique<Impl>()) {
    // nullptr means "use the DISPLAY environment variable", the same X server Qt connected to.
    impl->display = XOpenDisplay(nullptr);
    if (impl->display == nullptr) {
        errorMessage = "Couldn't connect to the X server, so typing is unavailable. Is the DISPLAY variable set?";
        return;
    }

    int eventBase = 0;
    int errorBase = 0;
    int majorVersion = 0;
    int minorVersion = 0;
    if (!XTestQueryExtension(impl->display, &eventBase, &errorBase, &majorVersion, &minorVersion)) {
        errorMessage = "The X server doesn't support the XTEST extension, which is required to simulate typing.";
        XCloseDisplay(impl->display);
        impl->display = nullptr;
        return;
    }

    impl->shiftKeycode = XKeysymToKeycode(impl->display, XK_Shift_L);
}

KeyboardSimulator::~KeyboardSimulator() {
    if (impl->display == nullptr) {
        return;
    }
    // Leave the keyboard mapping as we found it: return the borrowed scratch keycode to "no keysyms".
    if (impl->scratchKeysym != NoSymbol) {
        std::this_thread::sleep_until(impl->lastScratchUse + ScratchRebindInterval);
        KeySym empty = NoSymbol;
        XChangeKeyboardMapping(impl->display, impl->scratchKeycode, 1, &empty, 1);
        XSync(impl->display, False);
    }
    XCloseDisplay(impl->display);
}

bool KeyboardSimulator::IsReady() const {
    return impl->display != nullptr;
}

bool KeyboardSimulator::IsAnyKeyHeld() const {
    if (impl->display == nullptr) {
        return false;
    }
    // XQueryKeymap fills a 256-bit bitmap (32 bytes) with one bit per keycode: 1 = currently pressed.
    // It covers keyboard keys only, so mouse buttons are naturally ignored.
    char keys[32] = {};
    XQueryKeymap(impl->display, keys);
    for (const char byte : keys) {
        if (byte != 0) {
            return true;
        }
    }
    return false;
}

bool KeyboardSimulator::TypeCharacter(const char32_t character) {
    if (impl->display == nullptr) {
        return false;
    }

    const KeySym keysym = KeysymForCharacter(character);

    // The XKB state gives the active layout group (users with several layouts switch between groups) and
    // the locked modifiers (Caps Lock shows up as LockMask in locked_mods).
    XkbStateRec state {};
    XkbGetState(impl->display, XkbUseCoreKbd, &state);

    // Step 1: find a real key that produces the keysym at level 0 (plain) or level 1 (Shift).
    KeyCode keycode = XKeysymToKeycode(impl->display, keysym);
    bool found = false;
    bool withShift = false;
    if (keycode != 0) {
        if (XkbKeycodeToKeysym(impl->display, keycode, state.group, 0) == keysym) {
            found = true;
        } else if (XkbKeycodeToKeysym(impl->display, keycode, state.group, 1) == keysym) {
            found = true;
            withShift = true;
        }
        // Anything else (e.g. only reachable through AltGr at level 2) falls through to the scratch keycode.
    }

    // Caps Lock inverts Shift for letters: with Caps on, the plain "a" key produces "A". Compensate, so the
    // text comes out as written whatever the lock state.
    if (found && (state.locked_mods & LockMask) && IsCaseSensitive(keysym)) {
        withShift = !withShift;
    }

    // Step 2: no suitable key, so borrow the scratch keycode.
    if (!found) {
        keycode = impl->BindToScratch(keysym);
        withShift = false;
        if (keycode == 0) {
            errorMessage = QString("Couldn't type U+%1: the keyboard mapping has no free keycode to borrow.")
                               .arg(static_cast<uint>(character), 4, 16, QLatin1Char('0'));
            return false;
        }
    }

    impl->Tap(keycode, withShift);
    if (keycode == impl->scratchKeycode) {
        impl->lastScratchUse = std::chrono::steady_clock::now();
    }
    return true;
}

QString KeyboardSimulator::ErrorMessage() const {
    return errorMessage;
}
