/**
 * @file GlobalHotkeyManager_win.cpp
 * @brief Windows implementation of GlobalHotkeyManager, built on the Win32 `RegisterHotKey` API.
 *
 * How it works:
 * 1. `RegisterHotKey(nullptr, id, modifiers, virtualKey)` asks Windows to watch for a key combination
 *    system-wide. Passing a null window handle attaches the hotkey to the *calling thread's message queue*
 *    instead of to a window. That's the GUI thread, because this class must be used there.
 * 2. When the user presses the combination, Windows swallows it (the focused app never sees it) and posts a
 *    `WM_HOTKEY` message, with `wParam` set to the id, to that queue.
 * 3. Qt's event loop pulls every message off the queue and first offers it to the installed native event
 *    filters with the type "windows_generic_MSG". nativeEventFilter() spots `WM_HOTKEY` and emits Activated(id).
 *
 * Only one process can own a given combination. If another app already registered it, RegisterHotKey fails,
 * and the UI reports that to the user.
 */

#include "GlobalHotkeyManager.h"

#include <QCoreApplication>
#include <Windows.h>

namespace {

/**
 * @brief Maps a Qt key code onto the equivalent Windows virtual-key (VK_*) code.
 * @param key The Qt key from the key sequence.
 * @param keypad Whether the key was pressed on the numeric keypad (Qt reports that as a modifier flag).
 * @return The virtual-key code, or 0 if there is no equivalent.
 */
UINT QtKeyToVirtualKey(const Qt::Key key, const bool keypad) {
    if (keypad && key >= Qt::Key_0 && key <= Qt::Key_9) {
        return VK_NUMPAD0 + (key - Qt::Key_0);
    }
    // Qt's codes for A-Z and 0-9 are their ASCII values, which happen to be the virtual-key codes too.
    if ((key >= Qt::Key_A && key <= Qt::Key_Z) || (key >= Qt::Key_0 && key <= Qt::Key_9)) {
        return static_cast<UINT>(key);
    }
    // F1..F24 are consecutive in both enumerations, so an offset is enough.
    if (key >= Qt::Key_F1 && key <= Qt::Key_F24) {
        return VK_F1 + (key - Qt::Key_F1);
    }

    switch (key) {
        case Qt::Key_Escape:     return VK_ESCAPE;
        case Qt::Key_Tab:        return VK_TAB;
        case Qt::Key_Backspace:  return VK_BACK;
        case Qt::Key_Return:
        case Qt::Key_Enter:      return VK_RETURN;
        case Qt::Key_Insert:     return VK_INSERT;
        case Qt::Key_Delete:     return VK_DELETE;
        case Qt::Key_Pause:      return VK_PAUSE;
        case Qt::Key_Print:      return VK_SNAPSHOT;
        case Qt::Key_Home:       return VK_HOME;
        case Qt::Key_End:        return VK_END;
        case Qt::Key_Left:       return VK_LEFT;
        case Qt::Key_Up:         return VK_UP;
        case Qt::Key_Right:      return VK_RIGHT;
        case Qt::Key_Down:       return VK_DOWN;
        case Qt::Key_PageUp:     return VK_PRIOR;
        case Qt::Key_PageDown:   return VK_NEXT;
        case Qt::Key_Space:      return VK_SPACE;
        case Qt::Key_ScrollLock: return VK_SCROLL;
        default: break;
    }

    // Punctuation keys move around between keyboard layouts, so ask Windows which physical key produces the
    // character on the *current* layout. VkKeyScanW returns the VK code in the low byte (and the needed shift
    // state in the high byte, which isn't needed here). This also resolves shifted symbols: Qt records
    // Ctrl+Shift+1 as Ctrl+Shift+!, and VkKeyScanW('!') maps that back to the "1" key.
    if (key >= Qt::Key_Space && key <= Qt::Key_ydiaeresis) {
        const SHORT scan = VkKeyScanW(static_cast<wchar_t>(key));
        if (scan != -1) {
            return LOBYTE(scan);
        }
    }
    return 0;
}

} // namespace

/** @brief No platform state is needed on Windows: hotkeys are tracked by the OS, keyed by id. */
struct GlobalHotkeyManager::Impl {};

GlobalHotkeyManager::GlobalHotkeyManager(QObject* parent) : QObject(parent), impl(std::make_unique<Impl>()) {
    QCoreApplication::instance()->installNativeEventFilter(this);
}

GlobalHotkeyManager::~GlobalHotkeyManager() {
    // Windows also releases hotkeys when the process exits, but releasing them explicitly lets a manager be
    // destroyed and recreated within one run.
    for (auto it = registered.cbegin(); it != registered.cend(); ++it) {
        UnregisterHotKey(nullptr, it.key());
    }
    QCoreApplication::instance()->removeNativeEventFilter(this);
}

bool GlobalHotkeyManager::IsAvailable() const {
    return true;
}

bool GlobalHotkeyManager::CanRegister(const QKeySequence& sequence) const {
    NativeHotkey hotkey;
    return ToNative(sequence, hotkey);
}

bool GlobalHotkeyManager::Register(const int id, const QKeySequence& sequence) {
    Unregister(id);

    NativeHotkey hotkey;
    if (!ToNative(sequence, hotkey)) {
        return false;
    }

    // MOD_NOREPEAT: holding the combination down fires once, instead of repeating at the keyboard repeat rate.
    if (!RegisterHotKey(nullptr, id, hotkey.modifiers | MOD_NOREPEAT, hotkey.key)) {
        return false;  // Most likely ERROR_HOTKEY_ALREADY_REGISTERED: another app owns this combination.
    }
    registered.insert(id, hotkey);
    return true;
}

void GlobalHotkeyManager::Unregister(const int id) {
    if (registered.remove(id)) {
        UnregisterHotKey(nullptr, id);
    }
}

bool GlobalHotkeyManager::ToNative(const QKeySequence& sequence, NativeHotkey& hotkey) const {
    if (sequence.isEmpty()) {
        return false;
    }

    // A QKeySequence can hold up to four chords (like Emacs' "Ctrl+X, Ctrl+S"). Global hotkeys are single
    // chords, so only the first one is used. A QKeyCombination is one key plus its modifier flags.
    const QKeyCombination combination = sequence[0];
    const Qt::KeyboardModifiers qtModifiers = combination.keyboardModifiers();

    hotkey.key = QtKeyToVirtualKey(combination.key(), qtModifiers.testFlag(Qt::KeypadModifier));
    if (hotkey.key == 0) {
        return false;
    }

    hotkey.modifiers = 0;
    if (qtModifiers.testFlag(Qt::ControlModifier)) hotkey.modifiers |= MOD_CONTROL;
    if (qtModifiers.testFlag(Qt::ShiftModifier))   hotkey.modifiers |= MOD_SHIFT;
    if (qtModifiers.testFlag(Qt::AltModifier))     hotkey.modifiers |= MOD_ALT;
    if (qtModifiers.testFlag(Qt::MetaModifier))    hotkey.modifiers |= MOD_WIN;  // Qt calls the Windows key "Meta".
    return true;
}

bool GlobalHotkeyManager::nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) {
    Q_UNUSED(result)
    // Qt uses "windows_generic_MSG" for messages from its event loop and "windows_dispatcher_MSG" for some
    // dispatched ones. A thread message like WM_HOTKEY may arrive as either, depending on the Qt version.
    if (eventType != "windows_generic_MSG" && eventType != "windows_dispatcher_MSG") {
        return false;
    }

    const MSG* msg = static_cast<const MSG*>(message);
    if (msg->message == WM_HOTKEY && registered.contains(static_cast<int>(msg->wParam))) {
        emit Activated(static_cast<int>(msg->wParam));
        return true;  // Consumed: Qt has nothing to do with this message.
    }
    return false;
}
