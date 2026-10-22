/**
 * @file Platform_linux.cpp
 * @brief Linux implementation of Platform.h.
 *
 * Background, X11 vs. Wayland:
 * Linux desktops run on one of two display systems.
 * - **X11** (Xorg) is the older one. It lets any client observe and inject input globally, which is exactly
 *   what AutoTextInput needs: XTest for typing, and key grabs for global shortcuts.
 * - **Wayland** is the newer one, the default on recent GNOME and KDE. Its security model deliberately stops
 *   applications from reading other apps' keystrokes or injecting input into them. There is no portable
 *   Wayland API for global hotkeys or synthetic typing yet. Compositor-specific portals exist, but they differ
 *   between desktops.
 *
 * Wayland sessions usually still run **XWayland**, an X server for legacy apps. AutoTextInput connects to it
 * through Qt's xcb backend, so shortcuts and typing work whenever an X11 app has focus, but not when a native
 * Wayland app does. SessionWarning() tells the user about that limitation.
 */

#include "Platform.h"

#include <QGuiApplication>

namespace Platform {

void PrepareEnvironment() {
    // Respect an explicit choice (e.g. QT_QPA_PLATFORM=wayland), but default to xcb (X11). The global hotkey
    // code listens to raw xcb events, which Qt only delivers when it runs on the xcb backend.
    if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM")) {
        qputenv("QT_QPA_PLATFORM", "xcb");
    }
}

QString SessionWarning() {
    if (QGuiApplication::platformName() != QLatin1String("xcb")) {
        return "AutoTextInput is not connected to an X11 display, so global shortcuts and typing are unavailable. "
               "Start it without QT_QPA_PLATFORM, or with QT_QPA_PLATFORM=xcb.";
    }
    if (qEnvironmentVariable("XDG_SESSION_TYPE") == QLatin1String("wayland")
        || qEnvironmentVariableIsSet("WAYLAND_DISPLAY")) {
        return "You are in a Wayland session. Global shortcuts and typing only work while an X11 (XWayland) "
               "application has focus. Log in to an X11 session for full support.";
    }
    return {};
}

} // namespace Platform
