/**
 * @file GlobalHotkeyManager_x11.cpp
 * @brief Linux/X11 implementation of GlobalHotkeyManager, built on passive key grabs.
 *
 * How it works:
 * 1. Every X11 screen has a **root window**, the desktop background, and every other window is a descendant
 *    of it. A **passive grab** (`xcb_grab_key`) on the root window tells the X server: "whenever this keycode
 *    is pressed with exactly these modifiers, in any window, send the event to me instead of the focused app."
 * 2. The server then delivers a KeyPress event on our X connection. That's the same connection Qt reads events
 *    from, so it shows up in Qt's event loop as a native event of type "xcb_generic_event_t".
 * 3. nativeEventFilter() spots the KeyPress on the root window, matches its keycode and modifiers against the
 *    registered hotkeys, and emits Activated(id).
 *
 * Gotcha, lock modifiers: X11 grabs match the modifier state *exactly*. If Num Lock or Caps Lock is on, the
 * state has extra bits set (Mod2 or Lock), and a grab for plain Ctrl+Alt+V no longer matches. The usual fix,
 * used here, is to grab every combination of the hotkey's modifiers with those lock bits.
 *
 * Why two X connections:
 * - Grabs go through **Qt's own xcb connection**, because the grabbed events are delivered to the connection
 *   that made the grab, and that's the connection whose events Qt hands to the native event filter.
 * - Turning keysyms into keycodes (XKeysymToKeycode) needs Xlib, and Qt may have been built without an Xlib
 *   Display. So a separate, private Xlib connection is opened just for keymap lookups.
 *
 * Only one client can hold a given grab. If another app already grabbed the combination, the server answers
 * with a BadAccess error, which Register() detects with a checked request.
 */

#include "GlobalHotkeyManager.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <cstdlib>

// X11/xcb headers come after all Qt headers because of their conflicting macros (None, Bool, KeyPress, ...).
#include <xcb/xcb.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>

namespace {

/// The modifiers a hotkey can be defined with. Anything else in an event's state (lock keys, mouse buttons)
/// is masked off before comparing.
constexpr quint32 RelevantModifiers = XCB_MOD_MASK_SHIFT | XCB_MOD_MASK_CONTROL | XCB_MOD_MASK_1 | XCB_MOD_MASK_4;

/// Lock-key states to grab alongside each hotkey, so it still fires with Caps Lock (Lock) or Num Lock (Mod2) on.
constexpr quint32 LockModifierCombinations[] = {
    0,
    XCB_MOD_MASK_LOCK,
    XCB_MOD_MASK_2,
    XCB_MOD_MASK_LOCK | XCB_MOD_MASK_2,
};

/**
 * @brief Maps a Qt key code onto the equivalent X11 keysym.
 * @param key The Qt key from the key sequence.
 * @param keypad Whether the key was pressed on the numeric keypad.
 * @return The keysym, or NoSymbol if there is no equivalent.
 */
KeySym QtKeyToKeysym(const Qt::Key key, const bool keypad) {
    if (keypad && key >= Qt::Key_0 && key <= Qt::Key_9) {
        return XK_KP_0 + (key - Qt::Key_0);
    }
    // Qt reports letters in uppercase. The lowercase keysym is the key's level-0 symbol, but XKeysymToKeycode
    // finds the same physical key either way.
    if (key >= Qt::Key_A && key <= Qt::Key_Z) {
        return XK_a + (key - Qt::Key_A);
    }
    if (key >= Qt::Key_0 && key <= Qt::Key_9) {
        return XK_0 + (key - Qt::Key_0);
    }
    if (key >= Qt::Key_F1 && key <= Qt::Key_F35) {
        return XK_F1 + (key - Qt::Key_F1);
    }

    switch (key) {
        case Qt::Key_Escape:     return XK_Escape;
        case Qt::Key_Tab:        return XK_Tab;
        case Qt::Key_Backspace:  return XK_BackSpace;
        case Qt::Key_Return:     return XK_Return;
        case Qt::Key_Enter:      return XK_KP_Enter;
        case Qt::Key_Insert:     return XK_Insert;
        case Qt::Key_Delete:     return XK_Delete;
        case Qt::Key_Pause:      return XK_Pause;
        case Qt::Key_Print:      return XK_Print;
        case Qt::Key_Home:       return XK_Home;
        case Qt::Key_End:        return XK_End;
        case Qt::Key_Left:       return XK_Left;
        case Qt::Key_Up:         return XK_Up;
        case Qt::Key_Right:      return XK_Right;
        case Qt::Key_Down:       return XK_Down;
        case Qt::Key_PageUp:     return XK_Prior;
        case Qt::Key_PageDown:   return XK_Next;
        case Qt::Key_Space:      return XK_space;
        case Qt::Key_ScrollLock: return XK_Scroll_Lock;
        default: break;
    }

    // Remaining Latin-1 punctuation: Qt key codes and X11 keysyms both equal the character's code point here.
    // Shifted symbols resolve to their physical key: Ctrl+Shift+! -> XK_exclam -> the "1" key.
    if (key >= Qt::Key_Space && key <= Qt::Key_ydiaeresis) {
        return static_cast<KeySym>(key);
    }
    return NoSymbol;
}

} // namespace

/** @brief X11 connection state shared by all hotkeys. */
struct GlobalHotkeyManager::Impl {
    xcb_connection_t* connection = nullptr; ///< Qt's own connection. Not owned, never closed here.
    Display* keymapDisplay = nullptr;       ///< Private Xlib connection for keysym -> keycode lookups. Owned.
    xcb_window_t rootWindow = 0;            ///< Root window of the default screen, where the grabs are placed.

    /** @brief Releases every lock-key variant of a hotkey's grab. Grabs this client never got are ignored by the server. */
    void Ungrab(const NativeHotkey& hotkey) const {
        for (const quint32 locks : LockModifierCombinations) {
            xcb_ungrab_key(connection, static_cast<xcb_keycode_t>(hotkey.key), rootWindow,
                           static_cast<uint16_t>(hotkey.modifiers | locks));
        }
        xcb_flush(connection);  // xcb requests are buffered too. Send them now.
    }
};

GlobalHotkeyManager::GlobalHotkeyManager(QObject* parent) : QObject(parent), impl(std::make_unique<Impl>()) {
    // QNativeInterface is Qt 6's way to reach platform handles. It only exists when Qt runs on X11 (xcb).
    // Under a native Wayland backend it returns null, and the manager stays unavailable.
#if QT_CONFIG(xcb)
    if (const auto* x11 = qGuiApp->nativeInterface<QNativeInterface::QX11Application>()) {
        impl->connection = x11->connection();
        impl->keymapDisplay = XOpenDisplay(nullptr);
        if (impl->keymapDisplay != nullptr) {
            // Window ids are global to the X server, so the root window id from our Xlib connection is valid
            // on Qt's xcb connection too.
            impl->rootWindow = static_cast<xcb_window_t>(DefaultRootWindow(impl->keymapDisplay));
        }
    }
#endif
    QCoreApplication::instance()->installNativeEventFilter(this);
}

GlobalHotkeyManager::~GlobalHotkeyManager() {
    if (IsAvailable()) {
        for (const NativeHotkey& hotkey : std::as_const(registered)) {
            impl->Ungrab(hotkey);
        }
    }
    if (impl->keymapDisplay != nullptr) {
        XCloseDisplay(impl->keymapDisplay);
    }
    QCoreApplication::instance()->removeNativeEventFilter(this);
}

bool GlobalHotkeyManager::IsAvailable() const {
    return impl->connection != nullptr && impl->keymapDisplay != nullptr;
}

bool GlobalHotkeyManager::CanRegister(const QKeySequence& sequence) const {
    NativeHotkey hotkey;
    return ToNative(sequence, hotkey);
}

bool GlobalHotkeyManager::Register(const int id, const QKeySequence& sequence) {
    Unregister(id);

    NativeHotkey hotkey;
    if (!IsAvailable() || !ToNative(sequence, hotkey)) {
        return false;
    }

    bool grabbedAll = true;
    for (const quint32 locks : LockModifierCombinations) {
        // owner_events = 0: always report the key event to the grab window (root), even when one of our own
        // windows has focus. That way nativeEventFilter only needs to look at root-window events.
        // The two GRAB_MODE_ASYNC arguments let keyboard and pointer processing carry on normally during the
        // grab, instead of freezing until we acknowledge each event.
        const xcb_void_cookie_t cookie = xcb_grab_key_checked(
            impl->connection, 0, impl->rootWindow, static_cast<uint16_t>(hotkey.modifiers | locks),
            static_cast<xcb_keycode_t>(hotkey.key), XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC);

        // "_checked" requests can be waited on: xcb_request_check blocks until the server has processed the
        // request, then returns its error (BadAccess if another client owns the grab), or null on success.
        if (xcb_generic_error_t* error = xcb_request_check(impl->connection, cookie)) {
            grabbedAll = false;
            std::free(error);  // xcb allocates errors with malloc.
        }
    }

    if (!grabbedAll) {
        // Don't keep a half-working hotkey (e.g. one that only fires with Num Lock off).
        impl->Ungrab(hotkey);
        return false;
    }
    registered.insert(id, hotkey);
    return true;
}

void GlobalHotkeyManager::Unregister(const int id) {
    const auto it = registered.constFind(id);
    if (it == registered.cend()) {
        return;
    }
    if (IsAvailable()) {
        impl->Ungrab(*it);
    }
    registered.erase(it);
}

bool GlobalHotkeyManager::ToNative(const QKeySequence& sequence, NativeHotkey& hotkey) const {
    if (sequence.isEmpty() || !IsAvailable()) {
        return false;
    }

    // Only the first chord of a multi-chord sequence is used. See the Windows implementation for details.
    const QKeyCombination combination = sequence[0];
    const Qt::KeyboardModifiers qtModifiers = combination.keyboardModifiers();

    const KeySym keysym = QtKeyToKeysym(combination.key(), qtModifiers.testFlag(Qt::KeypadModifier));
    if (keysym == NoSymbol) {
        return false;
    }
    // A keysym may be unreachable on the current layout. Keycode 0 means "no key produces this".
    hotkey.key = XKeysymToKeycode(impl->keymapDisplay, keysym);
    if (hotkey.key == 0) {
        return false;
    }

    // X11 modifier conventions: Mod1 = Alt, Mod4 = Super (the Windows/logo key), which Qt calls Meta.
    hotkey.modifiers = 0;
    if (qtModifiers.testFlag(Qt::ShiftModifier))   hotkey.modifiers |= XCB_MOD_MASK_SHIFT;
    if (qtModifiers.testFlag(Qt::ControlModifier)) hotkey.modifiers |= XCB_MOD_MASK_CONTROL;
    if (qtModifiers.testFlag(Qt::AltModifier))     hotkey.modifiers |= XCB_MOD_MASK_1;
    if (qtModifiers.testFlag(Qt::MetaModifier))    hotkey.modifiers |= XCB_MOD_MASK_4;
    return true;
}

bool GlobalHotkeyManager::nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) {
    Q_UNUSED(result)
    if (eventType != "xcb_generic_event_t" || !IsAvailable()) {
        return false;
    }

    const auto* event = static_cast<const xcb_generic_event_t*>(message);
    // The top bit of response_type marks events that another client sent with SendEvent. Mask it off to get
    // the real event type.
    if ((event->response_type & 0x7F) != XCB_KEY_PRESS) {
        return false;
    }

    // Every xcb event starts with the same header, so the pointer can be read as the specific event type.
    const auto* keyEvent = static_cast<const xcb_key_press_event_t*>(message);
    if (keyEvent->event != impl->rootWindow) {
        return false;  // An ordinary key press in one of Qt's windows: not ours.
    }

    const quint32 modifiers = keyEvent->state & RelevantModifiers;
    for (auto it = registered.cbegin(); it != registered.cend(); ++it) {
        if (it->key == keyEvent->detail && it->modifiers == modifiers) {  // detail = the keycode.
            emit Activated(it.key());
            return true;
        }
    }
    return false;
}
