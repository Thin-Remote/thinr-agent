#pragma once

#ifndef THINREMOTE_ARGUMENT_PARSER_HPP
#define THINREMOTE_ARGUMENT_PARSER_HPP

#include <string>
#include <memory>
#include <boost/program_options.hpp>

namespace thinr::cli {

struct InstallOptions {
    std::string token;
    std::string device_id;
    std::string host = "backend.thinger.io";
    bool overwrite = false;
    bool no_start = false;
};

struct ParseResult {
    enum class Command {
        NONE,           // No command (interactive setup or agent mode)
        INSTALL,        // Fast-track installation
        UNINSTALL,      // Uninstall service
        CMD_STATUS,     // Show status (renamed to avoid macro collision)
        TEST,           // Test connection
        RECONFIGURE,    // Reconfigure
        TEST_MENU,      // Test interactive menu (dev command)
        HELP,           // Show help
        UNKNOWN         // Unknown command
    };
    
    Command command = Command::NONE;
    std::string command_str;
    std::string config_path;
    int verbosity_level = 1;
    
    // Command-specific options
    InstallOptions install_options;
    
    // Status
    bool success = true;
    std::string error_message;
};

class ArgumentParser {
public:
    ArgumentParser();
    
    ParseResult parse(int argc, char* argv[]);
    void show_help(const std::string& command = "") const;
    
private:
    void show_general_help() const;
    void show_install_help() const;
    
    ParseResult::Command string_to_command(const std::string& cmd) const;
    
    static constexpr const char* VERSION = "1.0.0";
};

} // namespace thinr::cli

#endif // THINREMOTE_ARGUMENT_PARSER_HPP