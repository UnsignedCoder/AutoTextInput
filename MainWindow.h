#pragma once

/**
 * @file MainWindow.h
 * @brief The full application window, opened by clicking the tray icon.
 */

#include "AppSettings.h"

#include <QWidget>

// Forward declarations: the header only stores pointers to these widgets, so the compiler just needs to know
// they are classes. Including their full headers here would slow down every file that includes MainWindow.h.
class QCheckBox;
class QDoubleSpinBox;
class QKeySequenceEdit;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;

/**
 * @brief Settings and manual-typing window.
 *
 * Lets the user paste text and type it after a delay, adjust typing speed and timing, configure the global
 * shortcuts, and toggle notifications and sounds.
 *
 * Design: this class is a "view" and holds no application logic. Every user action is reported through a
 * signal, and the owner (TrayController) decides what to do. The owner pushes state back in through the
 * public Set*() methods. Keeping the window passive means it can be redesigned without touching the logic,
 * and the logic can run (from hotkeys and the tray menu) without the window ever being shown.
 *
 * Settings are applied live: every change to a widget immediately emits SettingsChanged with the complete,
 * updated settings. There is no "Save" button.
 *
 * Closing the window only hides it. The app keeps running in the tray.
 */
class MainWindow : public QWidget {
    Q_OBJECT

public:
    /**
     * @brief Builds the window and fills every widget from @p settings. The window starts hidden.
     * @param settings The values to show initially.
     * @param parent Normally null, since this is a top-level window.
     */
    explicit MainWindow(const AppSettings& settings, QWidget* parent = nullptr);

    /** @brief Shows the window (restoring it if minimized) and brings it to the foreground. */
    void ShowAndActivate();

    /**
     * @brief Updates the buttons and status line to reflect whether a typing run is in progress.
     * @param typing true while TypingEngine is running.
     */
    void SetTyping(bool typing);

    /**
     * @brief Shows the result of the last attempt to register the global shortcuts.
     * @param errorMessage A description of the problem, or an empty string if every shortcut is active.
     */
    void SetHotkeyStatus(const QString& errorMessage);

    /**
     * @brief Shows a banner at the top of the window about features the current desktop session can't support.
     * @param warning The text from Platform::SessionWarning(). Empty hides the banner.
     */
    void SetPlatformWarning(const QString& warning);

signals:
    /**
     * @brief The user pressed "Type it". The window has already hidden itself, so focus returns to the
     *        previously active window.
     * @param text The text box contents. Never empty.
     */
    void TypeTextRequested(const QString& text);

    /** @brief The user pressed "Stop". */
    void StopRequested();

    /**
     * @brief Any setting widget changed.
     * @param settings The complete settings as currently shown in the window.
     */
    void SettingsChanged(const AppSettings& settings);

    /** @brief The user pressed "Quit AutoTextInput". */
    void QuitRequested();

protected:
    /**
     * @brief Hides the window instead of closing it when the user closes it, but allows programmatic closes.
     * @param event The close event. Calling ignore() vetoes the close.
     */
    void closeEvent(QCloseEvent* event) override;

private:
    /** @return A fresh AppSettings populated from the current widget values. */
    [[nodiscard]] AppSettings CollectSettings() const;

    /** @brief Enables "Type it" only when there is text to type and nothing is already typing. */
    void UpdateTypeButton();

    bool isTyping = false; ///< Mirror of the engine state, as last reported through SetTyping().

    // Child widgets. Qt's parent-child ownership deletes them along with the window, so there are no manual deletes.
    QLabel* platformWarningLabel = nullptr;
    QPlainTextEdit* textEdit = nullptr;
    QPushButton* typeButton = nullptr;
    QPushButton* stopButton = nullptr;
    QSpinBox* wordsPerMinuteSpin = nullptr;
    QDoubleSpinBox* startDelaySpin = nullptr;
    QCheckBox* varyTimingCheck = nullptr;
    QKeySequenceEdit* typeClipboardHotkeyEdit = nullptr;
    QKeySequenceEdit* stopTypingHotkeyEdit = nullptr;
    QLabel* hotkeyStatusLabel = nullptr;
    QCheckBox* notificationsCheck = nullptr;
    QCheckBox* soundsCheck = nullptr;
    QLabel* statusLabel = nullptr;
};
