#pragma once

/**
 * @file GlobalHotkeyManager.h
 * @brief System-wide keyboard shortcuts that fire even while another application has focus.
 *
 * Normal Qt shortcuts (QShortcut, QAction::setShortcut) only work while one of the application's own windows
 * is focused. AutoTextInput needs the opposite: the user is in some other program when they press the
 * typing shortcut. That takes OS-level APIs, which live in:
 * - `platform/windows/GlobalHotkeyManager_win.cpp`: `RegisterHotKey`, which posts `WM_HOTKEY` messages.
 * - `platform/linux/GlobalHotkeyManager_x11.cpp`: passive key grabs on the X11 root window (`xcb_grab_key`).
 *
 * Either way, the OS reports a pressed hotkey as a raw native event that arrives inside Qt's event loop. Qt
 * can't translate it into a QEvent, so this class installs itself as a QAbstractNativeEventFilter. That
 * gives it a look at every native event before Qt processes it, so it can pick out its own hotkey events and
 * turn them into the Qt signal Activated().
 */

#include <QAbstractNativeEventFilter>
#include <QHash>
#include <QKeySequence>
#include <QObject>
#include <memory>

/**
 * @brief Registers global hotkeys and emits Activated() when one is pressed.
 * @warning Must be created and used on the GUI thread. On both platforms the hotkey events are delivered to
 *          the GUI thread's event loop.
 */
class GlobalHotkeyManager : public QObject, public QAbstractNativeEventFilter {
    Q_OBJECT

public:
    /**
     * @brief Installs the native event filter and acquires any platform resources the manager needs.
     * @param parent Standard QObject parent, for ownership. May be null.
     */
    explicit GlobalHotkeyManager(QObject* parent = nullptr);

    /** @brief Unregisters every hotkey still held and removes the native event filter. */
    ~GlobalHotkeyManager() override;

    /**
     * @return true if global hotkeys can work in this session at all.
     * @note Always true on Windows. On Linux, false when no X11 connection is available (for example when
     *       Qt was forced onto its native Wayland backend).
     */
    [[nodiscard]] bool IsAvailable() const;

    /**
     * @brief Checks whether a key sequence can be expressed as a native global hotkey, without registering it.
     * @param sequence The shortcut to check. Only its first key combination matters.
     * @return true if the key can be mapped onto a native key code.
     */
    [[nodiscard]] bool CanRegister(const QKeySequence& sequence) const;

    /**
     * @brief Registers @p sequence as a global hotkey under @p id, replacing anything previously registered with that id.
     * @param id Caller-chosen identifier, reported back through Activated(). Must be in the range 0x0000-0xBFFF,
     *           a limit that comes from the Windows API.
     * @param sequence The shortcut. Only its first key combination is used.
     * @return false if the sequence can't be mapped, or another application already owns the combination.
     */
    bool Register(int id, const QKeySequence& sequence);

    /**
     * @brief Releases the hotkey registered under @p id.
     * @param id The identifier passed to Register(). Unknown ids are ignored.
     */
    void Unregister(int id);

    /**
     * @brief Inspects every native event Qt receives, and consumes the ones that are presses of our hotkeys.
     * @param eventType Identifies the kind of native event: "windows_generic_MSG" on Windows,
     *                  "xcb_generic_event_t" on Linux/X11.
     * @param message Pointer to the native event structure (`MSG` or `xcb_generic_event_t`).
     * @param result Windows-only out-parameter for a message result. Unused here.
     * @return true to swallow the event (one of our hotkeys), false to let Qt process it normally.
     */
    bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) override;

signals:
    /**
     * @brief Emitted on the GUI thread when a registered hotkey is pressed.
     * @param id The identifier the hotkey was registered with.
     */
    void Activated(int id);

private:
    /**
     * @brief A hotkey translated into the platform's own terms.
     * @details The same two fields work on both platforms, they just hold different values:
     *          - Windows: `modifiers` is a combination of MOD_CONTROL/MOD_SHIFT/MOD_ALT/MOD_WIN, `key` is a VK_* code.
     *          - X11: `modifiers` is a mask of ControlMask/ShiftMask/Mod1Mask/Mod4Mask, `key` is a hardware keycode.
     */
    struct NativeHotkey {
        quint32 modifiers = 0;
        quint32 key = 0;
    };

    /**
     * @brief Translates a Qt key sequence into the platform's hotkey representation.
     * @return false if the key has no native equivalent.
     */
    bool ToNative(const QKeySequence& sequence, NativeHotkey& hotkey) const;

    struct Impl;                         ///< Platform-specific state (X11 connections on Linux, empty on Windows).
    std::unique_ptr<Impl> impl;          ///< Always non-null after construction. See KeyboardSimulator.h for the pimpl idiom.
    QHash<int, NativeHotkey> registered; ///< Every currently registered hotkey, keyed by caller id.
};
