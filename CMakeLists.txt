# =====================================================================================================================
# AutoTextInput build script.
#
# Shared code lives at the repository root. Platform-specific code lives under platform/<os>/, and exactly one
# platform folder is compiled, chosen below by the target operating system.
# =====================================================================================================================

# 3.24 is needed for the X11::xcb imported target on Linux.
cmake_minimum_required(VERSION 3.24)
project(AutoTextInput LANGUAGES CXX)

# C++20 for std::jthread / std::stop_token (TypingEngine) and std::numbers (SoundSynth).
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# AUTOMOC runs Qt's Meta-Object Compiler on every header with Q_OBJECT. It generates the code behind signals,
# slots and Q_ENUM, so no manual moc step is needed.
set(CMAKE_AUTOMOC ON)

# Qt 6.4 is the oldest supported version (it's what Ubuntu 24.04 ships). Newer is fine.
find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets)

# --- Platform-independent sources ---
add_executable(AutoTextInput
    main.cpp
    AppSettings.cpp AppSettings.h
    GlobalHotkeyManager.h
    KeyboardSimulator.h
    MainWindow.cpp MainWindow.h
    Platform.h
    SoundSynth.cpp SoundSynth.h
    Sounds.h
    TrayController.cpp TrayController.h
    TypingEngine.cpp TypingEngine.h
)

# Lets the platform files under platform/<os>/ write #include "KeyboardSimulator.h" instead of "../../...".
target_include_directories(AutoTextInput PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(AutoTextInput PRIVATE Qt6::Widgets)

if(WIN32)
    # =================================================================================================================
    # Windows
    # =================================================================================================================
    target_sources(AutoTextInput PRIVATE
        platform/windows/GlobalHotkeyManager_win.cpp
        platform/windows/KeyboardSimulator_win.cpp
        platform/windows/Platform_win.cpp
        platform/windows/Sounds_win.cpp
    )

    # NOMINMAX stops <Windows.h> from defining min/max macros that break std::min/std::max.
    # WIN32_LEAN_AND_MEAN leaves rarely used APIs out of <Windows.h>, so it compiles faster.
    target_compile_definitions(AutoTextInput PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)

    # user32: SendInput/RegisterHotKey. winmm: timeBeginPeriod (precise keystroke timing) and PlaySound (audio cues).
    target_link_libraries(AutoTextInput PRIVATE user32 winmm)

    # Tray application: build as a GUI-subsystem exe so no console window opens alongside it.
    # On MinGW, -mwindows is used instead of WIN32_EXECUTABLE. That property links Qt's EntryPoint library,
    # which is built against Qt's bundled MinGW runtime and fails to link (undefined __imp___argc) with a
    # different MinGW toolchain. MinGW-w64's startup code calls main() for GUI-subsystem exes anyway.
    if(MINGW)
        target_link_options(AutoTextInput PRIVATE -mwindows)
    else()
        set_target_properties(AutoTextInput PROPERTIES WIN32_EXECUTABLE ON)
    endif()

    # --- Post-build: automatically deploy Qt DLLs and the matching MinGW runtime DLLs ---

    # Find windeployqt next to the Qt installation CMake already located.
    get_target_property(_qt6_qmake_location Qt6::qmake IMPORTED_LOCATION)
    get_filename_component(_qt_bin_dir "${_qt6_qmake_location}" DIRECTORY)
    find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS "${_qt_bin_dir}")

    if(WINDEPLOYQT_EXECUTABLE)
        add_custom_command(TARGET AutoTextInput POST_BUILD
            COMMAND "${WINDEPLOYQT_EXECUTABLE}" "$<TARGET_FILE:AutoTextInput>"
            COMMENT "Running windeployqt to copy required Qt DLLs next to the exe"
        )
    else()
        message(WARNING "windeployqt not found - Qt DLLs will not be copied automatically")
    endif()

    # Find the MinGW bin folder belonging to the SAME compiler that builds this target.
    # This matters because windeployqt copies runtime DLLs from Qt's own bundled MinGW,
    # which can be a different version than the one actually compiling this project -
    # causing STATUS_ENTRYPOINT_NOT_FOUND crashes at runtime. Copying from the real
    # compiler's folder afterward guarantees they match.
    if(MINGW)
        get_filename_component(MINGW_BIN_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)

        foreach(_dll libstdc++-6.dll libgcc_s_seh-1.dll libwinpthread-1.dll)
            add_custom_command(TARGET AutoTextInput POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${MINGW_BIN_DIR}/${_dll}"
                    "$<TARGET_FILE_DIR:AutoTextInput>"
                COMMENT "Copying matching MinGW runtime DLL: ${_dll}"
            )
        endforeach()
    endif()

elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # =================================================================================================================
    # Linux (X11)
    # =================================================================================================================
    target_sources(AutoTextInput PRIVATE
        platform/linux/GlobalHotkeyManager_x11.cpp
        platform/linux/KeyboardSimulator_x11.cpp
        platform/linux/Platform_linux.cpp
        platform/linux/Sounds_linux.cpp
    )

    # X11: Xlib (keymap lookups), XTest (simulated typing), xcb (global hotkey grabs on Qt's connection).
    # Debian/Ubuntu packages: libx11-dev libxtst-dev (libxcb1-dev comes in with libx11-dev).
    find_package(X11 REQUIRED)
    if(NOT X11_XTest_FOUND)
        message(FATAL_ERROR "The XTest library was not found. Install it (Debian/Ubuntu: libxtst-dev, Fedora: libXtst-devel).")
    endif()
    if(NOT X11_xcb_FOUND)
        message(FATAL_ERROR "libxcb was not found. Install it (Debian/Ubuntu: libxcb1-dev, Fedora: libxcb-devel).")
    endif()
    target_link_libraries(AutoTextInput PRIVATE X11::X11 X11::Xtst X11::xcb)

    # Audio cues use Qt Multimedia. It's optional: without it the app builds and runs, just silently.
    find_package(Qt6 QUIET COMPONENTS Multimedia)
    if(Qt6Multimedia_FOUND)
        target_link_libraries(AutoTextInput PRIVATE Qt6::Multimedia)
        target_compile_definitions(AutoTextInput PRIVATE AUTOTEXTINPUT_HAVE_QT_MULTIMEDIA)
    else()
        message(STATUS "Qt6 Multimedia not found - building without start/stop sounds "
                       "(Debian/Ubuntu: qt6-multimedia-dev to enable them).")
    endif()

    # `cmake --install build` copies the binary to <prefix>/bin (default prefix: /usr/local).
    include(GNUInstallDirs)
    install(TARGETS AutoTextInput RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")

else()
    message(FATAL_ERROR "AutoTextInput supports Windows and Linux (X11) only.")
endif()
