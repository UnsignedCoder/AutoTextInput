# AutoTextInput

A C++/Qt application for Windows and Linux that automates typing text into any active window. It simulates human typing, which can be useful for data entry, testing, or avoiding copy-paste detection.

AutoTextInput runs quietly in the system tray. Copy some text, put the cursor where you want it, press the global shortcut, and the text is typed out for you.

## Features

-   Lives in the system tray (under "Show hidden icons" on Windows 11). No window opens at launch.
-   Global shortcut (default `Ctrl+Alt+V`) types the clipboard into whichever window has focus. No Alt+Tab is needed.
-   Global stop shortcut (default `Esc`) cancels typing mid-text. It is only claimed while typing, so Esc works normally the rest of the time.
-   Both shortcuts are configurable in the app window.
-   Full window, opened from the tray icon, where you can:
    -   Paste or write text and type it after a configurable start delay, giving you time to click into the target window.
    -   Set the typing speed in words per minute.
    -   Turn on randomized keystroke timing for more human-like typing.
    -   Turn tray notifications on or off.
    -   Turn the start/stop sounds on or off.
-   Short sounds play when typing starts (rising chirp) and when it stops (falling chirp), so you know what's happening without looking.
-   Types any Unicode text, including accents, symbols, CJK and emoji, whatever the keyboard layout.
-   Typing runs on its own thread, so the app stays responsive and typing can be stopped at any time.
-   Settings are saved between runs.
-   Only one instance runs at a time.

## How to Use

1.  Run `AutoTextInput`. A blue "T" icon appears in the system tray (on Windows you may need to click the `^` "Show hidden icons" arrow). The tooltip shows the active shortcut.
2.  Copy the text you want typed.
3.  Click into the target text field and press `Ctrl+Alt+V`. The icon turns green while typing.
4.  Press `Esc` at any time to stop.

To type text manually or change settings, click the tray icon to open the window. Paste your text and press **Type it**. The window hides, and after the start delay (3 s by default) the text is typed into whichever window has focus.

Closing the window only hides it back to the tray. To exit, right-click the tray icon and choose **Quit**, or press **Quit AutoTextInput** in the window.

Launch with `--show` to open the window immediately.

## Platform Support

| | Windows 10/11 | Linux, X11 session | Linux, Wayland session |
| --- | --- | --- | --- |
| Tray icon | Yes | Yes (KDE, Xfce, Cinnamon, MATE...; GNOME needs the AppIndicator extension) | Same as X11 |
| Global shortcuts | Yes | Yes | Only while an X11 (XWayland) app has focus |
| Simulated typing | Yes | Yes | Only into X11 (XWayland) apps |
| Sounds | Yes | Yes, if built with Qt Multimedia | Same as X11 |

**Windows:** a normal program can't send keystrokes to an application running as administrator. To type into an elevated app, run AutoTextInput as administrator too.

**Linux / Wayland:** Wayland deliberately stops applications from reading global keystrokes or injecting input into other apps, and there's no cross-desktop API for it yet. AutoTextInput connects through XWayland, so it works with X11 apps, and the window shows a warning about the limitation. For full support, log in to an "X11" / "Xorg" session. If your desktop has no tray, the window opens at launch instead.

## Settings and Data

Settings are applied as soon as you change them and saved automatically:

| | Windows | Linux |
| --- | --- | --- |
| Settings | Registry: `HKEY_CURRENT_USER\Software\UnsignedCoder\AutoTextInput` | `~/.config/UnsignedCoder/AutoTextInput.conf` |
| Single-instance lock | `%LOCALAPPDATA%\UnsignedCoder\AutoTextInput\instance.lock` | `~/.local/share/UnsignedCoder/AutoTextInput/instance.lock` |

To reset to the defaults, quit the app and delete the settings entry or file.

## How It Works

-   **Typing:** on Windows, `SendInput` with Unicode keyboard events, so any character works whatever the keyboard layout. On Linux, the X11 XTest extension. Characters missing from the current layout are typed by briefly binding them to a spare keycode.
-   **Global shortcuts:** `RegisterHotKey` on Windows, passive key grabs on the X11 root window on Linux. Both are picked out of Qt's event loop with a native event filter.
-   **Background typing:** a dedicated `std::jthread` with cooperative cancellation (`std::stop_token`), so the UI and the stop shortcut stay responsive.
-   **Sounds:** synthesized in code (no audio files) and played through `PlaySound` on Windows or Qt Multimedia on Linux.

## Troubleshooting

| Problem | Fix |
| --- | --- |
| Shortcut shows "already in use" | Another app owns that combination. Record a different one in the window. |
| Nothing is typed into a particular app | On Windows, the app may be running as administrator. On Linux/Wayland, it may be a native Wayland app (see Platform Support). |
| Characters go missing at high speed | Some apps can't keep up. Lower the WPM. |
| Extra characters appear | The target app's autocomplete is reacting to the typed text. Turn it off there. |
| "AutoTextInput is already running" | Look in the tray (Windows: "Show hidden icons"). A lock left behind by a crash is cleared automatically on the next launch. |
| No tray icon on GNOME | Install the "AppIndicator and KStatusNotifierItem Support" extension. Without it, the window opens at launch instead. |

## Building from Source

This is a CMake (3.24+) project that needs a C++20 compiler and Qt 6.4 or newer.

### Windows

1.  Install a C++ compiler (MinGW-w64 or MSVC), CMake, and Qt 6.
2.  Generate the build files, pointing CMake at your Qt installation: `cmake -B build -DCMAKE_PREFIX_PATH=C:/Qt/6.x.x/mingw_64`
3.  Build: `cmake --build build`
4.  The executable is in `build/`. A post-build step runs `windeployqt` and copies the required Qt and MinGW runtime DLLs next to it.

### Linux

1.  Install the dependencies:
    -   Debian/Ubuntu: `sudo apt install build-essential cmake qt6-base-dev qt6-multimedia-dev libx11-dev libxtst-dev`
    -   Fedora: `sudo dnf install gcc-c++ cmake qt6-qtbase-devel qt6-qtmultimedia-devel libX11-devel libXtst-devel libxcb-devel`
    -   Arch: `sudo pacman -S base-devel cmake qt6-base qt6-multimedia libx11 libxtst`
2.  Configure and build: `cmake -B build && cmake --build build`
3.  Run `./build/AutoTextInput`, or install it with `sudo cmake --install build`.

Qt Multimedia is optional: without it, the app builds and works but plays no sounds.

## Project Structure

| Path | Purpose |
| --- | --- |
| `main.cpp` | Entry point: environment setup, single-instance check, event loop. |
| `TrayController` | Owns the tray icon and menu, and connects the hotkeys, typing engine, and window. |
| `MainWindow` | The settings and manual-typing window. |
| `TypingEngine` | Typing timing and cancellation on a background thread. |
| `AppSettings` | Settings that persist between runs (`QSettings`). |
| `SoundSynth` | Generates the audio cues as PCM samples / WAV data. |
| `KeyboardSimulator.h` | Interface: send keystrokes to the focused window. |
| `GlobalHotkeyManager.h` | Interface: system-wide shortcuts. |
| `Sounds.h`, `Platform.h` | Interfaces: play audio cues; platform setup and session checks. |
| `platform/windows/` | Win32 implementations (`SendInput`, `RegisterHotKey`, `PlaySound`). |
| `platform/linux/` | X11 implementations (XTest, `xcb_grab_key`, Qt Multimedia). |

Shared code never includes OS headers. Each interface has one implementation per platform under `platform/<os>/`, and CMake compiles only the folder for the target OS. Every source file carries Doxygen-style documentation of its role, mechanisms and design decisions.

## Roadmap

-   [x] Fix the issue where the program does not exit correctly after the last paragraph is sent.
-   [x] Refactor the code to use a dedicated typing thread, allowing the main thread to handle the UI and a keyboard listener.
-   [x] Implement a global hotkey to capture clipboard content and start typing immediately, removing the need for `Alt+Tab`.
-   [x] Create a graphical user interface (GUI) for easier interaction.
-   [x] Allow the program to run in the system tray for background access.
-   [x] Linux (X11) support.
