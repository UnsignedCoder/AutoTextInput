/**
 * @file MainWindow.cpp
 * @brief Widget layout and signal wiring for the main window.
 *
 * Layout overview, top to bottom:
 *   [platform warning banner]   only visible when the session lacks support (e.g. Wayland)
 *   "Text to type" group        text box + Paste / Clear / Stop / Type it buttons
 *   "Typing" group              speed, start delay, vary-timing checkbox
 *   "Global shortcuts" group    two shortcut editors + status line
 *   notifications checkbox
 *   sounds checkbox
 *   status label ........ Quit button
 *
 * Qt layouts (QVBoxLayout, QHBoxLayout, QFormLayout) position and resize child widgets automatically. Passing
 * a widget to a layout's constructor installs the layout on that widget. Adding widgets to a layout makes
 * them children of the widget the layout is installed on.
 */

#include "MainWindow.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace {

/**
 * @brief Creates a shortcut editor limited to a single key combination.
 * @details QKeySequenceEdit records whatever keys the user presses while it has focus. By default it accepts
 *          multi-chord sequences ("Ctrl+K, Ctrl+C"), but OS global hotkeys only support one chord, so the
 *          length is capped at 1. The clear button (an "x" inside the field) lets the user remove a shortcut.
 */
QKeySequenceEdit* MakeHotkeyEdit(const QKeySequence& sequence, QWidget* parent) {
    auto* edit = new QKeySequenceEdit(sequence, parent);
    edit->setClearButtonEnabled(true);
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    edit->setMaximumSequenceLength(1);
#else
    // Qt < 6.5 (e.g. Ubuntu 24.04's Qt 6.4) has no length limit, so trim to the first chord as it's recorded.
    // setKeySequence() emits keySequenceChanged again, but with one chord the condition is false and the
    // recursion stops.
    QObject::connect(edit, &QKeySequenceEdit::keySequenceChanged, edit, [edit](const QKeySequence& recorded) {
        if (recorded.count() > 1) {
            edit->setKeySequence(QKeySequence(recorded[0]));
        }
    });
#endif
    return edit;
}

} // namespace

MainWindow::MainWindow(const AppSettings& settings, QWidget* parent) : QWidget(parent) {
    setWindowTitle("AutoTextInput");
    resize(520, 640);

    // --- Platform warning banner (hidden unless SetPlatformWarning() gives it text) ---
    platformWarningLabel = new QLabel(this);
    platformWarningLabel->setWordWrap(true);
    platformWarningLabel->setStyleSheet("background: #fff4ce; color: #5c4400; padding: 8px; border-radius: 4px;");
    platformWarningLabel->hide();

    // --- Text to type ---
    auto* textGroup = new QGroupBox("Text to type", this);
    textEdit = new QPlainTextEdit(textGroup);  // QPlainTextEdit rather than QTextEdit: plain text only, and faster.
    textEdit->setPlaceholderText("Paste or write the text you want typed out...");

    auto* pasteButton = new QPushButton("Paste clipboard", textGroup);
    auto* clearButton = new QPushButton("Clear", textGroup);
    typeButton = new QPushButton("Type it", textGroup);
    typeButton->setDefault(true);  // Drawn as the primary button.
    typeButton->setToolTip("Hides this window, waits for the start delay, then types into whichever window has focus.");
    stopButton = new QPushButton("Stop", textGroup);

    auto* textButtons = new QHBoxLayout;
    textButtons->addWidget(pasteButton);
    textButtons->addWidget(clearButton);
    textButtons->addStretch();  // Pushes the following buttons to the right edge.
    textButtons->addWidget(stopButton);
    textButtons->addWidget(typeButton);

    auto* textLayout = new QVBoxLayout(textGroup);
    textLayout->addWidget(textEdit);
    textLayout->addLayout(textButtons);

    // --- Typing options ---
    auto* typingGroup = new QGroupBox("Typing", this);
    wordsPerMinuteSpin = new QSpinBox(typingGroup);
    wordsPerMinuteSpin->setRange(10, 3000);
    wordsPerMinuteSpin->setSingleStep(10);
    wordsPerMinuteSpin->setSuffix(" WPM");
    wordsPerMinuteSpin->setValue(static_cast<int>(settings.wordsPerMinute));

    // The delay is stored in milliseconds but shown in seconds, which is friendlier to read.
    startDelaySpin = new QDoubleSpinBox(typingGroup);
    startDelaySpin->setRange(0.0, 30.0);
    startDelaySpin->setSingleStep(0.5);
    startDelaySpin->setDecimals(1);
    startDelaySpin->setSuffix(" s");
    startDelaySpin->setValue(settings.startDelayMs / 1000.0);
    startDelaySpin->setToolTip("Time to click into the target window after pressing \"Type it\". "
                               "Not used by the clipboard shortcut, which types immediately.");

    varyTimingCheck = new QCheckBox("Vary keystroke timing (more human-like)", typingGroup);
    varyTimingCheck->setChecked(settings.varyTiming);

    // QFormLayout arranges "label: field" rows in two aligned columns.
    auto* typingLayout = new QFormLayout(typingGroup);
    typingLayout->addRow("Speed:", wordsPerMinuteSpin);
    typingLayout->addRow("Start delay:", startDelaySpin);
    typingLayout->addRow(varyTimingCheck);

    // --- Global shortcuts ---
    auto* hotkeyGroup = new QGroupBox("Global shortcuts", this);
    typeClipboardHotkeyEdit = MakeHotkeyEdit(settings.typeClipboardHotkey, hotkeyGroup);
    stopTypingHotkeyEdit = MakeHotkeyEdit(settings.stopTypingHotkey, hotkeyGroup);
    hotkeyStatusLabel = new QLabel(hotkeyGroup);
    hotkeyStatusLabel->setWordWrap(true);

    auto* hotkeyLayout = new QFormLayout(hotkeyGroup);
    hotkeyLayout->addRow("Type clipboard:", typeClipboardHotkeyEdit);
    hotkeyLayout->addRow("Stop typing:", stopTypingHotkeyEdit);
    hotkeyLayout->addRow(hotkeyStatusLabel);

    // --- Footer ---
    notificationsCheck = new QCheckBox("Show tray notifications", this);
    notificationsCheck->setChecked(settings.showNotifications);
    soundsCheck = new QCheckBox("Play a sound when typing starts and stops", this);
    soundsCheck->setChecked(settings.playSounds);
    statusLabel = new QLabel(this);
    auto* quitButton = new QPushButton("Quit AutoTextInput", this);

    auto* footer = new QHBoxLayout;
    footer->addWidget(statusLabel, 1);  // Stretch factor 1: the label takes all spare width.
    footer->addWidget(quitButton);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(platformWarningLabel);
    layout->addWidget(textGroup, 1);  // Only the text group grows when the window is resized.
    layout->addWidget(typingGroup);
    layout->addWidget(hotkeyGroup);
    layout->addWidget(notificationsCheck);
    layout->addWidget(soundsCheck);
    layout->addLayout(footer);

    // --- Signal/slot connections ---
    // These are made *after* the widgets get their initial values. Connecting first would make every
    // setValue()/setChecked() above emit SettingsChanged while the window is still being built.

    connect(pasteButton, &QPushButton::clicked, this, [this] {
        textEdit->setPlainText(QApplication::clipboard()->text());
    });
    connect(clearButton, &QPushButton::clicked, textEdit, &QPlainTextEdit::clear);
    connect(textEdit, &QPlainTextEdit::textChanged, this, &MainWindow::UpdateTypeButton);
    connect(typeButton, &QPushButton::clicked, this, [this] {
        const QString text = textEdit->toPlainText();
        if (text.isEmpty()) {
            return;
        }
        // Hide first: the window manager then hands focus back to the window the user was in before opening
        // this one. That's usually exactly where they want the text to go.
        hide();
        emit TypeTextRequested(text);
    });
    connect(stopButton, &QPushButton::clicked, this, &MainWindow::StopRequested);
    connect(quitButton, &QPushButton::clicked, this, &MainWindow::QuitRequested);

    // Every settings widget reports through the same lambda, which sends a complete snapshot of all settings.
    // The receiver never has to merge partial updates.
    const auto emitSettingsChanged = [this] { emit SettingsChanged(CollectSettings()); };
    connect(wordsPerMinuteSpin, &QSpinBox::valueChanged, this, emitSettingsChanged);
    connect(startDelaySpin, &QDoubleSpinBox::valueChanged, this, emitSettingsChanged);
    connect(varyTimingCheck, &QCheckBox::toggled, this, emitSettingsChanged);
    connect(notificationsCheck, &QCheckBox::toggled, this, emitSettingsChanged);
    connect(soundsCheck, &QCheckBox::toggled, this, emitSettingsChanged);
    connect(typeClipboardHotkeyEdit, &QKeySequenceEdit::keySequenceChanged, this, emitSettingsChanged);
    connect(stopTypingHotkeyEdit, &QKeySequenceEdit::keySequenceChanged, this, emitSettingsChanged);

    SetTyping(false);
    SetHotkeyStatus({});
}

void MainWindow::ShowAndActivate() {
    if (isMinimized()) {
        showNormal();
    } else {
        show();
    }
    raise();           // Move to the top of the window stack.
    activateWindow();  // Ask for keyboard focus. The OS may refuse and flash the taskbar entry instead.
}

void MainWindow::SetTyping(const bool typing) {
    isTyping = typing;
    stopButton->setEnabled(typing);
    statusLabel->setText(typing ? "Typing..." : "Idle");
    UpdateTypeButton();
}

void MainWindow::SetHotkeyStatus(const QString& errorMessage) {
    if (errorMessage.isEmpty()) {
        hotkeyStatusLabel->setStyleSheet({});
        hotkeyStatusLabel->setText("Shortcuts work from any application. "
                                   "The stop shortcut is only claimed while typing is in progress.");
    } else {
        hotkeyStatusLabel->setStyleSheet("color: #d93025;");
        hotkeyStatusLabel->setText(errorMessage);
    }
}

void MainWindow::SetPlatformWarning(const QString& warning) {
    platformWarningLabel->setText(warning);
    platformWarningLabel->setVisible(!warning.isEmpty());
}

void MainWindow::closeEvent(QCloseEvent* event) {
    // "Spontaneous" events come from the window system, i.e. the user clicked the title-bar X or pressed
    // Alt+F4. Those just hide the window to the tray.
    // Non-spontaneous closes come from code: QApplication::quit() closes all windows, and so does a logoff or
    // shutdown (through QGuiApplication::commitDataRequest). Those must be accepted, or the window would veto
    // quitting and could block the OS session from ending.
    if (event->spontaneous()) {
        event->ignore();
        hide();
    } else {
        event->accept();
    }
}

AppSettings MainWindow::CollectSettings() const {
    AppSettings settings;
    settings.wordsPerMinute = static_cast<unsigned int>(wordsPerMinuteSpin->value());
    settings.startDelayMs = static_cast<unsigned int>(startDelaySpin->value() * 1000.0);
    settings.varyTiming = varyTimingCheck->isChecked();
    settings.showNotifications = notificationsCheck->isChecked();
    settings.playSounds = soundsCheck->isChecked();
    settings.typeClipboardHotkey = typeClipboardHotkeyEdit->keySequence();
    settings.stopTypingHotkey = stopTypingHotkeyEdit->keySequence();
    return settings;
}

void MainWindow::UpdateTypeButton() {
    typeButton->setEnabled(!isTyping && !textEdit->toPlainText().isEmpty());
}
