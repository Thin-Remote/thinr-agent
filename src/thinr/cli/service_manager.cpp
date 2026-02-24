#include "service_manager.hpp"
#include "../utils/console.hpp"
#include "../installer/installation_config.hpp"
#include <iostream>
#include <thread>
#include <chrono>
#include <filesystem>
#include <array>
#include <cstdio>
#include <unistd.h>
#include <spdlog/spdlog.h>

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

namespace thinr::cli {

service_manager::service_manager() = default;

bool service_manager::show_management_menu() {
    auto status = service_installer_.get_service_status();
    
    utils::Console::printSectionHeader("Service Management", "📊");
    show_service_status();
    
    if (status == installer::service_installer::ServiceStatus::INSTALLED_RUNNING) {
        bool update_available = has_update_available();

        std::vector<std::string> options;
        if (update_available) {
            options.push_back("Update service to " + std::string(AGENT_VERSION));
        }
        options.push_back("View service logs");
        options.push_back("Restart service");
        options.push_back("Stop service and run manually");
        options.push_back("Uninstall service and remove configuration");
        options.push_back("Exit");

        int choice = utils::Console::getUserChoice(options, "What would you like to do?");

        // Adjust choice offset if update option is not shown
        if (!update_available) {
            choice += 1;  // shift to match the running_service_choice mapping with update at 1
        }

        return handle_running_service_choice(choice);
        
    } else if (status == installer::service_installer::ServiceStatus::INSTALLED_STOPPED) {
        std::vector<std::string> options = {
            "Start service",
            "Run manually (one-time)",
            "Uninstall service and remove configuration", 
            "Exit"
        };
        
        int choice = utils::Console::getUserChoice(options, "What would you like to do?");
        return handle_stopped_service_choice(choice);
        
    } else {
        // Service not installed, shouldn't get here but handle gracefully
        return false;
    }
}

void service_manager::show_service_status() const {
    auto status = get_service_status();

    std::string status_text;
    switch (status) {
        case installer::service_installer::ServiceStatus::NOT_INSTALLED:
            status_text = "Not installed";
            break;
        case installer::service_installer::ServiceStatus::INSTALLED_STOPPED:
            status_text = "Installed but stopped";
            break;
        case installer::service_installer::ServiceStatus::INSTALLED_RUNNING:
            status_text = "Running";
            break;
        case installer::service_installer::ServiceStatus::UNKNOWN:
            status_text = "Unknown";
            break;
    }

    std::cout << utils::Console::cyan("Service status: ") << status_text << "\n";

    // Show version info
    std::string installed_version = get_installed_version();
    std::string current_version = AGENT_VERSION;

    if (!installed_version.empty()) {
        std::cout << utils::Console::cyan("Installed version: ") << installed_version << "\n";
    }

    if (installed_version != current_version) {
        std::cout << utils::Console::cyan("This binary version: ") << current_version << "\n";
    }

    std::cout << "\n";
}


bool service_manager::handle_running_service_choice(int choice) {
    switch (choice) {
        case 1: // Update service
            return update_service();

        case 2: // View logs
            return view_logs();

        case 3: // Restart service
            return restart_service();

        case 4: // Stop and run manually
            std::cout << utils::Console::loading("Stopping service...") << "\n";
            if (service_installer_.stop_service()) {
                // Give it a moment to fully stop
                std::this_thread::sleep_for(std::chrono::seconds(1));
                
                // Verify service is actually stopped
                auto new_status = service_installer_.get_service_status();
                if (new_status == installer::service_installer::ServiceStatus::INSTALLED_STOPPED ||
                    new_status == installer::service_installer::ServiceStatus::NOT_INSTALLED) {
                    std::cout << utils::Console::success("Service stopped successfully. Switching to manual mode...") << "\n";
                    return false; // Continue to manual mode
                } else {
                    std::cout << utils::Console::warning("Service reported as stopped but may still be terminating.") << "\n";
                    std::cout << utils::Console::info("Proceeding to manual mode...") << "\n";
                    return false; // Continue to manual mode anyway
                }
            } else {
                std::cout << utils::Console::error("Failed to stop service") << "\n";
                return true; // Return to exit
            }
            
        case 5: // Uninstall
            return uninstall_completely();

        case 6: // Exit
        default:
            return true; // Exit
    }
}

bool service_manager::handle_stopped_service_choice(int choice) {
    switch (choice) {
        case 1: // Start service
            return start_service();
            
        case 2: // Run manually
            std::cout << utils::Console::success("Running in manual mode...") << "\n";
            return false; // Continue to manual mode
            
        case 3: // Uninstall
            return uninstall_completely();
            
        case 4: // Exit
        default:
            return true; // Exit
    }
}

bool service_manager::uninstall_completely() {
    utils::Console::printSectionHeader("Complete Uninstallation", "🗑️");
    
    if (!confirm_uninstall()) {
        std::cout << utils::Console::cyan("Uninstallation cancelled.") << "\n";
        return true; // Return to exit
    }
    
    bool success = true;
    
    // Step 1: Stop service if running
    auto status = service_installer_.get_service_status();
    if (status == installer::service_installer::ServiceStatus::INSTALLED_RUNNING) {
        std::cout << utils::Console::loading("Stopping service...") << "\n";
        if (service_installer_.stop_service()) {
            std::cout << utils::Console::success("Service stopped") << "\n";
        } else {
            std::cout << utils::Console::error("Failed to stop service") << "\n";
            success = false;
        }
    }
    
    // Step 2: Uninstall service and binary
    std::cout << utils::Console::loading("Removing service...") << "\n";
    if (!service_installer_.uninstall_service()) {
        success = false;
    }
    
    // Step 3: Remove configuration
    std::cout << utils::Console::loading("Removing configuration...") << "\n";
    if (!remove_configuration_and_data()) {
        success = false;
    }
    
    if (success) {
        std::cout << "\n" << utils::Console::success("ThinRemote has been completely uninstalled", false) << "\n";
    } else {
        std::cout << "\n" << utils::Console::warning("Uninstallation completed with some errors", false) << "\n";
        std::cout << utils::Console::cyan("You may need to manually remove some files") << "\n";
    }
    
    return true; // Return to exit
}

bool service_manager::confirm_uninstall() {
    std::cout << utils::Console::cyan("This will:") << "\n";
    std::cout << "  • Stop the service if running\n";
    std::cout << "  • Uninstall the service completely\n"; 
    std::cout << "  • Remove all configuration files\n";
    std::cout << "  • Remove the installed binary\n\n";
    
    std::cout << utils::Console::warning("This action cannot be undone!", false) << "\n\n";
    
    return utils::Console::confirm("Are you sure you want to completely uninstall ThinRemote?", false);
}

bool service_manager::remove_configuration_and_data() {
    try {
        bool config_removed = false;
        
        if (config_manager_.exists()) {
            std::string config_path = config_manager_.get_config_path();
            config_manager_.remove();
            std::cout << utils::Console::success("Configuration removed: " + config_path) << "\n";
            config_removed = true;
            
            // Try to remove the config directory if it's empty
            std::filesystem::path config_dir = std::filesystem::path(config_path).parent_path();
            std::error_code ec;
            
            // Only remove if directory is empty
            if (std::filesystem::is_empty(config_dir, ec) && !ec) {
                std::filesystem::remove(config_dir, ec);
                if (!ec) {
                    std::cout << utils::Console::success("Config directory removed: " + config_dir.string()) << "\n";
                }
            }
        } else {
            std::cout << utils::Console::info("No configuration file found") << "\n";
        }
        
        // Clean up logs and data directories
        cleanup_directories();
        
        return true;
        
    } catch (const std::exception& e) {
        std::cout << utils::Console::error("Failed to remove configuration: ") << e.what() << "\n";
        return false;
    }
}

void service_manager::cleanup_directories() {
    // Determine if we're cleaning system-wide or user installation
    bool is_root = (geteuid() == 0);
    
    if (is_root) {
        // System-wide installation cleanup
        std::vector<std::filesystem::path> system_dirs = {
            "/var/log/thinr-agent"  // System log directory
        };
        
        for (const auto& dir : system_dirs) {
            std::error_code ec;
            if (std::filesystem::exists(dir, ec) && !ec) {
                std::filesystem::remove_all(dir, ec);
                if (!ec) {
                    std::cout << utils::Console::success("Logs removed: " + dir.string()) << "\n";
                }
            }
        }
    } else {
        // User installation cleanup
        std::filesystem::path home_dir = std::filesystem::path(std::getenv("HOME") ? std::getenv("HOME") : "");
        if (!home_dir.empty()) {
            // First, remove directories with content
            std::vector<std::filesystem::path> dirs_with_content = {
                home_dir / ".local" / "share" / "thinr-agent" / "logs",  // Log directory (may contain log files)
                home_dir / ".cache" / "thinr-agent"                      // Cache directory (may contain cache files)
            };
            
            for (const auto& dir : dirs_with_content) {
                std::error_code ec;
                if (std::filesystem::exists(dir, ec) && !ec) {
                    std::filesystem::remove_all(dir, ec); // Remove directory and all its contents
                    if (!ec) {
                        std::cout << utils::Console::success("Logs removed: " + dir.string()) << "\n";
                    }
                }
            }
            
            // Then, try to remove parent directories if they're empty
            std::vector<std::filesystem::path> parent_dirs = {
                home_dir / ".local" / "share" / "thinr-agent",  // Working directory
                home_dir / ".cache" / "thinr-agent"              // Cache directory (in case it wasn't removed above)
            };
            
            for (const auto& dir : parent_dirs) {
                std::error_code ec;
                if (std::filesystem::exists(dir, ec) && !ec) {
                    if (std::filesystem::is_empty(dir, ec) && !ec) {
                        std::filesystem::remove(dir, ec);
                        if (!ec) {
                            std::cout << utils::Console::success("Directory removed: " + dir.string()) << "\n";
                        }
                    }
                }
            }
        }
    }
}

bool service_manager::start_service() {
    std::cout << utils::Console::loading("Starting service...") << "\n";
    if (service_installer_.start_service()) {
        std::cout << utils::Console::success("Service started successfully") << "\n";
        return true;
    } else {
        std::cout << utils::Console::error("Failed to start service") << "\n";
        return true;
    }
}

bool service_manager::stop_service() {
    std::cout << utils::Console::loading("Stopping service...") << "\n";
    if (service_installer_.stop_service()) {
        std::cout << utils::Console::success("Service stopped successfully") << "\n";
        return true;
    } else {
        std::cout << utils::Console::error("Failed to stop service") << "\n";
        return true;
    }
}

bool service_manager::restart_service() {
    std::cout << utils::Console::loading("Restarting service...") << "\n";
    if (service_installer_.stop_service() && service_installer_.start_service()) {
        std::cout << utils::Console::success("Service restarted successfully") << "\n";
        return true;
    } else {
        std::cout << utils::Console::error("Failed to restart service") << "\n";
        return true;
    }
}

bool service_manager::view_logs() {
    std::cout << "\nService logs:\n";

    std::string service_name = installer::InstallationConfig::SERVICE_NAME;

    // Try platform-specific log viewers in order
    if (std::filesystem::exists("/bin/logread") || std::filesystem::exists("/sbin/logread")) {
        // OpenWrt/procd — logread with grep for our service
        std::string cmd = "logread | grep -i " + service_name + " | tail -30";
        system(cmd.c_str());
    } else if (std::filesystem::exists("/bin/journalctl") || std::filesystem::exists("/usr/bin/journalctl")) {
        // systemd — journalctl
        std::string cmd = "journalctl -u " + service_name + " -n 30 --no-pager";
        system(cmd.c_str());
    } else {
        // Fallback — check /var/log for service log files
        std::string log_file = "/var/log/" + service_name + ".log";
        if (std::filesystem::exists(log_file)) {
            std::string cmd = "tail -30 " + log_file;
            system(cmd.c_str());
        } else {
            std::cout << "No supported log viewer found.\n";
            std::cout << "Try checking /var/log/syslog or /var/log/messages manually.\n";
        }
    }

    return true; // Return to menu
}

installer::service_installer::ServiceStatus service_manager::get_service_status() const {
    // Need to cast away const since get_service_status is not const in service_installer
    return const_cast<installer::service_installer&>(service_installer_).get_service_status();
}

bool service_manager::is_interactive_terminal() const {
    return isatty(STDIN_FILENO) && isatty(STDOUT_FILENO);
}

std::string service_manager::get_installed_version() const {
    bool is_root = (geteuid() == 0);
    std::string binary_path = const_cast<installer::service_installer&>(service_installer_).get_binary_install_path(is_root);

    if (!std::filesystem::exists(binary_path)) {
        return {};
    }

    // Run the installed binary with --version and capture output
    std::string cmd = binary_path + " --version 2>/dev/null";
    std::array<char, 256> buffer{};
    std::string output;

    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return {};

    while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
        output += buffer.data();
    }
    pclose(pipe);

    // Parse "thinr-agent version <VERSION>\n"
    const std::string prefix = "thinr-agent version ";
    auto pos = output.find(prefix);
    if (pos == std::string::npos) return {};

    std::string version = output.substr(pos + prefix.size());
    // Trim trailing whitespace/newlines
    while (!version.empty() && (version.back() == '\n' || version.back() == '\r' || version.back() == ' ')) {
        version.pop_back();
    }
    return version;
}

bool service_manager::has_update_available() const {
    std::string installed = get_installed_version();
    if (installed.empty()) return false;
    return installed != std::string(AGENT_VERSION);
}

bool service_manager::update_service() {
    bool is_root = (geteuid() == 0);
    std::string installed_version = get_installed_version();
    std::string current_version = AGENT_VERSION;

    std::cout << utils::Console::cyan("Update details:") << "\n";
    std::cout << "  Installed: " << (installed_version.empty() ? "unknown" : installed_version) << "\n";
    std::cout << "  New:       " << current_version << "\n\n";

    if (!utils::Console::confirm("Proceed with update?", true)) {
        std::cout << utils::Console::cyan("Update cancelled.") << "\n";
        return true;
    }

    // Step 1: Stop service
    std::cout << utils::Console::loading("Stopping service...") << "\n";
    if (!service_installer_.stop_service()) {
        std::cout << utils::Console::error("Failed to stop service") << "\n";
        return true;
    }
    std::cout << utils::Console::success("Service stopped") << "\n";

    // Step 2: Copy current binary to install path
    std::string target_path = service_installer_.get_binary_install_path(is_root);
    std::string current_binary;

    // Get current binary path from /proc/self/exe or argv[0]
    try {
        current_binary = std::filesystem::read_symlink("/proc/self/exe").string();
    } catch (...) {
        // macOS fallback: use _NSGetExecutablePath or find from PATH
        // For now, use the program invocation
    }

#ifdef __APPLE__
    if (current_binary.empty()) {
        // macOS: use _NSGetExecutablePath
        char path[4096];
        uint32_t size = sizeof(path);
        if (_NSGetExecutablePath(path, &size) == 0) {
            current_binary = std::filesystem::canonical(path).string();
        }
    }
#endif

    if (current_binary.empty()) {
        std::cout << utils::Console::error("Could not determine current binary path") << "\n";
        // Try to restart the service with old binary
        service_installer_.start_service();
        return true;
    }

    // Don't copy if source and target are the same file
    if (std::filesystem::equivalent(current_binary, target_path)) {
        std::cout << utils::Console::info("Binary is already at the install path, no copy needed") << "\n";
    } else {
        std::cout << utils::Console::loading("Installing new binary...") << "\n";
        try {
            std::filesystem::copy_file(current_binary, target_path,
                                       std::filesystem::copy_options::overwrite_existing);
            std::filesystem::permissions(target_path,
                                         std::filesystem::perms::owner_all |
                                         std::filesystem::perms::group_read |
                                         std::filesystem::perms::group_exec |
                                         std::filesystem::perms::others_read |
                                         std::filesystem::perms::others_exec);
            std::cout << utils::Console::success("Binary updated: " + target_path) << "\n";
        } catch (const std::exception& e) {
            std::cout << utils::Console::error("Failed to copy binary: ") << e.what() << "\n";
            // Try to restart the service with old binary
            std::cout << utils::Console::loading("Restarting service with previous binary...") << "\n";
            service_installer_.start_service();
            return true;
        }
    }

    // Step 3: Start service
    std::cout << utils::Console::loading("Starting service...") << "\n";
    if (service_installer_.start_service()) {
        std::cout << "\n" << utils::Console::success("Service updated successfully!", false) << "\n";
    } else {
        std::cout << utils::Console::error("Failed to start service after update") << "\n";
        std::cout << utils::Console::warning("Check logs for details") << "\n";
    }

    return true;
}

} // namespace thinr::cli