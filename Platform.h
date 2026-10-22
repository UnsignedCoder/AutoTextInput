#pragma once

/**
 * @file Platform.h
 * @brief Process-wide, platform-specific setup and environment checks.
 *
 * Most platform differences are hidden behind the classes that need them (KeyboardSimulator,
 * GlobalHotkeyManager, Sounds). This header covers the few things that don't belong to any single class:
 * preparing the environment before Qt starts, and telling the user when the current desktop session
 * can't support every feature.
 *
 * Implementations:
 * - `platform/windows/Platform_win.cpp`
 * - `platform/linux/Platform_linux.cpp`
 */

#include <QString>

namespace Platform {

/**
 * @brief Adjusts the process environment before the QApplication is constructed.
 * @details Must be called at the very top of main(), before any Qt GUI object exists, because Qt reads
 *          these settings while it initializes.
 *          - Windows: nothing to do.
 *          - Linux: forces Qt's X11 ("xcb") backend unless the user explicitly chose another one. The
 *            global hotkey code listens to raw X11 events, which Qt only delivers when it runs on xcb.
 *            Under a Wayland session this makes Qt use XWayland instead.
 */
void PrepareEnvironment();

/**
 * @brief Checks whether the current desktop session supports global shortcuts and simulated typing.
 * @return A user-facing explanation of what won't work and why, or an empty string if everything is supported.
 *         Shown at the top of the main window and once as a tray notification at startup.
 */
QString SessionWarning();

} // namespace Platform
