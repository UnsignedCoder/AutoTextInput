// TODO: fix program not exiting after last paragraph is sent
// TODO: refactor the code to use it's own typing thread so that the main tread can handle the UI and a keyboard listening thread listening for the shortcut that'll terminate the typing thread mid typing
// TODO: and a capture clipboard shortcut so i can use a global shortcut to immediately capture and execute auto typing hence letting me delete the alt tab to switch to the next tab
// TODO: make the UI
// TODO: look into launching the program in background like the system tray so it doesnt have to be open from start and also when you click on the tray icon it brings up the gui if you mant to manually paste in text or set the typing speed

#include <chrono>
#include <iostream>
#include <iterator>
#include <string>
#include <thread>
#include <Windows.h>
#include <QApplication>
#include <QWidget>

/**
 * @brief Simulates typing of a given string of characters.
 * @param inputWords The wide string to be typed out.
 * @param typeSpeed The desired typing speed in words per minute (WPM).
 * @pre typeSpeed must be greater than 0 - it is used as a divisor below, so 0 will trigger a division-by-zero.
 * @note This function calculates the delay between keystrokes based on a standard of 5 characters per word.
 * @warning This function relies on the Windows-specific `SendInput` function and is not portable to other operating systems.
 */
void TypeInput( const std::wstring& inputWords, const unsigned int typeSpeed) {
    INPUT input[2] = {};
    ZeroMemory(input, sizeof(input));

    // Calculate typing speed by converting words per minute to a delay in milliseconds per character.
    // Assumes an average word length of 5 characters.
    const unsigned int typingSpeed = static_cast<unsigned int>(60'000/(typeSpeed * 5));

    for (const wchar_t character : inputWords) {
        // Handle newline characters by simulating a press of the ENTER key.
        if (character == L'\n') {
            input[0].type = INPUT_KEYBOARD;
            input[0].ki.wVk = VK_RETURN;

            input[1].type = INPUT_KEYBOARD;
            input[1].ki.wVk = VK_RETURN;
            input[1].ki.dwFlags = KEYEVENTF_KEYUP;
        } else {
            // For all other characters, send a Unicode keyboard event.
            input[0].type = INPUT_KEYBOARD;
            input[0].ki.wScan = character;
            input[0].ki.dwFlags = KEYEVENTF_UNICODE; // Use Unicode character

            input[1].type = INPUT_KEYBOARD;
            input[1].ki.wScan = character;
            input[1].ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP; // Use Unicode character for key up
        }

        // cbSize (final arg) is the size of a single INPUT struct, not the size of the whole array -
        // this is correct as-is per the SendInput API contract, not a bug.
        const unsigned int sent = SendInput(std::size(input), input, sizeof(INPUT));
        if (sent != std::size(input)) {
            printf("SendInput failed: 0x%x\n", HRESULT_FROM_WIN32(GetLastError()));
        }

        // Pause between each character to simulate human typing speed.
        std::this_thread::sleep_for(std::chrono::milliseconds(typingSpeed));
    }
}

/**
 * @brief Switches focus to the next active window by simulating an 'Alt+Tab' key press.
 * @see TypeInput
 * @remark This is used to switch from the console window to the target application where text will be typed.
 * @warning This function's behavior can be unpredictable depending on the windowing environment and what windows are currently open. It assumes the desired window is the next one in the Alt+Tab list.
 */
void SwitchToNextActiveWindow()
{
    printf("Sending 'Alt + TAB'\r\n");
    INPUT inputs[4] = {};
    ZeroMemory(inputs, sizeof(inputs));

    // Press Alt
    inputs[0].type = INPUT_KEYBOARD;
    inputs[0].ki.wVk = VK_LMENU;

    // Press Tab
    inputs[1].type = INPUT_KEYBOARD;
    inputs[1].ki.wVk = VK_TAB;

    // Release Tab
    inputs[2].type = INPUT_KEYBOARD;
    inputs[2].ki.wVk = VK_TAB;
    inputs[2].ki.dwFlags = KEYEVENTF_KEYUP;

    // Release Alt
    inputs[3].type = INPUT_KEYBOARD;
    inputs[3].ki.wVk = VK_LMENU;
    inputs[3].ki.dwFlags = KEYEVENTF_KEYUP;

    const unsigned int uSent = SendInput(std::size(inputs), inputs, sizeof(INPUT));
    if (uSent != std::size(inputs))
    {
        printf("SendInput failed: 0x%x\n", HRESULT_FROM_WIN32(GetLastError()));
    } else {
        std::cout << "Sent 'Alt + TAB' successfully\n";
    }
}

/**
 * @brief Reads lines of text from the console and types each one into the previously active window.
 * @param CurrentInputLine Working buffer for the line currently being read; cleared and reused each iteration.
 * @param activeWindow Whether focus has already been switched to the target window this session.
 * @param typingSpeed Typing speed in words per minute, forwarded to TypeInput.
 * @note This is the original console-driven workflow. main() currently launches the Qt UI skeleton
 *       instead of calling this function, so it is presently unused - kept here pending the planned
 *       typing-thread refactor described in the TODOs above.
 * @see TypeInput
 * @see SwitchToNextActiveWindow
 */
void ExecuteTyping ( std::wstring CurrentInputLine, bool activeWindow, const unsigned int typingSpeed ) {
    std::cout << "Paste words to type here: " << "\n";

    // Loop to read lines from the console until an empty line is entered.
    while (std::getline(std::wcin, CurrentInputLine) && !CurrentInputLine.empty()) {
        CurrentInputLine += L"\n"; // Append the current line and a newline character

        // On the first line of input, switch to the target window.
        if (!activeWindow) {
            SwitchToNextActiveWindow();
            activeWindow = true;
        }
        // Wait for the window switch (above) to actually complete before typing begins -
        // without this delay, keystrokes can arrive before the target window has focus.
        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        TypeInput(CurrentInputLine, typingSpeed);
        CurrentInputLine.clear();
    }

    std::cout << "Finished typing. Press Enter to exit." << std::endl;
}

/**
 * @brief Application entry point.
 * @return The Qt event loop's exit code, forwarded from QApplication::exec().
 * @details Currently boots a minimal, empty Qt window as a smoke test to confirm the build
 * toolchain (compiler, CMake, and Qt linking) is wired together correctly. The original
 * console-based typing workflow (see ExecuteTyping, above) is temporarily disabled below
 * while the real Qt UI is being built out.
 * @note This doc comment previously described the console-input workflow as the active behavior;
 *       corrected here since main() no longer calls ExecuteTyping.
 * @warning This application depends on both the Windows API (input simulation) and Qt, and is not portable as-is.
 */
int main (int argc, char* argv[]) {
    // Original console-driven entry point, temporarily disabled while the Qt UI replaces it.
    // std::wstring CurrentInputLine;
    // bool activeWindow = false;
    // const unsigned int typingSpeed = 250; // Default typing speed in WPM
    //
    // ExecuteTyping(CurrentInputLine, activeWindow, typingSpeed);

    // Toolchain smoke test: an empty window with no content yet - proves QApplication,
    // CMake's Qt linkage, and the deployed runtime DLLs all work end-to-end before any
    // real UI (tray icon, settings, typing speed control) is built on top of it.
    QApplication App (argc, argv);
    QWidget Window;
    Window.resize(400, 300);
    Window.setWindowTitle("AutoTextInput");
    Window.show();
    return App.exec();
}