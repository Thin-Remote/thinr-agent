#include "argument_parser.hpp"
#include <algorithm>
#include <iostream>
#include <vector>

namespace thinr::cli {

argument_parser::argument_parser() = default;

ParseResult argument_parser::parse(int argc, char* argv[]) {
    namespace po = boost::program_options;
    ParseResult result;

    try {
        // Pre-process arguments: expand -vv, -vvv into verbosity level
        std::vector<std::string> args;
        int verbose_count = 0;
        for (int i = 1; i < argc; ++i) {
            std::string arg(argv[i]);
            // Match -v, -vv, -vvv, etc.
            if (arg.size() >= 2 && arg[0] == '-' && arg[1] != '-' &&
                arg.find_first_not_of('v', 1) == std::string::npos) {
                verbose_count += static_cast<int>(arg.size() - 1);
            } else {
                args.push_back(arg);
            }
        }

        // First, parse global options and command
        po::options_description global_desc("Global options");
        global_desc.add_options()
            ("help,h", "show this help message")
            ("version", "show version information")
            ("config,c", po::value<std::string>(&result.config_path), "path to configuration file")
            ("command", po::value<std::string>(&result.command_str), "command to execute");

        po::positional_options_description pos_desc;
        pos_desc.add("command", 1);

        // Parse global options and command
        po::variables_map vm;
        po::parsed_options parsed = po::command_line_parser(args)
            .options(global_desc)
            .positional(pos_desc)
            .allow_unregistered() // Important: allows command-specific options
            .run();

        po::store(parsed, vm);
        po::notify(vm);

        // Set verbosity: -v = info(3), -vv = debug(4)
        if (verbose_count > 0) {
            result.verbosity_level = std::min(2 + verbose_count, 4);
        }
        
        // Check for help at global level
        if (vm.count("help")) {
            result.command = ParseResult::Command::HELP;
            return result;
        }
        
        // Check for version flag
        if (vm.count("version")) {
            result.command = ParseResult::Command::VERSION;
            return result;
        }
        
        // Convert command string to enum
        result.command = string_to_command(result.command_str);
        
        // Handle command-specific parsing
        if (result.command == ParseResult::Command::INSTALL) {
            po::options_description install_desc("Install options");
            install_desc.add_options()
                ("help,h", "show help for install command")
                ("token", po::value<std::string>(&result.install_options.token), "auto-provision token (skips interactive auth)")
                ("device", po::value<std::string>(&result.install_options.device_id), "custom device identifier (default: hostname)")
                ("host", po::value<std::string>(&result.install_options.host)->default_value("backend.thinger.io"), "Thinger.io server host")
                ("overwrite", po::bool_switch(&result.install_options.overwrite), "auto-overwrite existing device (no conflict prompt)")
                ("no-start", po::bool_switch(&result.install_options.no_start), "install but don't start service immediately")
                ("no-verify-ssl", po::bool_switch(&result.install_options.no_verify_ssl), "disable SSL certificate verification");
            
            // Get unrecognized options from first parse
            std::vector<std::string> remaining = po::collect_unrecognized(parsed.options, po::include_positional);
            remaining.erase(remaining.begin()); // Remove the command itself
            
            // Parse install-specific options
            po::variables_map install_vm;
            po::store(po::command_line_parser(remaining)
                .options(install_desc)
                .run(), install_vm);
            po::notify(install_vm);
            
            // Check for install-specific help
            if (install_vm.count("help")) {
                result.command = ParseResult::Command::HELP;
                result.command_str = "install";
                return result;
            }
        }
        
    } catch (const po::error& e) {
        result.success = false;
        result.error_message = "Error parsing arguments: " + std::string(e.what());
        result.command = ParseResult::Command::HELP;
    }
    
    return result;
}

void argument_parser::show_help(const std::string& command) const {
    if (command.empty()) {
        show_general_help();
    } else if (command == "install") {
        show_install_help();
    } else {
        std::cout << "Unknown command: " << command << "\n\n";
        show_general_help();
    }
}

void argument_parser::show_general_help() const {
    std::cout << "ThinRemote " << AGENT_VERSION << " - Remote Access Client\n";
    std::cout << "Usage: thinr-agent [command] [options]\n\n";
    std::cout << "Commands:\n";
    std::cout << "  (no command)     Interactive setup or agent mode\n";
    std::cout << "  install          Fast-track installation as system service\n";
    std::cout << "  uninstall        Uninstall service and remove configuration\n";
    std::cout << "  status           Show connection status\n";
    std::cout << "  reconfigure      Restart interactive configuration\n";
    std::cout << "  test             Test connection with current config\n\n";
    std::cout << "General options:\n";
    std::cout << "  -h, --help       Show help message\n";
    std::cout << "  -v               Increase verbosity (-v=info, -vv=debug)\n";
    std::cout << "  -c, --config     Path to configuration file\n\n";
    std::cout << "For command-specific help, use: thinr-agent <command> --help\n";
}

void argument_parser::show_install_help() const {
    std::cout << "ThinRemote " << AGENT_VERSION << " - Install Command\n";
    std::cout << "Usage: thinr-agent install [options]\n\n";
    std::cout << "Fast-track installation that skips testing and installs the service immediately.\n\n";
    std::cout << "Options:\n";
    std::cout << "  --token TOKEN    Auto-provision token (skips interactive auth)\n";
    std::cout << "  --device ID      Custom device identifier (default: hostname)\n";
    std::cout << "  --host HOST      Thinger.io server (default: backend.thinger.io)\n";
    std::cout << "  --overwrite      Auto-overwrite existing device\n";
    std::cout << "  --no-start       Install but don't start service\n";
    std::cout << "  --no-verify-ssl  Disable SSL certificate verification\n";
    std::cout << "  -h, --help       Show this help message\n\n";
    std::cout << "Examples:\n";
    std::cout << "  thinr-agent install                           # Interactive fast install\n";
    std::cout << "  thinr-agent install --token abc123            # Auto-provision with token\n";
    std::cout << "  thinr-agent install --token abc123 --device server-01 --overwrite\n";
    std::cout << "  thinr-agent install --token abc123 --no-verify-ssl  # Self-signed certificate\n";
}

ParseResult::Command argument_parser::string_to_command(const std::string& cmd) const {
    if (cmd.empty()) return ParseResult::Command::NONE;
    if (cmd == "install") return ParseResult::Command::INSTALL;
    if (cmd == "uninstall") return ParseResult::Command::UNINSTALL;
    if (cmd == "status") return ParseResult::Command::CMD_STATUS;
    if (cmd == "test") return ParseResult::Command::TEST;
    if (cmd == "reconfigure") return ParseResult::Command::RECONFIGURE;
    return ParseResult::Command::UNKNOWN;
}

} // namespace thinr::cli