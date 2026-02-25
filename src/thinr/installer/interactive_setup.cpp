#include "interactive_setup.hpp"
#include "../agent/agent.hpp"
#include "../utils/device_id_generator.hpp"
#include <iostream>
#include <sstream>
#include <termios.h>
#include <unistd.h>
#include <sys/utsname.h>
#include <fstream>
#include <filesystem>
#include <thread>
#include <chrono>
#include <limits>
#include <cstdio>
#include <algorithm>
#include <spdlog/spdlog.h>
#include <boost/asio.hpp>
#include <condition_variable>
#include <mutex>

namespace thinr::installer {

// OAuth2 Device Flow Client ID
static const std::string OAUTH2_CLIENT_ID = "3b40164d28730e416cbc";

interactive_setup::interactive_setup() 
    : config_manager_(),  // Use default constructor to get proper config path
      service_installer_()  {
    // Set up SSL verification callback
    auth_manager_.set_ssl_verification_callback(
        [this](const std::string& error_msg) -> bool {
            // Only ask once per session
            if (ssl_decision_ == installer::SSLVerificationDecision::NOT_ASKED) {
                std::cout << "\n" << utils::Console::warning("Cannot perform SSL Certificate Verification", false) << "\n";
                std::cout << "This often happens on embedded systems without proper CA certificates installed.\n";
                std::cout << "\n" << utils::Console::yellow("Would you like to continue without certificate verification?") << "\n";
                std::cout << utils::Console::yellow("WARNING: This is less secure and should only be used for testing or trusted internal networks.") << "\n\n";
                
                bool allow = confirm("Continue without certificate verification?", true);
                ssl_decision_ = allow ? installer::SSLVerificationDecision::NO_VERIFY : installer::SSLVerificationDecision::VERIFY;
                return allow;
            }
            
            // Return cached decision
            return ssl_decision_ == installer::SSLVerificationDecision::NO_VERIFY;
        }
    );
}

bool interactive_setup::run() {
    try {
        // Ensure stdin is in a clean state at the start
        std::cin.clear();
        
        // Clear any pending input in the buffer
        if (std::cin.rdbuf()->in_avail() > 0) {
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
        }
        
        clear_screen();
        show_banner();
        show_system_info();
        
        // Show main menu instead of direct confirmation
        MainMenuOption menu_choice = show_main_menu();
        
        switch (menu_choice) {
            case MainMenuOption::INSTALL_SERVICE: {
                // Full flow: authenticate -> install as service (no interactive test)
                config::DeviceCredentials credentials = authenticate();
                
                if (!credentials.is_valid()) {
                    std::cout << "Failed to obtain valid credentials.\n";
                    return false;
                }
                
                bool success = install_as_service(credentials);
                if (success) {
                    std::cout << "\n" << utils::Console::success("ThinRemote setup completed successfully!", false) << "\n";
                    std::cout << "The service is now running. You can access your device at:\n";
                    std::string device_url = "https://" + credentials.host + "/console/devices/" + credentials.device_id;
                    std::cout << "   " << utils::Console::hyperlink(device_url) << "\n";
                }
                return success;
            }
            
            case MainMenuOption::CONFIGURE_STANDALONE: {
                // Authenticate and save config without installing service
                config::DeviceCredentials credentials = authenticate();
                
                if (!credentials.is_valid()) {
                    std::cout << "Failed to obtain valid credentials.\n";
                    return false;
                }
                
                bool success = save_configuration_only(credentials);
                if (success) {
                    std::cout << "\n" << utils::Console::success("ThinRemote setup completed successfully!", false) << "\n";
                    std::cout << "You can now run 'thinr-agent' to start ThinRemote manually.\n";
                }
                return success;
            }
            
            case MainMenuOption::TEST_CONNECTION: {
                // Just test connection without saving anything
                config::DeviceCredentials credentials = authenticate();
                
                if (!credentials.is_valid()) {
                    std::cout << "Failed to obtain valid credentials.\n";
                    return false;
                }
                
                if (test_connection(credentials)) {
                    std::cout << "Connection test successful!\n\n";
                    
                    // After successful test, offer installation options
                    utils::Console::printSectionHeader("What would you like to do next?", "💭");
                    
                    std::vector<std::string> options = {
                        "Install as a service",
                        "Save configuration for standalone use",
                        "Exit without saving"
                    };
                    
                    int choice = utils::Console::getUserChoice(options, "");
                    
                    switch (choice) {
                        case 1: {
                            bool success = install_as_service(credentials);
                            if (success) {
                                std::cout << "\n" << utils::Console::success("ThinRemote setup completed successfully!", false) << "\n";
                            }
                            return success;
                        }
                        case 2: {
                            bool success = save_configuration_only(credentials);
                            if (success) {
                                std::cout << "\n" << utils::Console::success("ThinRemote setup completed successfully!", false) << "\n";
                                std::cout << "You can now run 'thinr-agent' to start ThinRemote manually.\n";
                            }
                            return success;
                        }
                        case 3:
                            std::cout << "\nExiting without saving configuration.\n";
                            std::cout << "Run setup again if you want to install or configure ThinRemote.\n";
                            return true;
                        default:
                            return true;
                    }
                } else {
                    std::cout << "Connection test failed. Please check your configuration.\n";
                    return false;
                }
            }
            
            case MainMenuOption::EXIT:
                std::cout << "Setup cancelled by user.\n";
                return false;
        }
        
        return false;
        
    } catch (const std::exception& e) {
        spdlog::error("Setup error: {}", e.what());
        std::cout << "Setup failed: " << e.what() << "\n";
        return false;
    }
}

void interactive_setup::show_banner() {
    utils::Console::printBanner("ThinRemote", "");
}

void interactive_setup::show_system_info() {
    utils::Console::printSectionHeader("System Information", "💻");
    
    struct utsname system_info;
    if (uname(&system_info) == 0) {
        // System info
        std::cout << utils::Console::cyan("System: ") << std::string(system_info.sysname) << "\n";
        std::cout << utils::Console::cyan("Arhitecture: ") << system_info.machine << "\n";
        std::cout << utils::Console::cyan("Hostname: ") << system_info.nodename << "\n";
        std::cout << utils::Console::cyan("Kernel: ") << system_info.release << "\n";

        // Detect distribution
        std::cout << utils::Console::cyan("Distribution: ") << detect_distribution() << "\n";
        
        // Detect init system
        std::cout << utils::Console::cyan("Init system: ") << detect_init_system() << "\n";
        std::cout << "\n";
        std::cout << utils::Console::success("System compatible", false) << "\n\n";
    }
}

bool interactive_setup::confirm_connectivity_test() {
    utils::Console::printSectionHeader("Getting Started", "🚀");
    return confirm("Configure ThinRemote connection?", true);
}

MainMenuOption interactive_setup::show_main_menu() {
    utils::Console::printSectionHeader("What would you like to do?", "🚀");
    
    std::vector<std::string> options = {
        "Install as a service",
        "Configure for standalone execution",
        "Test connection without installing",
        "Exit"
    };
    
    int choice = utils::Console::getUserChoice(options, "");
    
    switch (choice) {
        case 1: return MainMenuOption::INSTALL_SERVICE;
        case 2: return MainMenuOption::CONFIGURE_STANDALONE;
        case 3: return MainMenuOption::TEST_CONNECTION;
        case 4: return MainMenuOption::EXIT;
        default: return MainMenuOption::EXIT;
    }
}

AuthMethod interactive_setup::select_auth_method() {
    std::vector<std::string> options = {
        "Browser authentication (recommended)",
        "Username and password",
        "Auto-provisioning token",
        "Direct device credentials"
    };
    
    int choice = utils::Console::getUserChoice(options, "🔐 Select authentication method:");
    
    switch (choice) {
        case 1: return AuthMethod::DEVICE_FLOW;
        case 2: return AuthMethod::PASSWORD_FLOW;
        case 3: return AuthMethod::AUTO_PROVISION;
        case 4: return AuthMethod::DIRECT_CREDENTIALS;
        default: return AuthMethod::DEVICE_FLOW; // Fallback to recommended
    }
}

config::DeviceCredentials interactive_setup::authenticate() {
    AuthMethod method = AuthMethod::PASSWORD_FLOW; // Will be set on first iteration
    bool first_time = true;
    
    while (true) {
        try {
            if (first_time) {
                method = select_auth_method();
                first_time = false;
            }

            switch (method) {
                case AuthMethod::PASSWORD_FLOW:
                    return password_flow_auth();
                case AuthMethod::AUTO_PROVISION:
                    return auto_provision_auth();
                case AuthMethod::DEVICE_FLOW:
                    return device_flow_auth();
                case AuthMethod::DIRECT_CREDENTIALS:
                    return direct_credentials_auth();
                default:
                    throw std::runtime_error("Invalid authentication method");
            }

        } catch (const setup_cancelled&) {
            throw;  // User cancellation — propagate immediately
        } catch (const std::exception&) {
            // Ask user if they want to retry or try a different method
            std::vector<std::string> options = {
                "Try again with same method",
                "Choose different authentication method",
                "Exit setup"
            };

            int choice = utils::Console::getUserChoice(options, "What would you like to do?");

            switch (choice) {
                case 1: break;
                case 2: first_time = true; break;
                case 3: throw setup_cancelled("Setup cancelled by user");
                default: break;
            }
        }
    }
}

config::DeviceCredentials interactive_setup::password_flow_auth() {
    utils::Console::printSectionHeader("Username and password authentication", "🔐");

    std::string host = read_input("ThinRemote Host (e.g: thin.company.com): ");
    std::string username = read_input("Username: ");
    std::string password = read_password("Password: ");

    std::cout << utils::Console::loading("Performing OAuth authentication...") << "\n";

    auto result = auth_manager_.oauth_password_flow(host, username, password);

    if (!result.success) {
        show_auth_error(result);
        throw std::runtime_error("Authentication failed");
    }

    std::cout << utils::Console::success("Authentication successful") << "\n";

    auto [device_id, device_name] = prompt_device_info();

    auto credentials = provision_with_conflict_resolution(host, username, device_id, device_name, result.access_token);
    std::cout << utils::Console::success("Device provisioned: ") << credentials.device_id << "\n";

    return credentials;
}

config::DeviceCredentials interactive_setup::auto_provision_auth() {
    utils::Console::printSectionHeader("Auto-provisioning token authentication", "🔑");

    std::string token = read_input("Provisioning token: ");

    std::cout << utils::Console::loading("Validating token...") << "\n";

    try {
        nlohmann::json payload = auth_manager_.decode_jwt_payload(token);
        std::cout << utils::Console::success("Token validated successfully") << "\n\n";
    } catch (const std::exception& e) {
        std::cout << utils::Console::error("Invalid provisioning token - check your token") << "\n";
        spdlog::debug("JWT decode failed: {}", e.what());
        throw std::runtime_error("Auto-provisioning failed");
    }

    auto [device_id, device_name] = prompt_device_info();

    auto credentials = auto_provision_with_conflict_resolution(token, device_id, device_name);
    std::cout << utils::Console::success("Device provisioned: ") << credentials.device_id << "\n";

    return credentials;
}

config::DeviceCredentials interactive_setup::device_flow_auth() {
    utils::Console::printSectionHeader("Browser authentication", "🌐");

    std::string host = read_input("ThinRemote Host (e.g: thin.company.com): ");

    std::cout << utils::Console::loading("Initiating device authorization flow...") << "\n";

    // Start the device flow
    auto device_flow = auth_manager_.start_device_flow(host, OAUTH2_CLIENT_ID);

    if (!device_flow.success) {
        show_device_flow_error(device_flow);
        throw std::runtime_error("Device flow authentication failed");
    }

    const auto& device_response = device_flow.response;

    // Display instructions to user
    std::cout << utils::Console::success("Device authorization started!") << "\n\n";
    std::cout << utils::Console::cyan("To complete authentication:") << "\n";
    std::string activation_url = device_response.verification_uri + "?user_code=" + device_response.user_code;
    std::cout << "1. Open your web browser and go to: " << utils::Console::bold(utils::Console::hyperlink(activation_url)) << "\n";
    std::cout << "2. Follow the instructions to authorize this device\n\n";
    std::cout << utils::Console::info("This code expires in ", false) << device_response.expires_in << " seconds\n\n";

    std::cout << utils::Console::loading("Waiting for authorization...") << "\n";

    // Poll for the access token
    auto poll_result = auth_manager_.poll_device_token(
        host,
        OAUTH2_CLIENT_ID,
        device_response.device_code,
        device_response.expires_in,
        device_response.interval
    );

    if (!poll_result.success) {
        show_auth_error(poll_result);
        throw std::runtime_error("Device flow authentication failed");
    }

    std::cout << utils::Console::success("Authorization successful!") << "\n\n";

    auto [device_id, device_name] = prompt_device_info();

    // Extract username from the JWT access token
    std::string username;
    try {
        nlohmann::json payload = auth_manager_.decode_jwt_payload(poll_result.access_token);
        if (payload.contains("usr")) {
            username = payload["usr"];
            spdlog::debug("Extracted username from token: {}", username);
        } else {
            throw std::runtime_error("JWT token missing 'usr' field");
        }
    } catch (const std::exception& e) {
        spdlog::error("Failed to extract username from JWT: {}", e.what());
        throw std::runtime_error("Failed to extract username from access token: " + std::string(e.what()));
    }

    auto credentials = provision_with_conflict_resolution(host, username, device_id, device_name, poll_result.access_token);
    std::cout << utils::Console::success("Device provisioned: ") << credentials.device_id << "\n";

    return credentials;
}

config::DeviceCredentials interactive_setup::direct_credentials_auth() {
    utils::Console::printSectionHeader("Direct device credentials", "🔧");
    
    config::DeviceCredentials credentials;
    credentials.host = read_input("Host: ");
    credentials.device_user = read_input("Device user: ");
    credentials.device_id = read_input("Device ID: ");
    credentials.device_token = read_password("Device token: ");
    credentials.version = "1.0.0";
    
    return credentials;
}

bool interactive_setup::test_connection(const config::DeviceCredentials& creds) {
    utils::Console::printSectionHeader("Connection Test", "🔗");
    std::cout << utils::Console::loading("Testing connection...") << "\n";

    try {
        // Save current log level and set to silent during test
        auto original_level = spdlog::get_level();
        spdlog::set_level(spdlog::level::off);
        
        // Create temporary agent for testing
        agent::agent test_agent(creds);

        // Start agent in a separate thread for testing
        test_agent.start();

        // Wait for connection with timeout
        auto start_time = std::chrono::steady_clock::now();
        const auto timeout = std::chrono::seconds(30);

        auto& client = test_agent.get_client();

        // Check connection status periodically
        while (std::chrono::steady_clock::now() - start_time < timeout) {
            // client.run() is thread-safe and returns immediately
            bool connected = client.run([]() {
                // This lambda runs in the client's io_context thread
                // If we get here, client.connected() returned true
                return true;
            });

            if (connected) {
                std::cout << utils::Console::success("Connection established successfully") << "\n";
                
                // Show device URL and let user test it
                std::string device_url = "https://" + creds.host + "/console/devices/" + creds.device_id;
                std::cout << "\n" << utils::Console::cyan("🌐 Your device is now live! Access it at:") << "\n";
                std::cout << "   " << utils::Console::hyperlink(device_url, device_url) << "\n\n";
                std::cout << utils::Console::cyan("💡 You can now test your device from the web dashboard.") << "\n";
                std::cout << utils::Console::cyan("   Try viewing system metrics, running commands, etc.") << "\n\n";
                
                // Wait for user to test
                std::cout << utils::Console::userPrompt() << "Press Enter to stop demo...";
                std::string dummy;
                std::getline(std::cin, dummy); // Wait for Enter
                std::cout << "\n";
                
                test_agent.stop();
                
                // Restore original log level
                spdlog::set_level(original_level);
                return true;
            }

            // Wait a bit before checking again
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }

        // Timeout reached
        test_agent.stop();
        
        // Restore original log level
        spdlog::set_level(original_level);
        std::cout << utils::Console::error("Failed to connect to ThinRemote server (timeout after 30 seconds)") << "\n";
        return false;

    } catch (const std::exception& e) {
        // Restore original log level in case of exception
        spdlog::set_level(spdlog::level::info); // Default level
        spdlog::error("Connection test failed: {}", e.what());
        std::cout << utils::Console::error("Connection error: ") << e.what() << "\n";
        return false;
    }
}

std::pair<bool, bool> interactive_setup::offer_service_installation(const config::DeviceCredentials& credentials) {
    utils::Console::printSectionHeader("Installation Options", "⚙️");
    
    // Show current service status
    auto status = service_installer_.get_service_status();
    std::string status_text;
    switch (status) {
        case service_installer::ServiceStatus::NOT_INSTALLED:
            status_text = "Not installed";
            break;
        case service_installer::ServiceStatus::INSTALLED_STOPPED:
            status_text = "Installed but stopped";
            break;
        case service_installer::ServiceStatus::INSTALLED_RUNNING:
            status_text = "Installed and running";
            break;
        case service_installer::ServiceStatus::UNKNOWN:
            status_text = "Unknown";
            break;
    }
    
    // Only show status if it's meaningful to the user (i.e., something is already installed)
    if (status != service_installer::ServiceStatus::NOT_INSTALLED) {
        std::cout << utils::Console::cyan("Current status: ") << status_text << "\n\n";
    }
    
    if (status == service_installer::ServiceStatus::INSTALLED_RUNNING) {
        std::cout << utils::Console::success("Service is already installed and running") << "\n";
        return {true, true}; // Configuration saved, service installed
    }
    
    if (status == service_installer::ServiceStatus::INSTALLED_STOPPED) {
        if (confirm("Service is installed but not running. Start it?", true)) {
            if (service_installer_.start_service()) {
                std::cout << utils::Console::success("Service started successfully") << "\n";
            } else {
                std::cout << utils::Console::error("Failed to start service") << "\n";
            }
        }
        return {true, true}; // Configuration saved, service installed
    }
    
    // Service not installed, offer installation options
    std::vector<std::string> options = {
        "Install as service (recommended) - Starts automatically, runs continuously",
        "Save configuration for manual use - Run when needed, full control",
        "Don't save configuration - Just testing, don't install anything"
    };
    
    int choice = utils::Console::getUserChoice(options, "How would you like to use ThinRemote?");
    
    switch (choice) {
        case 1: {
            bool success = install_as_service(credentials);
            return {success, success}; // If install succeeded, service is installed
        }
        case 2: {
            bool success = save_configuration_only(credentials);
            return {success, false}; // Config saved but no service
        }
        case 3:
            std::cout << utils::Console::cyan("Configuration not saved. Setup completed.") << "\n";
            return {false, false}; // Neither config nor service
        default:
            return {false, false}; // Fallback
    }
}

bool interactive_setup::install_as_service(const config::DeviceCredentials& credentials) {
    // Persist SSL verification decision into credentials
    auto final_credentials = credentials;
    if (ssl_decision_ == SSLVerificationDecision::NO_VERIFY) {
        final_credentials.verify_ssl = false;
    }

    // First save configuration - service needs it to run
    std::cout << "\n" << utils::Console::loading("Saving configuration...") << "\n";
    try {
        config_manager_.save(final_credentials);
        std::string config_path = config_manager_.get_config_path();
        std::cout << utils::Console::success("Configuration saved: " + config_path) << "\n";
    } catch (const std::exception& e) {
        std::cout << utils::Console::error("Failed to save configuration: ") << e.what() << "\n";
        return false;
    }
    
    // Add section header for service installation
    std::cout << "\n";  // Add spacing before new section
    utils::Console::printSectionHeader("Service Installation", "🛠️");
    std::cout << utils::Console::loading("Installing service...") << "\n";
    
    if (service_installer_.install_service()) {
        std::cout << utils::Console::loading("Starting service...") << "\n";
        if (service_installer_.start_service()) {
            std::cout << utils::Console::success("Service started successfully") << "\n";
        } else {
            std::cout << utils::Console::error("Failed to start service") << "\n";
        }
        
        return true;
    } else {
        std::cout << utils::Console::error("Failed to install service") << "\n";
        return false;
    }
}

bool interactive_setup::save_configuration_only(const config::DeviceCredentials& credentials) {
    // Persist SSL verification decision into credentials
    auto final_credentials = credentials;
    if (ssl_decision_ == SSLVerificationDecision::NO_VERIFY) {
        final_credentials.verify_ssl = false;
    }

    std::cout << "\n" << utils::Console::loading("Saving configuration...") << "\n";

    try {
        config_manager_.save(final_credentials);
        std::string config_path = config_manager_.get_config_path();
        std::cout << utils::Console::success("Configuration saved: " + config_path) << "\n";
        std::cout << utils::Console::cyan("You can now run 'thinr-agent' to start ThinRemote manually.") << "\n";
        return true;
    } catch (const std::exception& e) {
        std::cout << utils::Console::error("Failed to save configuration: ") << e.what() << "\n";
        return false;
    }
}

std::pair<std::string, std::string> interactive_setup::prompt_device_info() {
    utils::Console::printSectionHeader("Device Provisioning", "🖥️");

    std::string default_device_id = get_default_device_id();
    std::cout << "Enter device identifier (unique name for this device):\n";
    std::cout << utils::Console::userPrompt() << "Device ID [" << default_device_id << "]: ";
    std::string device_id_input;
    std::getline(std::cin, device_id_input);
    std::string device_id = device_id_input.empty() ? default_device_id : device_id_input;

    std::string default_device_name = get_default_device_name();
    std::cout << "\nEnter device name (human-friendly name for this device):\n";
    std::cout << utils::Console::userPrompt() << "Device Name [" << default_device_name << "]: ";
    std::string device_name_input;
    std::getline(std::cin, device_name_input);
    std::string device_name = device_name_input.empty() ? default_device_name : device_name_input;

    return {device_id, device_name};
}

std::string interactive_setup::get_default_device_id() {
    // Use the new device ID generator
    std::string generated_id = utils::DeviceIdGenerator::generate();
    
    // Ensure it only returns [a-zA-Z0-9_-] and up to 32 characters
    if (generated_id.length() > 32) {
        generated_id = generated_id.substr(0, 32);
    }
    
    // Replace invalid characters with '-'
    for (char& c : generated_id) {
        if (!std::isalnum(c) && c != '-' && c != '_') {
            c = '-';  // Replace invalid characters with '-'
        }
    }
    
    return generated_id;
}

std::string interactive_setup::get_default_device_name() {
    // Get the full hostname as the default device name
    boost::system::error_code ec;
    std::string hostname = boost::asio::ip::host_name(ec);
    if (hostname.empty() || ec) {
        hostname = "ThinRemote Device";
    }
    
    // Remove .local suffix if present (common in macOS)
    const std::string local_suffix = ".local";
    if (hostname.length() > local_suffix.length() && 
        hostname.substr(hostname.length() - local_suffix.length()) == local_suffix) {
        hostname = hostname.substr(0, hostname.length() - local_suffix.length());
    }
    
    // Convert to lowercase for consistency
    std::transform(hostname.begin(), hostname.end(), hostname.begin(), ::tolower);
    
    return hostname;
}

std::string interactive_setup::detect_distribution() {
    // Try /etc/os-release first (most common)
    std::ifstream os_release("/etc/os-release");
    if (os_release.is_open()) {
        std::string line;
        while (std::getline(os_release, line)) {
            if (line.find("PRETTY_NAME=") == 0) {
                std::string distro = line.substr(12);
                // Remove quotes
                if (!distro.empty() && distro.front() == '"' && distro.back() == '"') {
                    distro = distro.substr(1, distro.length() - 2);
                }
                if (!distro.empty()) {
                    return distro;
                }
            }
        }
    }
    
    // Try to detect macOS
    struct utsname system_info;
    if (uname(&system_info) == 0) {
        std::string sysname = system_info.sysname;
        if (sysname == "Darwin") {
            // Try to get macOS version
            std::ifstream version_file("/System/Library/CoreServices/SystemVersion.plist");
            if (version_file.is_open()) {
                std::string line;
                bool found_version = false;
                std::string version;
                
                while (std::getline(version_file, line)) {
                    if (line.find("ProductUserVisibleVersion") != std::string::npos) {
                        found_version = true;
                        continue;
                    }
                    if (found_version && line.find("<string>") != std::string::npos) {
                        size_t start = line.find("<string>") + 8;
                        size_t end = line.find("</string>");
                        if (end != std::string::npos) {
                            version = line.substr(start, end - start);
                            break;
                        }
                    }
                }
                
                if (!version.empty()) {
                    return "macOS " + version;
                }
            }
            return "macOS";
        }
    }
    
    // Fallback: try other common release files
    std::vector<std::pair<std::string, std::string>> release_files = {
        {"/etc/redhat-release", "Red Hat"},
        {"/etc/debian_version", "Debian"},
        {"/etc/alpine-release", "Alpine Linux"},
        {"/etc/arch-release", "Arch Linux"},
        {"/etc/gentoo-release", "Gentoo"}
    };
    
    for (const auto& [file_path, distro_name] : release_files) {
        std::ifstream file(file_path);
        if (file.is_open()) {
            std::string content;
            std::getline(file, content);
            if (!content.empty()) {
                return distro_name + " " + content;
            }
            return distro_name;
        }
    }
    
    return "Unknown";
}

std::string interactive_setup::detect_init_system() {
    // Check if running on macOS (Darwin) - uses launchd
    struct utsname system_info;
    if (uname(&system_info) == 0) {
        std::string sysname = system_info.sysname;
        if (sysname == "Darwin") {
            // macOS uses launchd as init system
            if (std::filesystem::exists("/sbin/launchd") || 
                std::filesystem::exists("/System/Library/LaunchDaemons")) {
                return "launchd";
            }
            return "launchd (assumed)"; // macOS always uses launchd
        }
    }
    
    // Check for systemd (most common modern init system)
    if (std::filesystem::exists("/run/systemd/system") || 
        std::filesystem::exists("/systemd")) {
        return "systemd";
    }
    
    // Check for OpenRC (Alpine Linux, Gentoo)
    if (std::filesystem::exists("/sbin/openrc") || 
        std::filesystem::exists("/etc/init.d/") && std::filesystem::exists("/sbin/rc-service")) {
        return "OpenRC";
    }
    
    // Check for Upstart (older Ubuntu versions)
    if (std::filesystem::exists("/sbin/upstart") || 
        std::filesystem::exists("/etc/init/") && std::filesystem::exists("/sbin/initctl")) {
        return "Upstart";
    }
    
    // Check for SysV init (traditional Unix init)
    if (std::filesystem::exists("/etc/inittab") || 
        std::filesystem::exists("/etc/rc.d/") || 
        std::filesystem::exists("/etc/init.d/")) {
        return "SysV init";
    }
    
    // Check for runit (Void Linux)
    if (std::filesystem::exists("/etc/runit") || 
        std::filesystem::exists("/usr/bin/sv")) {
        return "runit";
    }
    
    // Check for s6 (some embedded systems)
    if (std::filesystem::exists("/etc/s6") || 
        std::filesystem::exists("/usr/bin/s6-svc")) {
        return "s6";
    }
    
    // Fallback: check if /sbin/init exists
    if (std::filesystem::exists("/sbin/init")) {
        return "init (unknown type)";
    }
    
    return "Unknown";
}

std::string interactive_setup::read_input(const std::string& prompt, bool hide_input) {
    if (hide_input) {
        return read_password(prompt);
    }
    
    std::cout << utils::Console::userPrompt() << prompt << " ";
    std::string input;
    std::getline(std::cin, input);
    return input;
}

std::string interactive_setup::read_password(const std::string& prompt) {
    std::cout << utils::Console::userPrompt() << prompt << " ";
    std::cout.flush(); // Ensure prompt is displayed immediately

    // Disable echo
    struct termios old_termios, new_termios;
    if (tcgetattr(STDIN_FILENO, &old_termios) != 0) {
        // Fallback to regular input if termios fails
        std::string password;
        std::getline(std::cin, password);
        return password;
    }
    
    new_termios = old_termios;
    new_termios.c_lflag &= ~ECHO;  // Only disable echo, keep other flags
    
    if (tcsetattr(STDIN_FILENO, TCSANOW, &new_termios) != 0) {
        // Fallback to regular input if setting fails
        std::string password;
        std::getline(std::cin, password);
        return password;
    }
    
    std::string password;
    std::getline(std::cin, password);
    
    // Restore echo
    tcsetattr(STDIN_FILENO, TCSANOW, &old_termios);
    std::cout << "\n";
    
    return password;
}

bool interactive_setup::confirm(const std::string& prompt, bool default_yes) {
    return utils::Console::confirm(prompt, default_yes);
}

void interactive_setup::clear_screen() {
    utils::Console::clear();
}

interactive_setup::ConflictResolution interactive_setup::handle_device_conflict(const std::string& device_id) {
    std::cout << utils::Console::warning("Device '") << device_id << "' already exists." << "\n\n";
    
    std::vector<std::string> options = {
        "Choose different identifier",
        "Overwrite (delete existing device)",
        "Go back"
    };
    
    int choice = utils::Console::getUserChoice(options, "What would you like to do?");
    
    switch (choice) {
        case 1: return ConflictResolution::CHOOSE_NEW_ID;
        case 2: return ConflictResolution::OVERWRITE;
        case 3: return ConflictResolution::GO_BACK;
        default: return ConflictResolution::GO_BACK; // Fallback
    }
}

config::DeviceCredentials interactive_setup::provision_with_conflict_resolution(
    const std::string& host,
    const std::string& username,
    const std::string& initial_device_id,
    const std::string& device_name,
    const std::string& access_token) {

    return provision_with_conflict_loop(
        initial_device_id, device_name,
        [&](const std::string& did, const std::string& dname) {
            return auth_manager_.provision_device(host, username, did, dname, access_token);
        },
        [&](const std::string& did) {
            return auth_manager_.delete_device(host, username, did, access_token);
        }
    );
}

config::DeviceCredentials interactive_setup::auto_provision_with_conflict_resolution(
    const std::string& token,
    const std::string& initial_device_id,
    const std::string& device_name) {

    return provision_with_conflict_loop(
        initial_device_id, device_name,
        [&](const std::string& did, const std::string& dname) {
            return auth_manager_.auto_provision_with_device_id(token, did, dname);
        },
        [&](const std::string& did) {
            nlohmann::json payload = auth_manager_.decode_jwt_payload(token);
            std::string host = payload.value("svr", "");
            std::string username = payload.value("usr", "");
            if (host.empty() || username.empty()) return false;
            return auth_manager_.delete_device(host, username, did, token);
        }
    );
}

config::DeviceCredentials interactive_setup::provision_with_conflict_loop(
    const std::string& initial_device_id,
    const std::string& device_name,
    ProvisionFn provision_fn,
    DeleteFn delete_fn) {

    std::string device_id = initial_device_id;

    while (true) {
        if (device_id.empty()) {
            std::string default_device_id = "thinremote-device";
            std::cout << "Enter device identifier (unique name for this device):\n";
            std::cout << utils::Console::userPrompt() << "Device ID [" << default_device_id << "]: ";
            std::string device_id_input;
            std::getline(std::cin, device_id_input);
            device_id = device_id_input.empty() ? default_device_id : device_id_input;
        }

        std::cout << "\n" << utils::Console::loading("Provisioning device: ") << device_id << "..." << "\n";

        auto result = provision_fn(device_id, device_name);

        if (result.success) {
            return result.credentials;
        }

        if (result.status_code == 409) {
            ConflictResolution resolution = handle_device_conflict(device_id);

            switch (resolution) {
                case ConflictResolution::CHOOSE_NEW_ID:
                    device_id.clear();
                    break;

                case ConflictResolution::OVERWRITE:
                    std::cout << utils::Console::loading("Deleting existing device...") << "\n";
                    if (delete_fn(device_id)) {
                        std::cout << utils::Console::success("Device deleted") << "\n";
                        std::cout << utils::Console::loading("Creating new device...") << "\n";
                        auto retry = provision_fn(device_id, device_name);
                        if (retry.success) {
                            return retry.credentials;
                        }
                        spdlog::error("Failed to create device after deletion (HTTP {})", retry.status_code);
                    }
                    std::cout << utils::Console::error("Error recreating device, try a different ID") << "\n";
                    device_id.clear();
                    break;

                case ConflictResolution::GO_BACK:
                    throw setup_cancelled("Setup cancelled by user");
            }
        } else {
            throw std::runtime_error("Device provisioning failed (HTTP " + std::to_string(result.status_code) + ")");
        }
    }
}

void interactive_setup::show_auth_error(const auth::auth_manager::AuthResult& result) {
    using auth::AuthError;
    switch (result.error) {
        case AuthError::invalid_credentials:
            std::cout << utils::Console::error("Invalid username or password") << "\n";
            break;
        case AuthError::access_denied:
            std::cout << utils::Console::error("Access denied - check your permissions") << "\n";
            break;
        case AuthError::not_found:
            std::cout << utils::Console::error("Host not found - check the ThinRemote host URL") << "\n";
            break;
        case AuthError::network_error:
            std::cout << utils::Console::error("Connection failed - check your network connection") << "\n";
            break;
        case AuthError::ssl_error:
            std::cout << utils::Console::error("SSL connection failed") << "\n";
            break;
        case AuthError::timeout:
            std::cout << utils::Console::error("Authorization timeout - please try again") << "\n";
            break;
        case AuthError::user_denied:
            std::cout << utils::Console::error("Authorization denied by user") << "\n";
            break;
        case AuthError::token_expired:
            std::cout << utils::Console::error("Device code expired - please try again") << "\n";
            break;
        case AuthError::invalid_response:
            std::cout << utils::Console::error("Invalid server response") << "\n";
            break;
        case AuthError::server_error:
            std::cout << utils::Console::error("Server error") << "\n";
            break;
        default:
            std::cout << utils::Console::error("Authentication failed: ") << result.error_detail << "\n";
            break;
    }
    spdlog::debug("Auth error (status {}): {}", result.status_code, result.error_detail);
}

void interactive_setup::show_device_flow_error(const auth::auth_manager::DeviceFlowResult& result) {
    using auth::AuthError;
    switch (result.error) {
        case AuthError::not_found:
            std::cout << utils::Console::error("Host not found - check the ThinRemote host URL") << "\n";
            break;
        case AuthError::network_error:
            std::cout << utils::Console::error("Connection failed - check your network connection and host URL") << "\n";
            break;
        case AuthError::ssl_error:
            std::cout << utils::Console::error("SSL connection failed") << "\n";
            break;
        default:
            std::cout << utils::Console::error("Device flow failed: ") << result.error_detail << "\n";
            break;
    }
    spdlog::debug("Device flow error (status {}): {}", result.status_code, result.error_detail);
}

} // namespace thinr::installer