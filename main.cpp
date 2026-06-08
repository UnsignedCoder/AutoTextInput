// TODO: fix program not exiting after last paragrapgh is sent
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

/**
 * @brief Simulates typing of a given string of characters.
 * @param inputWords The wide string to be typed out.
 * @param typeSpeed The desired typing speed in words per minute (WPM).
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
 * @brief Main entry point for the auto-typing application.
 * @return Returns 0 on successful completion.
 * @details This program reads lines of text from standard input and simulates typing them into another window.
 * It is designed to automate the process of transferring text from one source to another by simulating keyboard input.
 * @note The program currently requires the user to paste text into the console window. It will then switch to the most recent window to type out the pasted content.
 * @warning This application is dependent on the Windows API for input simulation and will not compile or run on other operating systems.
 */
int main () {
    std::wstring CurrentInputLine;
    bool activeWindow = false;
    const unsigned int typingSpeed = 250; // Default typing speed in WPM

    std::cout << "Paste words to type here: " << "\n";

    // Loop to read lines from the console until an empty line is entered.
    while (std::getline(std::wcin, CurrentInputLine) && !CurrentInputLine.empty()) {
        CurrentInputLine += L"\n"; // Append the current line and a newline character

        // On the first line of input, switch to the target window.
        if (!activeWindow) {
            SwitchToNextActiveWindow();
            activeWindow = true;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(500)); // Wait for the window to switch

        TypeInput(CurrentInputLine, typingSpeed);
        CurrentInputLine.clear();
    }

    std::cout << "Finished typing. Press Enter to exit." << std::endl;
    std::cin.get();
}