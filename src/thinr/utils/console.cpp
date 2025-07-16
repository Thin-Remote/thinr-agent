#include "console.hpp"
#include <iostream>
#include <iomanip>
#include <limits>
#include <termios.h>
#include <unistd.h>
#include <cstdio>
#include <sys/select.h>
#include <chrono>
#include <csignal>
#include <cstdlib>

namespace thinr::utils {

// Global variables for signal handling
static struct termios* g_saved_termios = nullptr;
static struct sigaction g_old_sigint_handler;

// Signal handler to restore terminal on Ctrl+C
static void restore_terminal_handler(int sig) {
    if (g_saved_termios) {
        // Show cursor
        std::cout << "\033[?25h" << std::endl;
        // Restore terminal settings
        tcsetattr(STDIN_FILENO, TCSANOW, g_saved_termios);
        g_saved_termios = nullptr;
    }
    
    // Call original handler if it exists
    if (g_old_sigint_handler.sa_handler != SIG_DFL && 
        g_old_sigint_handler.sa_handler != SIG_IGN) {
        g_old_sigint_handler.sa_handler(sig);
    } else {
        // Default behavior: exit
        exit(128 + sig);
    }
}

void Console::printBanner(const std::string& title, const std::string& subtitle) {
    std::cout << "\n";

    // ThinRemote ASCII Art
    std::cout << "░▀█▀░█░█░▀█▀░█▀█░█▀▄░█▀▀░█▄█░█▀█░▀█▀░█▀▀" << "\n";
    std::cout << "░░█░░█▀█░░█░░█░█░█▀▄░█▀▀░█░█░█░█░░█░░█▀▀" << "\n";
    std::cout << "░░▀░░▀░▀░▀▀▀░▀░▀░▀░▀░▀▀▀░▀░▀░▀▀▀░░▀░░▀▀▀" << "\n";
    std::cout << "\n" << GRAY << "ThinRemote Monitor Agent v.1.0.0" << RESET << "\n\n";
}

void Console::printMenu(const std::string& title, const std::vector<std::string>& options) {
    // Title - only print if not empty
    if (!title.empty()) {
        std::cout << BRIGHT_WHITE << BOLD << title << RESET << "\n\n";
    }
    
    // Options
    for (size_t i = 0; i < options.size(); ++i) {
        std::cout << BRIGHT_CYAN << BOLD << (i + 1) << ")" << RESET 
                  << " " << options[i] << "\n";
    }
    
    std::cout << "\n";
}

void Console::printSectionHeader(const std::string& title, const std::string& emoji, const std::string& color) {
    std::string header_text = emoji.empty() ? title : emoji + " " + title;

    // Calculate visual length more accurately
    // Most emojis are 2 characters wide visually, regardless of their UTF-8 byte count
    int visual_length;
    if (emoji.empty()) {
        visual_length = title.length();
    } else {
        visual_length = 2 + 1 + title.length(); // emoji (2 visual) + space (1) + title
    }

    int content_width = std::max(visual_length + 6, 50); // Content width with padding

    // Blue background with white text and arrow end
    std::cout << BG_BLUE << BRIGHT_WHITE << " " << header_text;
    
    // Add padding spaces to fill the width
    int padding_needed = content_width - visual_length - 1; // -1 for the initial space
    for (int i = 0; i < padding_needed; ++i) {
        std::cout << " ";
    }
    
    // Gradient transition with background colors
    std::cout << "\033[44m " << "\033[100m " << "\033[0m " << RESET << "\n\n";
}

void Console::clear() {
    std::cout << "\033[2J\033[1;1H";
}

void Console::newLine(int count) {
    for (int i = 0; i < count; ++i) {
        std::cout << "\n";
    }
}

int Console::selectOption(const std::string& title, const std::vector<std::string>& options, int defaultSelected) {
    if (options.empty()) {
        return -1;
    }
    
    // Validate defaultSelected
    int selected = (defaultSelected >= 0 && defaultSelected < static_cast<int>(options.size())) 
                   ? defaultSelected : 0;
    
    // Check if we're in a proper terminal
    if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) {
        return -1; // Fallback to traditional menu
    }
    
    // Save current terminal settings
    struct termios original_termios;
    if (tcgetattr(STDIN_FILENO, &original_termios) != 0) {
        return -1; // Fallback if can't get terminal settings
    }
    
    // Set up signal handler to restore terminal on Ctrl+C
    g_saved_termios = &original_termios;
    struct sigaction new_handler;
    new_handler.sa_handler = restore_terminal_handler;
    sigemptyset(&new_handler.sa_mask);
    new_handler.sa_flags = 0;
    sigaction(SIGINT, &new_handler, &g_old_sigint_handler);
    
    // Set terminal to raw mode for arrow key detection
    struct termios raw_termios = original_termios;
    raw_termios.c_lflag &= ~(ICANON | ECHO);  // Disable canonical mode and echo
    raw_termios.c_cc[VMIN] = 1;   // Minimum characters to read
    raw_termios.c_cc[VTIME] = 0;  // No timeout
    
    if (tcsetattr(STDIN_FILENO, TCSANOW, &raw_termios) != 0) {
        // Restore signal handler
        sigaction(SIGINT, &g_old_sigint_handler, nullptr);
        g_saved_termios = nullptr;
        return -1; // Fallback if can't set raw mode
    }
    
    // Hide cursor
    std::cout << "\033[?25l";
    std::cout.flush();
    
    // Print title only if not empty
    if (!title.empty()) {
        std::cout << BRIGHT_WHITE << BOLD << title << RESET << "\n\n";
    }
    
    // Function to print menu options
    auto print_menu = [&]() {
        for (int i = 0; i < static_cast<int>(options.size()); ++i) {
            if (i == selected) {
                // Selected option: bright cyan with arrow
                std::cout << BRIGHT_CYAN << "❯ " << options[i] << RESET << "\n";
            } else {
                // Unselected option: dimmed
                std::cout << "  " << DIM << options[i] << RESET << "\n";
            }
        }
    };
    
    // Print initial menu
    print_menu();
    
    int result = -1;
    
    while (true) {
        char c;
        if (read(STDIN_FILENO, &c, 1) != 1) {
            continue; // Read error, try again
        }
        
        if (c == '\033') { // ESC sequence (arrow keys)
            char seq[2];
            if (read(STDIN_FILENO, &seq[0], 1) == 1 && seq[0] == '[') {
                if (read(STDIN_FILENO, &seq[1], 1) == 1) {
                    int old_selected = selected;
                    
                    switch (seq[1]) {
                        case 'A': // Up arrow
                            selected = (selected > 0) ? selected - 1 : static_cast<int>(options.size()) - 1;
                            break;
                        case 'B': // Down arrow
                            selected = (selected < static_cast<int>(options.size()) - 1) ? selected + 1 : 0;
                            break;
                        default:
                            continue; // Ignore other escape sequences
                    }
                    
                    if (old_selected != selected) {
                        // Move cursor up to redraw menu
                        std::cout << "\033[" << options.size() << "A";
                        print_menu();
                        std::cout.flush();
                    }
                }
            } else {
                // Plain ESC key - cancel
                result = -1;
                break;
            }
        } else if (c == '\r' || c == '\n') { // Enter key
            result = selected;
            break;
        } else if (c == 3) { // Ctrl+C
            result = -1;
            break;
        }
        // Ignore other characters
    }
    
    // Show cursor again
    std::cout << "\033[?25h";
    std::cout.flush();
    
    // Restore original terminal settings
    tcsetattr(STDIN_FILENO, TCSANOW, &original_termios);
    
    // Restore original signal handler
    sigaction(SIGINT, &g_old_sigint_handler, nullptr);
    g_saved_termios = nullptr;
    
    return result;
}

bool Console::confirm(const std::string& prompt, bool default_yes) {
    std::vector<std::string> options = {"Yes", "No"};
    
    // Use getUserChoice which handles both interactive and traditional menus
    // and automatically adds the newline
    int choice = getUserChoice(options, prompt);
    
    // getUserChoice returns 1-based index (1=Yes, 2=No)
    if (choice == 1) {
        return true;  // Yes
    } else if (choice == 2) {
        return false; // No
    }
    
    // Should not reach here, but return default if something goes wrong
    return default_yes;
}

int Console::getUserChoice(const std::vector<std::string>& options, const std::string& prompt) {
    int choice = 0;
    
    // Check if we're in an interactive terminal
    bool is_interactive = isatty(STDIN_FILENO) && isatty(STDOUT_FILENO);
    
    // Try interactive menu first, fallback to traditional if needed
    if (is_interactive) {
        int selected = selectOption(prompt, options, 0);
        
        if (selected >= 0 && selected < static_cast<int>(options.size())) {
            choice = selected + 1; // Convert to 1-based indexing
            std::cout << "\n"; // Add newline after interactive selection
        } else {
            // If interactive menu was cancelled, fall back to traditional
            std::cout << warning("Interactive menu cancelled, using traditional menu") << "\n\n";
            choice = 0; // Will trigger traditional menu below
        }
    }
    
    // Traditional fallback menu or if interactive menu was cancelled
    if (choice == 0) {
        printMenu(prompt, options);
        
        while (true) {
            std::cout << userPrompt() << "Option [1-" << options.size() << "]: ";
            std::string input;
            std::getline(std::cin, input);
            
            try {
                choice = std::stoi(input);
            } catch (...) {
                std::cout << error("Invalid input. Please enter a number.") << "\n";
                continue;
            }
            
            if (choice < 1 || choice > static_cast<int>(options.size())) {
                std::cout << error("Invalid option. Please select 1-") << options.size() << "." << "\n";
                continue;
            }
            
            std::cout << "\n"; // Add newline after traditional selection
            break; // Valid choice, exit the input loop
        }
    }
    
    return choice;
}

void Console::clearInputBuffer() {
    // Clear any failure flags
    std::cin.clear();
    
    // Sync stdio buffers
    std::cin.sync();
    
    // Clear input buffer manually if needed
    if (std::cin.rdbuf()->in_avail() > 0) {
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
    }
}

bool Console::supportsHyperlinks() {
    // Check if terminal supports hyperlinks
    const char* term = std::getenv("TERM_PROGRAM");
    if (term) {
        std::string term_str(term);
        // Known terminals that support OSC 8 hyperlinks
        if (term_str == "iTerm.app" || 
            term_str == "vscode" || 
            term_str == "Hyper" ||
            term_str == "WezTerm") {
            return true;
        }
    }
    
    // Check VTE-based terminals (GNOME Terminal, Tilix, etc.)
    const char* vte_version = std::getenv("VTE_VERSION");
    if (vte_version) {
        int version = std::atoi(vte_version);
        if (version >= 5000) { // VTE 0.50.0 and later support hyperlinks
            return true;
        }
    }
    
    // Check for specific terminal emulators
    const char* term_env = std::getenv("TERM");
    if (term_env) {
        std::string term_str(term_env);
        // Some terminals set specific TERM values
        if (term_str.find("kitty") != std::string::npos ||
            term_str.find("alacritty") != std::string::npos) {
            return true;
        }
    }
    
    // Conservative default: assume no support
    return false;
}

std::string Console::hyperlink(const std::string& url, const std::string& text) {
    // If terminal doesn't support hyperlinks, just return the URL or text
    if (!supportsHyperlinks()) {
        if (text.empty() || text == url) {
            return url;
        }
        return text + " (" + url + ")";
    }
    
    // OSC 8 hyperlink format
    return "\033]8;;" + url + "\033\\" + (text.empty() ? url : text) + "\033]8;;\033\\";
}

} // namespace thinr::utils