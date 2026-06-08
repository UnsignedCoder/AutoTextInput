# AutoTextInput

A simple C++ application for Windows that automates typing text into any active window. This tool is designed to take text input and simulate human typing, which can be useful for data entry, testing, or avoiding copy-paste detection.

## Features

-   Types text from the console into another application window.
-   Simulates human-like typing at a configurable speed.
-   Automatically switches to the last active window to begin typing.

## How to Use

1.  Run the `AutoTextInput.exe` executable.
2.  A console window will appear asking you to "Paste words to type here:".
3.  Paste the text you want to automate and press Enter.
4.  The application will automatically switch to your last active window and begin typing the text.
5.  To type another block of text, paste the new text into the console and press Enter again.
6.  When you are finished, simply press Enter on an empty line in the console to exit.

## Building from Source

This is a CMake project and can be built using any C++ compiler that supports C++17 and the Windows API.

1.  Clone the repository.
2.  Make sure you have a C++ compiler (like MSVC) and CMake installed.
3.  Generate the build files using CMake: `cmake -B build`
4.  Build the project: `cmake --build build`
5.  The executable will be located in the `build/` directory.

## Future Improvements

-   [ ] Fix the issue where the program does not exit correctly after the last paragraph is sent.
-   [ ] Refactor the code to use a dedicated typing thread, allowing the main thread to handle a future UI and a keyboard listener.
-   [ ] Implement a global hotkey to capture clipboard content and start typing immediately, removing the need for `Alt+Tab`.
-   [ ] Create a graphical user interface (GUI) for easier interaction.
-   [ ] Allow the program to run in the system tray for background access.

