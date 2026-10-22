/**
 * @file AppSettings.cpp
 * @brief Loading and saving AppSettings through QSettings.
 */

#include "AppSettings.h"

#include <QSettings>

AppSettings AppSettings::Load() {
    // A default-constructed AppSettings holds the default value of every field, from the member initializers.
    const AppSettings defaults;
    // A default-constructed QSettings uses the organization/application names set in main().
    const QSettings settings;
    AppSettings loaded;

    // value(key, fallback) returns a QVariant: the stored value, or the fallback if the key doesn't exist.
    // toUInt()/toBool() then convert it to the C++ type.
    loaded.wordsPerMinute = settings.value("typing/wordsPerMinute", defaults.wordsPerMinute).toUInt();
    loaded.startDelayMs = settings.value("typing/startDelayMs", defaults.startDelayMs).toUInt();
    loaded.varyTiming = settings.value("typing/varyTiming", defaults.varyTiming).toBool();
    loaded.showNotifications = settings.value("general/showNotifications", defaults.showNotifications).toBool();
    loaded.playSounds = settings.value("general/playSounds", defaults.playSounds).toBool();

    // Shortcuts are stored as text such as "Ctrl+Alt+V". PortableText always uses English key names
    // ("Ctrl", not the user's localized name), so the stored value reads back the same on any system locale.
    // An empty string is a valid saved value (the user cleared the shortcut), so the default is only used
    // when the key has never been written at all.
    loaded.typeClipboardHotkey = QKeySequence::fromString(
        settings.value("hotkeys/typeClipboard", defaults.typeClipboardHotkey.toString(QKeySequence::PortableText)).toString(),
        QKeySequence::PortableText);
    loaded.stopTypingHotkey = QKeySequence::fromString(
        settings.value("hotkeys/stopTyping", defaults.stopTypingHotkey.toString(QKeySequence::PortableText)).toString(),
        QKeySequence::PortableText);

    // Guard against a hand-edited or corrupted value. TypingEngine divides by this.
    if (loaded.wordsPerMinute == 0) {
        loaded.wordsPerMinute = defaults.wordsPerMinute;
    }

    return loaded;
}

void AppSettings::Save() const {
    QSettings settings;
    settings.setValue("typing/wordsPerMinute", wordsPerMinute);
    settings.setValue("typing/startDelayMs", startDelayMs);
    settings.setValue("typing/varyTiming", varyTiming);
    settings.setValue("general/showNotifications", showNotifications);
    settings.setValue("general/playSounds", playSounds);
    settings.setValue("hotkeys/typeClipboard", typeClipboardHotkey.toString(QKeySequence::PortableText));
    settings.setValue("hotkeys/stopTyping", stopTypingHotkey.toString(QKeySequence::PortableText));
    // QSettings writes to disk lazily (and when it is destroyed at the end of this function), so no explicit
    // sync() call is needed.
}
