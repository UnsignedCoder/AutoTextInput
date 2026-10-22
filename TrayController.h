#pragma once

/**
 * @file TrayController.h
 * @brief The application's central coordinator: tray icon, global hotkeys, typing engine and window.
 */

#include "AppSettings.h"
#include "GlobalHotkeyManager.h"
#include "MainWindow.h"
#include "TypingEngine.h"

#include <QIcon>
#include <QMenu>
#include <QObject>
#include <QSystemTrayIcon>

class QAction;

/**
 * @brief Owns the application's state and connects all the components together.
 *
 * The app starts with only the tray icon visible. Typing is normally triggered with the global "type
 * clipboard" shortcut, which types the clipboard into whichever window has focus. Clicking the tray icon
 * opens MainWindow for manual text entry and configuration.
 *
 * Event flow:
 * @code
 *   Global hotkey ──► GlobalHotkeyManager::Activated ──┐
 *   Tray menu click ───────────────────────────────────┼──► TrayController ──► TypingEngine::Start / Stop
 *   MainWindow buttons ────────────────────────────────┘          ▲
 *                                                                 │ TypingStarted / TypingFinished
 *   TypingEngine (worker thread) ─────────────────────────────────┘ (queued onto the GUI thread)
 *                                   └──► tray icon, menu, window status, sounds, notifications
 * @endcode
 *
 * Ownership: all components are plain data members, not heap pointers, so they are constructed in
 * declaration order and destroyed in reverse order automatically. The declaration order in this header is
 * deliberate: `settings` must be loaded before `window` is built from it, and `trayMenu` must outlive
 * `trayIcon`, which holds a pointer to it.
 */
class TrayController : public QObject {
    Q_OBJECT

public:
    /**
     * @brief Loads settings, builds the tray icon and window, and registers the typing hotkey.
     * @param parent Standard QObject parent. Normally null: main() owns the controller on its stack.
     */
    explicit TrayController(QObject* parent = nullptr);

    /** @brief Opens the full application window. */
    void ShowWindow();

private:
    /// Identifiers passed to GlobalHotkeyManager. Any distinct values in 0x0000-0xBFFF would do.
    enum HotkeyId : int {
        TypeClipboardHotkeyId = 1,
        StopTypingHotkeyId = 2
    };

    /** @brief Creates the tray icon's right-click menu. */
    void BuildTrayMenu();

    /**
     * @brief Adopts new settings from the window: saves them, re-registers the hotkeys, and refreshes the UI.
     * @param newSettings The complete settings snapshot from MainWindow::SettingsChanged.
     */
    void ApplySettings(const AppSettings& newSettings);

    /**
     * @brief (Re)registers the "type clipboard" hotkey from the current settings.
     * @return A user-facing description of the problem, or an empty string on success.
     */
    QString RegisterTypeClipboardHotkey();

    /** @brief Types the current clipboard text immediately. Does nothing if a run is in progress. */
    void TypeClipboard();

    /**
     * @brief Starts a typing run and switches the UI into the "typing" state.
     * @param text The text to type.
     * @param startDelayMs Pause before typing: the configured delay for window-initiated typing, 0 for the hotkey.
     */
    void StartTyping(const QString& text, unsigned int startDelayMs);

    /** @brief Reacts to TypingEngine::TypingStarted: plays the start sound. */
    void OnTypingStarted();

    /**
     * @brief Reacts to TypingEngine::TypingFinished: plays the stop sound, reports failures, resets the UI.
     * @param result How the run ended.
     * @param errorMessage Platform explanation for Result::Failed.
     */
    void OnTypingFinished(TypingEngine::Result result, const QString& errorMessage);

    /** @brief Dispatches a global hotkey press to the matching action. */
    void OnHotkeyActivated(int id);

    /** @brief Brings the tray icon, tooltip, menu items and window status in line with the current state. */
    void UpdateTrayState();

    /**
     * @brief Shows a tray balloon/notification, if notifications are enabled and the tray is visible.
     * @param title Bold first line.
     * @param message Body text.
     * @param icon Information, Warning or Critical.
     */
    void Notify(const QString& title, const QString& message,
                QSystemTrayIcon::MessageIcon icon = QSystemTrayIcon::Information);

    /** @brief Stops typing, removes the tray icon and ends the event loop. */
    void Quit();

    AppSettings settings;           ///< The current settings. Always in sync with what MainWindow shows.
    TypingEngine typingEngine;      ///< Performs typing on its own thread.
    GlobalHotkeyManager hotkeys;    ///< System-wide shortcuts.
    QIcon idleIcon;                 ///< Blue tray icon.
    QIcon typingIcon;               ///< Green tray icon, shown while typing.
    QMenu trayMenu;                 ///< Right-click menu. Must be declared before trayIcon (see class docs).
    QSystemTrayIcon trayIcon;       ///< The icon in the notification area.
    MainWindow window;              ///< Hidden until the user opens it.

    QAction* typeClipboardAction = nullptr; ///< "Type clipboard" menu item. Owned by trayMenu.
    QAction* stopAction = nullptr;          ///< "Stop typing" menu item. Owned by trayMenu.
};
