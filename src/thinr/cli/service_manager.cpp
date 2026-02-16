#include "service_manager.hpp"
#include "../utils/console.hpp"
#include <iostream>
#include <thread>
#include <chrono>
#include <filesystem>
#include <unistd.h>
#include <spdlog/spdlog.h>

namespace thinr::cli {

service_manager::service_manager() = default;

bool service_manager::show_management_menu() {
    auto status = service_installer_.get_service_status();
    
    utils::Console::printSectionHeader("Service Management", "📊");
    show_service_status();
    
    if (status == installer::service_installer::ServiceStatus::INSTALLED_RUNNING) {
        std::vector<std::string> options = {
            "View service logs",
            "Restart service", 
            "Stop service and run manually",
            "Uninstall service and remove configuration",
            "Exit"
        };
        
        int choice = utils::Console::getUserChoice(options, "What would you like to do?");
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
    
    std::cout << utils::Console::cyan("Service status: ") << status_text << "\n\n";
}


bool service_manager::handle_running_service_choice(int choice) {
    switch (choice) {
        case 1: // View logs
            return view_logs();
            
        case 2: // Restart service
            return restart_service();
            
        case 3: // Stop and run manually
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
            
        case 4: // Uninstall
            return uninstall_completely();
            
        case 5: // Exit
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
    // TODO: Implement log viewing
    system("journalctl -u thin-remote -n 20 --no-pager || launchctl log show --predicate 'subsystem == \"io.thinremote.agent\"' --last 1h");
    return true; // Return to exit
}

installer::service_installer::ServiceStatus service_manager::get_service_status() const {
    // Need to cast away const since get_service_status is not const in service_installer
    return const_cast<installer::service_installer&>(service_installer_).get_service_status();
}

bool service_manager::is_interactive_terminal() const {
    return isatty(STDIN_FILENO) && isatty(STDOUT_FILENO);
}


} // namespace thinr::cli