#pragma once

#ifndef THINREMOTE_CONSOLE_HPP
#define THINREMOTE_CONSOLE_HPP

#include <string>
#include <iostream>
#include <vector>

namespace thinr::utils {

class Console {
public:
    // ANSI Color codes
    static constexpr const char* RESET = "\033[0m";
    static constexpr const char* BOLD = "\033[1m";
    static constexpr const char* DIM = "\033[2m";
    static constexpr const char* UNDERLINE = "\033[4m";
    
    // Colors
    static constexpr const char* RED = "\033[31m";
    static constexpr const char* GREEN = "\033[32m";
    static constexpr const char* YELLOW = "\033[33m";
    static constexpr const char* BLUE = "\033[34m";
    static constexpr const char* MAGENTA = "\033[35m";
    static constexpr const char* CYAN = "\033[36m";
    static constexpr const char* WHITE = "\033[37m";
    static constexpr const char* GRAY = "\033[90m";
    
    // Background colors
    static constexpr const char* BG_RED = "\033[41m";
    static constexpr const char* BG_GREEN = "\033[42m";
    static constexpr const char* BG_YELLOW = "\033[43m";
    static constexpr const char* BG_BLUE = "\033[44m";
    static constexpr const char* BG_MAGENTA = "\033[45m";
    static constexpr const char* BG_CYAN = "\033[46m";
    static constexpr const char* BG_WHITE = "\033[47m";
    
    // Bright colors
    static constexpr const char* BRIGHT_RED = "\033[91m";
    static constexpr const char* BRIGHT_GREEN = "\033[92m";
    static constexpr const char* BRIGHT_YELLOW = "\033[93m";
    static constexpr const char* BRIGHT_BLUE = "\033[94m";
    static constexpr const char* BRIGHT_MAGENTA = "\033[95m";
    static constexpr const char* BRIGHT_CYAN = "\033[96m";
    static constexpr const char* BRIGHT_WHITE = "\033[97m";
    
    // Helper methods
    static std::string red(const std::string& text) { return RED + text + RESET; }
    static std::string green(const std::string& text) { return GREEN + text + RESET; }
    static std::string yellow(const std::string& text) { return YELLOW + text + RESET; }
    static std::string blue(const std::string& text) { return BLUE + text + RESET; }
    static std::string magenta(const std::string& text) { return MAGENTA + text + RESET; }
    static std::string cyan(const std::string& text) { return CYAN + text + RESET; }
    static std::string gray(const std::string& text) { return GRAY + text + RESET; }
    static std::string bold(const std::string& text) { return BOLD + text + RESET; }
    static std::string dim(const std::string& text) { return DIM + text + RESET; }
    static std::string underline(const std::string& text) { return UNDERLINE + text + RESET; }
    
    // Bright colors
    static std::string brightRed(const std::string& text) { return BRIGHT_RED + text + RESET; }
    static std::string brightGreen(const std::string& text) { return BRIGHT_GREEN + text + RESET; }
    static std::string brightYellow(const std::string& text) { return BRIGHT_YELLOW + text + RESET; }
    static std::string brightBlue(const std::string& text) { return BRIGHT_BLUE + text + RESET; }
    static std::string brightMagenta(const std::string& text) { return BRIGHT_MAGENTA + text + RESET; }
    static std::string brightCyan(const std::string& text) { return BRIGHT_CYAN + text + RESET; }
    
    // Status indicators
    static std::string success(const std::string& text, bool indent=true) { return (indent ? "  " : "") + brightGreen("✓") + " " + text; }
    static std::string error(const std::string& text, bool indent=true) { return (indent ? "  " : "")  + brightRed("✗") + " " + text; }
    static std::string warning(const std::string& text, bool indent=true) { return (indent ? "  " : "")  + brightYellow("⚠") + " " + text; }
    static std::string info(const std::string& text, bool indent=true) { return (indent ? "  " : "")  + brightBlue("ℹ") + " " + text; }
    static std::string loading(const std::string& text) { return brightCyan("⟳") + " " + text; }
    static std::string userPrompt() { return brightCyan("❯ "); }
    
    // Box drawing
    static void printBanner(const std::string& title, const std::string& subtitle = "");
    static void printMenu(const std::string& title, const std::vector<std::string>& options);
    static void printSectionHeader(const std::string& title, const std::string& emoji = "", const std::string& color = BRIGHT_BLUE);
    
    // Clear and positioning
    static void clear();
    static void newLine(int count = 1);
    
    // Hyperlink support
    static std::string hyperlink(const std::string& url, const std::string& text = "");
    static bool supportsHyperlinks();
    
    // Interactive menu (experimental)
    static int selectOption(const std::string& title, const std::vector<std::string>& options, int defaultSelected = 0);
    
    // Interactive confirmation with fallback
    static bool confirm(const std::string& prompt, bool default_yes = true);
    
    // Generic menu choice handler with fallback
    static int getUserChoice(const std::vector<std::string>& options, const std::string& prompt);
    
    // Clear input buffer
    static void clearInputBuffer();
};

} // namespace thinr::utils

#endif // THINREMOTE_CONSOLE_HPP