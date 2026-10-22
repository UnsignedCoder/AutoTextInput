/**
 * @file TrayController.cpp
 * @brief Implementation of TrayController: startup, wiring, and the reactions to hotkeys and typing events.
 */

#include "TrayController.h"

#include "Platform.h"
#include "Sounds.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QPainter>
#include <algorithm>

namespace {

/**
 * @brief Draws the app icon (a white "T" on a rounded square) at several sizes, so no image files are needed.
 * @param accent Background color, used to tell the idle (blue) and typing (green) states apart in the tray.
 * @details A QIcon can hold several pixmaps of different sizes. Qt picks the closest match for wherever the
 *          icon is shown (16 px tray, 32 px taskbar, high-DPI screens), which looks sharper than scaling one
 *          bitmap.
 */
QIcon MakeAppIcon(const QColor& accent) {
    QIcon icon;
    for (const int size : {16, 20, 24, 32, 48, 64, 128}) {
        QPixmap pixmap(size, size);
        pixmap.fill(Qt::transparent);

        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing);  // Smooth edges on the rounded corners.
        painter.setPen(Qt::NoPen);
        painter.setBrush(accent);
        const qreal radius = size * 0.22;
        painter.drawRoundedRect(QRectF(0, 0, size, size), radius, radius);

        // The "T": a horizontal bar and a vertical stem, with stroke width scaled to the icon size.
        painter.setPen(QPen(Qt::white, std::max(1.5, size / 7.0), Qt::SolidLine, Qt::RoundCap));
        const qreal margin = size * 0.27;
        const qreal center = size / 2.0;
        painter.drawLine(QPointF(margin, margin), QPointF(size - margin, margin));
        painter.drawLine(QPointF(center, margin), QPointF(center, size - margin));

        icon.addPixmap(pixmap);
    }
    return icon;
}

/**
 * @brief Rejects shortcuts that would hijack ordinary typing system-wide.
 * @return true if the sequence includes Ctrl, Alt or Win/Super, or is a key with no typing role (F-keys,
 *         Pause, Scroll Lock).
 * @details A global hotkey swallows its key combination in *every* application. Registering a bare "V" or
 *          "Shift+V" would make it impossible to type that letter anywhere while AutoTextInput runs.
 */
bool IsSafeGlobalHotkey(const QKeySequence& sequence) {
    const QKeyCombination combination = sequence[0];
    const Qt::KeyboardModifiers modifiers = combination.keyboardModifiers();
    if (modifiers & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)) {
        return true;
    }
    const Qt::Key key = combination.key();
    return (key >= Qt::Key_F1 && key <= Qt::Key_F35) || key == Qt::Key_Pause || key == Qt::Key_ScrollLock;
}

/** @return The shortcut as the user would expect to read it on this OS (e.g. "Ctrl+Alt+V"). */
QString ShortcutText(const QKeySequence& sequence) {
    return sequence.toString(QKeySequence::NativeText);
}

} // namespace

TrayController::TrayController(QObject* parent)
    : QObject(parent),
      settings(AppSettings::Load()),
      idleIcon(MakeAppIcon(QColor(0x2f, 0x6f, 0xeb))),
      typingIcon(MakeAppIcon(QColor(0x1e, 0x9e, 0x5a))),
      window(settings) {
    QApplication::setWindowIcon(idleIcon);  // Default icon for every window, the main window included.

    // --- Tray icon ---
    BuildTrayMenu();
    trayIcon.setIcon(idleIcon);
    trayIcon.setContextMenu(&trayMenu);  // Shown on right-click. Doesn't take ownership.
    connect(&trayIcon, &QSystemTrayIcon::activated, this, [this](const QSystemTrayIcon::ActivationReason reason) {
        // Trigger = single left click, DoubleClick = double left click. Context (right click) shows the menu.
        if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick) {
            ShowWindow();
        }
    });

    // --- Hotkeys and typing engine ---
    connect(&hotkeys, &GlobalHotkeyManager::Activated, this, &TrayController::OnHotkeyActivated);
    // The engine emits from its worker thread, and these connections are automatically queued (see TypingEngine.h).
    connect(&typingEngine, &TypingEngine::TypingStarted, this, &TrayController::OnTypingStarted);
    connect(&typingEngine, &TypingEngine::TypingFinished, this, &TrayController::OnTypingFinished);

    // --- Window ---
    connect(&window, &MainWindow::TypeTextRequested, this, [this](const QString& text) {
        StartTyping(text, settings.startDelayMs);
    });
    connect(&window, &MainWindow::StopRequested, &typingEngine, &TypingEngine::Stop);
    connect(&window, &MainWindow::SettingsChanged, this, &TrayController::ApplySettings);
    connect(&window, &MainWindow::QuitRequested, this, &TrayController::Quit);

    const QString platformWarning = Platform::SessionWarning();
    window.SetPlatformWarning(platformWarning);
    window.SetHotkeyStatus(RegisterTypeClipboardHotkey());
    UpdateTrayState();

    if (!QSystemTrayIcon::isSystemTrayAvailable()) {
        // No notification area (e.g. stock GNOME without the AppIndicator extension). The app would be
        // unreachable, so fall back to showing the window.
        ShowWindow();
        return;
    }
    trayIcon.show();

    // Startup hint, so the user knows the app is running even though no window opened.
    if (!platformWarning.isEmpty()) {
        Notify("AutoTextInput: limited support", platformWarning, QSystemTrayIcon::Warning);
    } else if (settings.typeClipboardHotkey.isEmpty()) {
        Notify("AutoTextInput is running", "Click the tray icon to open settings and set a typing shortcut.");
    } else {
        Notify("AutoTextInput is running",
               QString("Copy some text, then press %1 to type it. Click the tray icon for settings.")
                   .arg(ShortcutText(settings.typeClipboardHotkey)));
    }
}

void TrayController::ShowWindow() {
    window.ShowAndActivate();
}

void TrayController::BuildTrayMenu() {
    // addAction(text, receiver, slot) creates the item, connects its triggered() signal, and gives the menu
    // ownership of the QAction.
    QAction* openAction = trayMenu.addAction("Open AutoTextInput", this, &TrayController::ShowWindow);
    trayMenu.setDefaultAction(openAction);  // Shown in bold, the same action a left click performs.
    trayMenu.addSeparator();
    typeClipboardAction = trayMenu.addAction("Type clipboard", this, &TrayController::TypeClipboard);
    stopAction = trayMenu.addAction("Stop typing", &typingEngine, &TypingEngine::Stop);
    trayMenu.addSeparator();
    trayMenu.addAction("Quit", this, &TrayController::Quit);
}

void TrayController::ApplySettings(const AppSettings& newSettings) {
    settings = newSettings;
    settings.Save();

    window.SetHotkeyStatus(RegisterTypeClipboardHotkey());
    // The stop hotkey is only held during a run. If it was changed mid-run, swap the registration now.
    if (typingEngine.IsTyping()) {
        hotkeys.Register(StopTypingHotkeyId, settings.stopTypingHotkey);
    }
    UpdateTrayState();
}

QString TrayController::RegisterTypeClipboardHotkey() {
    hotkeys.Unregister(TypeClipboardHotkeyId);

    // The checks run from cheapest and most specific to the actual OS registration, so the user gets the
    // most precise explanation of what's wrong.
    const QKeySequence& sequence = settings.typeClipboardHotkey;
    if (!hotkeys.IsAvailable()) {
        return "Global shortcuts aren't available in this desktop session. Use the tray menu to type the clipboard.";
    }
    if (sequence.isEmpty()) {
        return "No typing shortcut is set, so clipboard typing is only available from the tray menu.";
    }
    if (!IsSafeGlobalHotkey(sequence)) {
        return QString("%1 would block normal typing in every application. "
                       "Include Ctrl, Alt or the Windows/Super key, or use an F-key.").arg(ShortcutText(sequence));
    }
    if (sequence == settings.stopTypingHotkey) {
        // They'd clash during a run, because the OS won't register the same combination twice.
        return "The typing and stop shortcuts must be different.";
    }
    if (!hotkeys.CanRegister(sequence)) {
        return QString("%1 can't be used as a global shortcut. Try a different key.").arg(ShortcutText(sequence));
    }
    if (!hotkeys.Register(TypeClipboardHotkeyId, sequence)) {
        return QString("%1 is already in use by the system or another application. Pick a different shortcut.")
            .arg(ShortcutText(sequence));
    }
    return {};
}

void TrayController::TypeClipboard() {
    if (typingEngine.IsTyping()) {
        return;
    }
    const QString text = QApplication::clipboard()->text();
    if (text.isEmpty()) {
        Notify("Nothing to type", "The clipboard doesn't contain any text.", QSystemTrayIcon::Warning);
        return;
    }
    // No start delay: when the hotkey is pressed, the user's cursor is already where the text should go.
    StartTyping(text, 0);
}

void TrayController::StartTyping(const QString& text, const unsigned int startDelayMs) {
    const TypingOptions options {settings.wordsPerMinute, startDelayMs, settings.varyTiming};
    if (!typingEngine.Start(text, options)) {
        return;
    }

    // The stop shortcut is only claimed while typing, so keys like Esc keep working normally the rest of the time.
    if (!settings.stopTypingHotkey.isEmpty() && !hotkeys.Register(StopTypingHotkeyId, settings.stopTypingHotkey)) {
        Notify("Stop shortcut unavailable",
               QString("%1 is in use by another application. Use the tray menu to stop typing.")
                   .arg(ShortcutText(settings.stopTypingHotkey)),
               QSystemTrayIcon::Warning);
    }

    if (startDelayMs > 0) {
        Notify("Get ready",
               QString("Typing starts in %1 s. Click where the text should go.").arg(startDelayMs / 1000.0));
    }
    UpdateTrayState();
}

void TrayController::OnTypingStarted() {
    if (settings.playSounds) {
        Sounds::PlayTypingStarted();
    }
}

void TrayController::OnTypingFinished(const TypingEngine::Result result, const QString& errorMessage) {
    if (settings.playSounds) {
        Sounds::PlayTypingStopped();
    }
    if (result == TypingEngine::Result::Failed) {
        Notify("Typing failed", errorMessage.isEmpty() ? QString("The simulated keystrokes were rejected.") : errorMessage,
               QSystemTrayIcon::Critical);
    }

    // Race guard: this notification is queued. If the user started a new run in the gap between the worker
    // finishing and this slot running, don't reset the new run's UI state.
    if (typingEngine.IsTyping()) {
        return;
    }
    hotkeys.Unregister(StopTypingHotkeyId);
    UpdateTrayState();
}

void TrayController::OnHotkeyActivated(const int id) {
    switch (id) {
        case TypeClipboardHotkeyId: TypeClipboard(); break;
        case StopTypingHotkeyId:    typingEngine.Stop(); break;
        default: break;
    }
}

void TrayController::UpdateTrayState() {
    const bool typing = typingEngine.IsTyping();

    trayIcon.setIcon(typing ? typingIcon : idleIcon);
    if (typing) {
        trayIcon.setToolTip(settings.stopTypingHotkey.isEmpty()
                                ? QString("AutoTextInput: typing...")
                                : QString("AutoTextInput: typing... (%1 to stop)").arg(ShortcutText(settings.stopTypingHotkey)));
    } else {
        trayIcon.setToolTip(settings.typeClipboardHotkey.isEmpty()
                                ? QString("AutoTextInput")
                                : QString("AutoTextInput: %1 types the clipboard").arg(ShortcutText(settings.typeClipboardHotkey)));
    }

    // In a Qt menu item, text after a tab character is drawn right-aligned in the shortcut column. That shows
    // the global shortcut without binding a local QShortcut to the item.
    typeClipboardAction->setText(settings.typeClipboardHotkey.isEmpty()
                                     ? QString("Type clipboard")
                                     : QString("Type clipboard\t%1").arg(ShortcutText(settings.typeClipboardHotkey)));
    stopAction->setText(settings.stopTypingHotkey.isEmpty()
                            ? QString("Stop typing")
                            : QString("Stop typing\t%1").arg(ShortcutText(settings.stopTypingHotkey)));
    typeClipboardAction->setEnabled(!typing);
    stopAction->setEnabled(typing);

    window.SetTyping(typing);
}

void TrayController::Notify(const QString& title, const QString& message, const QSystemTrayIcon::MessageIcon icon) {
    if (settings.showNotifications && trayIcon.isVisible()) {
        trayIcon.showMessage(title, message, icon, 4000);  // Shown for ~4 s (the OS may override this).
    }
}

void TrayController::Quit() {
    typingEngine.Stop();
    trayIcon.hide();  // Remove the icon right away, rather than leaving a "ghost" until the mouse passes over it.
    QApplication::quit();
}
