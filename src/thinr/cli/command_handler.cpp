#include "command_handler.hpp"
#include "../utils/console.hpp"
#include "../utils/ssl_config.hpp"
#include "../utils/device_id_generator.hpp"
#include "../installer/interactive_setup.hpp"
#include "../installer/service_installer.hpp"
#include "../extensions/updater.hpp"
#include "../agent/agent.hpp"
#include <iostream>
#include <unistd.h>
#include <termios.h>
#include <ctime>
#include <filesystem>
#include <algorithm>
#include <spdlog/spdlog.h>


namespace thinr::cli {

command_handler::command_handler() = default;

int command_handler::execute(const ParseResult& parse_result) {
    // Determine appropriate verbosity level based on mode
    int verbosity_level = parse_result.verbosity_level;
    
    // If verbosity wasn't explicitly set (default value), adjust based on mode
    if (verbosity_level == 1) { // Default value in ArgumentParser
        // For interactive commands, use quiet (0). For agent mode, use error level (1)
        bool is_interactive_mode = (parse_result.command == ParseResult::Command::INSTALL ||
                                   parse_result.command == ParseResult::Command::RECONFIGURE ||
                                   parse_result.command == ParseResult::Command::UNINSTALL);
        
        // For NONE command, check if we'll likely do interactive setup vs agent mode
        if (parse_result.command == ParseResult::Command::NONE) {
            // If no config exists, we'll likely do interactive setup
            if (!config_manager_.exists()) {
                is_interactive_mode = true;
            }
        }
        
        verbosity_level = is_interactive_mode ? 0 : 1;
    }
    
    setup_logging(verbosity_level);
    
    // Update config manager if custom path provided
    if (!parse_result.config_path.empty()) {
        config_manager_ = config::config_manager(parse_result.config_path);
    }
    
    // Handle parse errors
    if (!parse_result.success) {
        std::cerr << parse_result.error_message << "\n";
        argument_parser parser;
        parser.show_help();
        return 1;
    }

    // Configure SSL for commands that need HTTPS connectivity
    bool needs_ssl = false;
    switch (parse_result.command) {
        case ParseResult::Command::INSTALL:
        case ParseResult::Command::TEST:
        case ParseResult::Command::RECONFIGURE:
        case ParseResult::Command::UPDATE:
            needs_ssl = true;
            break;
        case ParseResult::Command::NONE:
            // Need SSL if config exists (agent mode) or if no config (setup mode)
            needs_ssl = true;  // Always need SSL for NONE command
            break;
        default:
            needs_ssl = false;
            break;
    }
    
    if (needs_ssl) {
        utils::SSLConfig::configure_ssl_certificates();
    }
    
    // Execute the appropriate command
    switch (parse_result.command) {
        case ParseResult::Command::HELP:
            {
                argument_parser parser;
                parser.show_help(parse_result.command_str);
                return 0;
            }
        case ParseResult::Command::INSTALL:
            return handle_install(parse_result.install_options);
            
        case ParseResult::Command::UNINSTALL:
            return handle_uninstall();
            
        case ParseResult::Command::CMD_STATUS:
            return handle_status();
            
        case ParseResult::Command::TEST:
            return handle_test();
            
        case ParseResult::Command::RECONFIGURE:
            return handle_reconfigure();

        case ParseResult::Command::UPDATE:
            return handle_update(parse_result.update_options);

        case ParseResult::Command::NONE:
            return handle_no_command(parse_result.config_path);
            
        case ParseResult::Command::VERSION:
            std::cout << "thinr-agent version " << AGENT_VERSION << "\n";
            return 0;
            
        case ParseResult::Command::UNKNOWN:
            std::cerr << "Unknown command: " << parse_result.command_str << "\n";
            argument_parser parser;
            parser.show_help();
            return 1;
    }
    
    return 0;
}

int command_handler::handle_install(const InstallOptions& options) {
    utils::Console::printSectionHeader("ThinRemote Fast Installation", "🚀");
    
    // Check if already configured
    if (config_manager_.exists()) {
        std::cout << utils::Console::warning("Configuration already exists.") << "\n";
        std::cout << "Use 'thinr-agent reconfigure' to change configuration.\n";
        std::cout << "Use 'thinr-agent' without arguments to manage the service.\n";
        return 1;
    }
    
    // Store verify_ssl preference for use in save_and_install_service
    verify_ssl_ = !options.no_verify_ssl;

    bool success;
    if (!options.token.empty()) {
        success = install_with_token(options);
    } else {
        success = install_interactive(options);
    }

    return success ? 0 : 1;
}

bool command_handler::install_with_token(const InstallOptions& options) {
    std::string final_device_id = determine_device_id(options.device_id);

    std::cout << utils::Console::cyan("Target server: ") << options.host << "\n\n";

    try {
        // Resolve product: use provided, auto-detect, or create default
        std::string product_id;
        try {
            auto jwt_payload = auth_manager_.decode_jwt_payload(options.token);
            std::string username = jwt_payload.value("usr", "");
            if (!username.empty()) {
                product_id = resolve_product(options.host, username, options.token, options.product);
            }
        } catch (const std::exception& e) {
            spdlog::debug("Could not resolve product: {}", e.what());
        }

        std::cout << utils::Console::loading("Auto-provisioning device with token...") << "\n";

        std::string device_name = determine_device_name(final_device_id);

        auto result = auth_manager_.auto_provision_with_device_id(options.token, final_device_id, device_name, product_id);

        if (result.success) {
            result.credentials.host = options.host;
            std::cout << utils::Console::success("Device provisioned successfully!") << "\n";
            return save_and_install_service(result.credentials, options.no_start);
        }

        if (result.status_code == 409 && options.overwrite) {
            auto jwt_payload = auth_manager_.decode_jwt_payload(options.token);
            std::string username = jwt_payload.value("usr", "");

            std::cout << utils::Console::warning("Device already exists, overwriting...") << "\n";
            auth_manager_.delete_device(options.host, username, final_device_id, options.token);

            auto retry = auth_manager_.auto_provision_with_device_id(options.token, final_device_id, device_name, product_id);
            if (retry.success) {
                retry.credentials.host = options.host;
                std::cout << utils::Console::success("Device overwritten successfully!") << "\n";
                return save_and_install_service(retry.credentials, options.no_start);
            }
            std::cout << utils::Console::error("Failed to overwrite device (HTTP " + std::to_string(retry.status_code) + ")") << "\n";
            return false;
        }

        if (result.status_code == 409) {
            std::cout << utils::Console::error("Device already exists: ") << final_device_id << "\n";
            std::cout << "Use --overwrite flag to replace the existing device.\n";
        } else {
            std::cout << utils::Console::error("Auto-provisioning failed (HTTP " + std::to_string(result.status_code) + ")") << "\n";
        }
        return false;

    } catch (const std::exception& e) {
        std::cout << utils::Console::error("Auto-provisioning failed: ") << e.what() << "\n";
        return false;
    }
}

bool command_handler::install_interactive(const InstallOptions& options) {
    if (!is_interactive_terminal()) {
        std::cout << utils::Console::error("Interactive authentication required but not in interactive terminal.") << "\n";
        std::cout << "Use --token flag for non-interactive installation.\n";
        return false;
    }
    
    std::cout << utils::Console::cyan("Interactive authentication required.") << "\n\n";
    
    // Ask for username and password directly
    std::cout << utils::Console::userPrompt() << "Thinger.io username: ";
    std::string username;
    std::getline(std::cin, username);
    
    if (username.empty()) {
        std::cout << utils::Console::error("Username cannot be empty") << "\n";
        return false;
    }
    
    std::string password = read_password_securely();
    std::string final_device_id = determine_device_id(options.device_id);
    std::string device_name = determine_device_name(final_device_id);

    std::cout << utils::Console::loading("Authenticating and provisioning device...") << "\n";

    // Get access token first
    auto auth_result = auth_manager_.oauth_password_flow(options.host, username, password);

    if (!auth_result.success) {
        using auth::AuthError;
        switch (auth_result.error) {
            case AuthError::invalid_credentials:
                std::cout << utils::Console::error("Invalid username or password") << "\n";
                break;
            case AuthError::access_denied:
                std::cout << utils::Console::error("Access denied - check your permissions") << "\n";
                break;
            case AuthError::not_found:
                std::cout << utils::Console::error("Host not found - check the host URL") << "\n";
                break;
            case AuthError::ssl_error:
                std::cout << utils::Console::error("SSL connection failed") << "\n";
                break;
            default:
                std::cout << utils::Console::error("Authentication failed: ") << auth_result.error_detail << "\n";
                break;
        }
        return false;
    }

    // Resolve product
    std::string product_id = resolve_product(options.host, username, auth_result.access_token, options.product);

    // Provision device with access token
    auto result = auth_manager_.provision_device(options.host, username, final_device_id, device_name, auth_result.access_token, product_id);

    if (result.success) {
        std::cout << utils::Console::success("Device provisioned successfully!") << "\n";
        return save_and_install_service(result.credentials, options.no_start);
    }

    if (result.status_code == 409 && options.overwrite) {
        std::cout << utils::Console::warning("Device already exists, overwriting...") << "\n";
        auth_manager_.delete_device(options.host, username, final_device_id, auth_result.access_token);
        auto retry = auth_manager_.provision_device(options.host, username, final_device_id, device_name, auth_result.access_token, product_id);
        if (retry.success) {
            std::cout << utils::Console::success("Device overwritten successfully!") << "\n";
            return save_and_install_service(retry.credentials, options.no_start);
        }
        std::cout << utils::Console::error("Failed to overwrite device") << "\n";
        return false;
    }

    if (result.status_code == 409) {
        std::cout << utils::Console::error("Device already exists: ") << final_device_id << "\n";
        std::cout << "Use --overwrite flag to replace the existing device.\n";
    } else {
        std::cout << utils::Console::error("Provisioning failed (HTTP " + std::to_string(result.status_code) + ")") << "\n";
    }
    return false;
}

bool command_handler::save_and_install_service(const config::DeviceCredentials& credentials, bool no_start) {
    // Apply verify_ssl preference
    auto final_credentials = credentials;
    final_credentials.verify_ssl = verify_ssl_;

    // Save configuration
    std::cout << utils::Console::loading("Saving configuration...") << "\n";
    config_manager_.save(final_credentials);
    std::string config_path = config_manager_.get_config_path();
    std::cout << utils::Console::success("Configuration saved: " + config_path) << "\n";
    
    // Install service
    std::cout << utils::Console::loading("Installing service...") << "\n";
    installer::service_installer service_installer;
    
    if (!service_installer.install_service()) {
        std::cout << utils::Console::error("Failed to install service") << "\n";
        std::cout << "Configuration has been saved. You can run 'thinr-agent' manually.\n";
        return false;
    }
    
    // Start service unless --no-start flag is used
    if (!no_start) {
        std::cout << utils::Console::loading("Starting service...") << "\n";
        if (service_installer.start_service()) {
            std::cout << utils::Console::success("Service started successfully") << "\n";
            std::cout << "Use 'thinr-agent' to manage the service.\n";
        } else {
            std::cout << utils::Console::warning("Could not start service automatically") << "\n";
            std::cout << "You can start it manually with your system's service manager.\n";
        }
    } else {
        std::cout << utils::Console::cyan("Service installed but not started (--no-start flag used).") << "\n";
        std::cout << "Start it manually when ready.\n";
    }
    
    return true;
}

std::string command_handler::determine_device_id(const std::string& provided_device_id) {
    if (!provided_device_id.empty()) {
        return provided_device_id;
    }

    // Generate device ID using hostname prefix + MAC address
    std::string generated_id = utils::DeviceIdGenerator::generate();
    std::cout << utils::Console::cyan("Generated device ID: ") << generated_id << "\n";
    spdlog::debug("Device ID generated from hostname prefix and MAC address");

    return generated_id;
}

std::string command_handler::determine_device_name(const std::string& fallback) {
    char hostname[256];
    std::string name = (gethostname(hostname, sizeof(hostname)) == 0) ? hostname : fallback;

    // Remove .local suffix if present
    const std::string local_suffix = ".local";
    if (name.length() > local_suffix.length() &&
        name.substr(name.length() - local_suffix.length()) == local_suffix) {
        name = name.substr(0, name.length() - local_suffix.length());
    }

    std::transform(name.begin(), name.end(), name.begin(), ::tolower);
    return name;
}

std::string command_handler::resolve_product(const std::string& host, const std::string& username,
                                             const std::string& access_token, const std::string& provided_product) {
    auto list_result = auth_manager_.list_products(host, username, access_token);

    if (!list_result.success) {
        spdlog::debug("Could not list products (HTTP {}), skipping product association", list_result.status_code);
        return provided_product;
    }

    if (!provided_product.empty()) {
        if (list_result.products.is_array()) {
            for (const auto& p : list_result.products) {
                if (p.value("product", "") == provided_product) {
                    std::cout << utils::Console::success("Using product: ") << p.value("name", provided_product) << "\n";
                    return provided_product;
                }
            }
        }

        std::cout << utils::Console::loading("Creating product '" + provided_product + "'...") << "\n";

        auto product_data = auth::auth_manager::build_default_product(provided_product, provided_product);
        auto create_result = auth_manager_.create_product(host, username, access_token, product_data);

        if (create_result.success) {
            std::cout << utils::Console::success("Product '" + provided_product + "' created") << "\n";
            return provided_product;
        }

        spdlog::debug("Failed to create product '{}': {} {}", provided_product, create_result.status_code, create_result.error_detail);
        return provided_product;
    }

    if (list_result.products.is_array()) {
        for (const auto& p : list_result.products) {
            std::string type;
            if (p.contains("config") && p["config"].contains("type")) {
                type = p["config"]["type"].get<std::string>();
            }
            if (type == "thinremote") {
                std::string id = p.value("product", "");
                if (!id.empty()) {
                    std::cout << utils::Console::success("Using product: ") << p.value("name", id) << "\n";
                    return id;
                }
            }
        }
    }

    std::cout << utils::Console::loading("Creating default ThinRemote product...") << "\n";

    auto product_data = auth::auth_manager::build_default_product("thinremote", "ThinRemote");
    auto create_result = auth_manager_.create_product(host, username, access_token, product_data);

    if (create_result.success) {
        std::cout << utils::Console::success("Product 'ThinRemote' created") << "\n";
        return "thinremote";
    }

    spdlog::debug("Failed to create default product: {} {}", create_result.status_code, create_result.error_detail);
    return "";
}

int command_handler::handle_uninstall() {
    if (!is_interactive_terminal()) {
        spdlog::error("Uninstall command requires an interactive terminal");
        return 1;
    }
    
    service_manager_.uninstall_completely();
    return 0;
}

int command_handler::handle_status() {
    std::cout << "Checking ThinRemote status...\n";
    spdlog::error("Status check not yet implemented");
    return 1;
}

int command_handler::handle_test() {
    std::cout << "Testing connection...\n";
    spdlog::error("Connection test not yet implemented");
    return 1;
}

int command_handler::handle_update(const UpdateOptions& options) {
    using extensions::updater;

    auto action = options.apply ? updater::action::apply : updater::action::check;
    auto result = updater::run(action, options.channel);

    std::cout << utils::Console::cyan("Current version: ") << result.current << "\n";
    std::cout << utils::Console::cyan("Architecture:    ") << result.arch << "\n";
    if (!result.latest.empty()) {
        std::cout << utils::Console::cyan("Latest version:  ") << result.latest << "\n";
    }

    if (result.status == "up_to_date") {
        std::cout << utils::Console::success("Agent is already up to date.") << "\n";
        return 0;
    }

    if (result.status == "update_available") {
        std::cout << utils::Console::warning("Update available. Re-run with --apply to install it.") << "\n";
        return 0;
    }

    if (result.status == "updated") {
        std::cout << utils::Console::success("Update applied. Restart the agent to use the new binary.") << "\n";
        return 0;
    }

    // Any other status (including "error")
    if (!result.message.empty()) {
        std::cout << utils::Console::error(result.message) << "\n";
    } else {
        std::cout << utils::Console::error("Update failed.") << "\n";
    }
    return 1;
}

int command_handler::handle_reconfigure() {
    if (!is_interactive_terminal()) {
        spdlog::error("Reconfigure command requires an interactive terminal");
        return 1;
    }
    
    // Remove existing config and start interactive setup
    if (config_manager_.exists()) {
        config_manager_.remove();
        std::cout << "Existing configuration removed.\n";
    }
    
    installer::interactive_setup setup;
    return setup.run() ? 0 : 1;
}

int command_handler::handle_no_command(const std::string& config_path) {
    // No command specified - determine mode based on configuration and terminal
    if (config_manager_.exists()) {
        // Configuration exists - check if service is installed and running
        auto service_status = service_manager_.get_service_status();
        
        // If service is installed and we're in interactive mode, show management options
        // BUT skip this if a specific config file was provided via --config
        if ((service_status == installer::service_installer::ServiceStatus::INSTALLED_RUNNING ||
             service_status == installer::service_installer::ServiceStatus::INSTALLED_STOPPED) &&
            is_interactive_terminal() &&
            config_path.empty()) {  // Only show menu if no explicit config was provided
            
            // Show service management options
            if (service_manager_.show_management_menu()) {
                return 0; // User chose to exit or manage service
            }
            // If we get here, user chose to run manually
        }
        
        // Start agent mode (either no service installed or user chose manual mode)
        spdlog::info("Configuration found, starting agent mode");
        try {
            auto credentials = config_manager_.load();
            agent::agent agent(credentials);
            agent.start();
            agent.wait();
        } catch (const std::exception& e) {
            spdlog::error("Failed to start agent: {}", e.what());
            return 1;
        }
    } else {
        // No user configuration found, check for system-wide installation
        if (!is_running_as_root() && check_system_installation()) {
            // System-wide installation exists but we're not root
            if (!is_interactive_terminal()) {
                spdlog::error("System-wide installation detected. Run with 'sudo thinr-agent' to manage it");
                return 1;
            }
            
            return handle_system_installation_detected();
        }
        
        // No configuration - check if we can run interactive setup
        if (!is_interactive_terminal()) {
            spdlog::error("No configuration found and not running in interactive terminal");
            spdlog::error("Please run 'thinr-agent reconfigure' in an interactive terminal");
            return 1;
        }
        
        // Start interactive setup
        spdlog::info("No configuration found, starting interactive setup");
        installer::interactive_setup setup;
        return setup.run() ? 0 : 1;
    }
    
    return 0;
}

bool command_handler::is_interactive_terminal() const {
    return isatty(STDIN_FILENO) && isatty(STDOUT_FILENO);
}

std::string command_handler::read_password_securely() {
    std::cout << utils::Console::userPrompt() << "Password: ";
    std::string password;
    
    // Disable echo for password input
    termios oldt{};
    tcgetattr(STDIN_FILENO, &oldt);
    termios newt = oldt;
    newt.c_lflag &= ~ECHO;
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);
    std::getline(std::cin, password);
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    std::cout << "\n";
    
    return password;
}

void command_handler::setup_logging(int verbosity_level) {
    spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");
    
    switch (verbosity_level) {
        case 0: spdlog::set_level(spdlog::level::off); break;
        case 1: spdlog::set_level(spdlog::level::err); break;
        case 2: spdlog::set_level(spdlog::level::warn); break;
        case 3: spdlog::set_level(spdlog::level::info); break;
        case 4: spdlog::set_level(spdlog::level::debug); break;
        default: spdlog::set_level(spdlog::level::debug); break;
    }
}

bool command_handler::check_system_installation() const {
    // Check if system-wide config exists
    std::filesystem::path system_config = "/etc/thinr-agent/config.json";
    if (!std::filesystem::exists(system_config)) {
        return false;
    }
    
    // Check if system-wide service is installed
    config::config_manager system_config_manager(system_config.string());
    installer::service_installer system_service_installer;

    auto status = system_service_installer.get_service_status_for_privilege(true); // Check system-wide
    return status != installer::service_installer::ServiceStatus::NOT_INSTALLED;
}

int command_handler::handle_system_installation_detected() {
    // Get system-wide service status
    installer::service_installer system_service_installer;
    auto status = system_service_installer.get_service_status_for_privilege(true);

    // Get device info from system config
    config::config_manager system_config_manager("/etc/thinr-agent/config.json");
    std::string device_id = "unknown";
    std::string status_text = "Unknown";
    
    try {
        auto system_creds = system_config_manager.load();
        device_id = system_creds.device_id;
    } catch (...) {
        // Ignore errors reading system config
    }
    
    switch (status) {
        case installer::service_installer::ServiceStatus::INSTALLED_RUNNING:
            status_text = "Running";
            break;
        case installer::service_installer::ServiceStatus::INSTALLED_STOPPED:
            status_text = "Stopped";
            break;
        default:
            status_text = "Installed";
            break;
    }
    
    // Show information about system-wide installation
    std::cout << "\n" << utils::Console::info("A system-wide installation exists but requires root privileges", false) << "\n";
    std::cout << utils::Console::cyan("  Status: ") << status_text << "\n";
    std::cout << utils::Console::cyan("  Device: ") << device_id << "\n";
    std::cout << "\n";
    std::cout << utils::Console::cyan("Run 'sudo thinr-agent' to manage the system service") << "\n\n";
    
    // Offer options
    std::vector<std::string> options = {
        "Create a user-specific installation",
        "Exit"
    };
    
    int choice = utils::Console::getUserChoice(options, "What would you like to do?");
    
    if (choice == 1) {
        // User wants to create their own installation
        installer::interactive_setup setup;
        return setup.run() ? 0 : 1;
    }

    return 0;
}

bool command_handler::is_running_as_root() const {
    return geteuid() == 0;
}

int command_handler::handle_no_configuration_but_run(const std::string& config_path) {
    // This method was referenced but not implemented, adding it
    spdlog::info("Starting agent mode");
    try {
        auto credentials = config_manager_.load();
        agent::agent agent(credentials);
        agent.start();
        agent.wait();
    } catch (const std::exception& e) {
        spdlog::error("Failed to start agent: {}", e.what());
        return 1;
    }
    return 0;
}

} // namespace thinr::cli