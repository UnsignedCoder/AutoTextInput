#pragma once

/**
 * @file AppSettings.h
 * @brief The user's preferences, and how they are saved between runs.
 */

#include <QKeySequence>

/**
 * @brief User-configurable options, persisted with QSettings.
 *
 * QSettings stores key/value pairs in the platform's native place:
 * - Windows: the registry, under `HKEY_CURRENT_USER\Software\UnsignedCoder\AutoTextInput`.
 * - Linux: an INI file at `~/.config/UnsignedCoder/AutoTextInput.conf`.
 *
 * Keys are grouped with slashes ("typing/wordsPerMinute"). That becomes a subkey in the registry and an
 * `[typing]` section in the INI file.
 *
 * This is a plain value type (a "struct of settings") that is copied freely. MainWindow builds one from its
 * widgets, TrayController stores the current one, and Load()/Save() move it to and from disk.
 *
 * @note QCoreApplication's organization and application names must be set before Load() or Save() is called,
 *       because QSettings uses them to find its storage location. main() does that first.
 */
struct AppSettings {
    unsigned int wordsPerMinute = 250;   ///< Typing speed, assuming an average word length of 5 characters.
    unsigned int startDelayMs = 3000;    ///< Delay before typing started from the window, giving time to focus the target.
    bool varyTiming = true;              ///< Randomize the delay between keystrokes so typing looks less mechanical.
    bool showNotifications = true;       ///< Show tray balloon notifications (startup hint, errors, countdowns).
    bool playSounds = true;              ///< Play an audio cue when typing starts and when it stops.

    /// Global shortcut that types the clipboard. An empty sequence means "no shortcut".
    QKeySequence typeClipboardHotkey = QKeySequence(QStringLiteral("Ctrl+Alt+V"));

    /// Global shortcut that cancels typing. Only claimed while typing is in progress. Empty means "no shortcut".
    QKeySequence stopTypingHotkey = QKeySequence(Qt::Key_Escape);

    /**
     * @brief Reads the saved settings. Anything never saved falls back to the defaults above.
     * @return The loaded settings, with invalid values (e.g. a speed of 0) replaced by defaults.
     */
    static AppSettings Load();

    /** @brief Writes these settings to persistent storage, overwriting what was there. */
    void Save() const;
};
