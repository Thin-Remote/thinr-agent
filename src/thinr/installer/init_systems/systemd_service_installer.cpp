#include "systemd_service_installer.hpp"
#include "../../utils/console.hpp"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <iostream>
#include <spdlog/spdlog.h>

namespace thinr::installer {

std::string systemd_service_installer::get_service_file_path(bool system_wide) {
    return config_.get_systemd_service_path(system_wide);
}

std::string systemd_service_installer::get_systemctl_command(bool system_wide) const {
    return system_wide ? "systemctl" : "systemctl --user";
}

std::string systemd_service_installer::get_systemctl_command_for_current_context() const {
    // Check if we need system or user mode based on current permissions
    bool system_wide = can_install_system_service();
    return get_systemctl_command(system_wide);
}

std::string systemd_service_installer::get_service_name() const {
    return config_.get_service_identifier() + ".service";
}

bool systemd_service_installer::install_service_impl(bool system_wide) {
    // Generate service file content
    std::string service_content = generate_service_file(system_wide);
    std::string service_file_path = get_service_file_path(system_wide);
    
    // Write service file
    std::ofstream service_file(service_file_path);
    if (!service_file) {
        spdlog::error("Failed to create service file: {}", service_file_path);
        return false;
    }
    
    service_file << service_content;
    service_file.close();
    
    if (!service_file.good()) {
        spdlog::error("Failed to write service file: {}", service_file_path);
        return false;
    }
    
    spdlog::info("Created service file: {}", service_file_path);
    std::cout << utils::Console::success("Service file created: " + service_file_path) << "\n";
    
    // Reload systemd daemon and enable service
    std::string systemctl_cmd = get_systemctl_command(system_wide);
    
    // Reload daemon
    std::string reload_cmd = systemctl_cmd + " daemon-reload";
    if (!execute_system_command(reload_cmd, "systemctl daemon-reload")) {
        spdlog::warn("Failed to reload systemd daemon");
    }
    
    // Enable service
    std::string enable_cmd = systemctl_cmd + " enable " + get_service_name() + " >/dev/null 2>&1";
    if (!execute_system_command(enable_cmd, "systemctl enable")) {
        spdlog::error("Failed to enable service");
        return false;
    }
    
    // For user services, enable lingering to allow service to start at boot without login
    if (!system_wide) {
        std::string username = get_username();
        std::string linger_cmd = "loginctl enable-linger " + username + " >/dev/null 2>&1";
        
        if (execute_system_command(linger_cmd, "enable lingering")) {
            spdlog::info("Enabled lingering for user {} (service will start at boot)", username);
            std::cout << utils::Console::success("Auto-start at boot enabled for user " + username) << "\n";
        } else {
            spdlog::warn("Could not enable lingering - service won't start automatically at boot");
            std::cout << utils::Console::warning("Service will only run when you're logged in") << "\n";
            std::cout << utils::Console::info("To enable auto-start at boot, run: sudo loginctl enable-linger " + username) << "\n";
            std::cout << utils::Console::info("Alternatively, consider installing service as root") << "\n";
        }
    }
    
    return true;
}

bool systemd_service_installer::uninstall_service_impl(bool system_wide) {
    std::string systemctl_cmd = get_systemctl_command(system_wide);
    std::string service_file_path = get_service_file_path(system_wide);
    
    // Check if service file exists
    if (!std::filesystem::exists(service_file_path)) {
        spdlog::debug("Service file not found: {}", service_file_path);
        std::cout << utils::Console::info("Service file not found: " + service_file_path) << "\n";
        return true; // Not installed, so uninstall is successful
    }
    
    // Stop service if running
    std::string stop_cmd = systemctl_cmd + " stop " + get_service_name() + " >/dev/null 2>&1";
    execute_system_command(stop_cmd); // Don't check result as service may not be running
    
    // Disable service
    std::string disable_cmd = systemctl_cmd + " disable " + get_service_name() + " >/dev/null 2>&1";
    if (execute_system_command(disable_cmd)) {
        std::cout << utils::Console::success("Service disabled: " + get_service_name()) << "\n";
    }
    
    // Remove service file
    std::filesystem::remove(service_file_path);
    spdlog::info("Removed service file: {}", service_file_path);
    std::cout << utils::Console::success("Service file removed: " + service_file_path) << "\n";
    
    // Reload daemon
    std::string reload_cmd = systemctl_cmd + " daemon-reload 2>/dev/null";
    execute_system_command(reload_cmd);
    
    // For user services, optionally disable lingering
    if (!system_wide) {
        std::string username = get_username();
        if (check_lingering_enabled(username)) {
            std::cout << utils::Console::info("Note: User lingering is still enabled for " + username) << "\n";
            std::cout << utils::Console::info("To disable auto-start at boot for all user services, run: loginctl disable-linger " + username) << "\n";
        }
    }
    
    return true;
}

bool systemd_service_installer::start_service_impl() {
    std::string systemctl_cmd = get_systemctl_command_for_current_context();
    std::string start_cmd = systemctl_cmd + " start " + get_service_name();
    return execute_system_command(start_cmd, "systemctl start");
}

bool systemd_service_installer::stop_service_impl() {
    std::string systemctl_cmd = get_systemctl_command_for_current_context();
    std::string stop_cmd = systemctl_cmd + " stop " + get_service_name();
    return execute_system_command(stop_cmd, "systemctl stop");
}

systemd_service_installer::ServiceStatus systemd_service_installer::check_service_status_impl(bool system_wide) {
    std::string service_file_path = get_service_file_path(system_wide);
    
    // Check if service file exists
    if (!std::filesystem::exists(service_file_path)) {
        return ServiceStatus::NOT_INSTALLED;
    }
    
    // For user services, check if lingering is enabled
    if (!system_wide) {
        std::string username = get_username();
        if (!check_lingering_enabled(username)) {
            spdlog::debug("Lingering not enabled for user {} - service won't start at boot", username);
        }
    }
    
    // Check service status using systemctl
    std::string systemctl_cmd = get_systemctl_command(system_wide);
    std::string status_cmd = systemctl_cmd + " is-active " + get_service_name() + " >/dev/null 2>&1";
    
    // systemctl is-active returns 0 if service is active, non-zero otherwise
    if (execute_system_command(status_cmd)) {
        return ServiceStatus::INSTALLED_RUNNING;
    } else {
        return ServiceStatus::INSTALLED_STOPPED;
    }
}

std::string systemd_service_installer::generate_service_file(bool system_wide) {
    std::string binary_path = config_.get_binary_install_path(system_wide);
    std::string config_path = config_.get_config_path(system_wide);
    std::string username = get_username();
    
    std::ostringstream service_content;
    service_content << "[Unit]\n";
    service_content << "Description=" << InstallationConfig::SERVICE_DESCRIPTION << "\n";
    service_content << "After=network.target\n";
    service_content << "Wants=network.target\n";
    service_content << "\n";
    service_content << "[Service]\n";
    service_content << "Type=simple\n";
    service_content << "ExecStart=" << binary_path << " --config " << config_path << "\n";
    service_content << "Restart=always\n";
    service_content << "RestartSec=5\n";
    service_content << "KillMode=process\n";
    service_content << "TimeoutStopSec=30\n";
    
    // Note: We keep the service file simple to avoid compatibility issues
    // User/Group directives are not needed for user services (systemd handles this)
    // Security directives can cause issues on different systems/configurations
    
    service_content << "\n";
    service_content << "[Install]\n";
    service_content << "WantedBy=" << (system_wide ? "multi-user.target" : "default.target") << "\n";
    
    return service_content.str();
}

bool systemd_service_installer::check_lingering_enabled(const std::string& username) const {
    // Check if lingering is enabled for the user
    std::string check_cmd = "loginctl show-user " + username + " --property=Linger 2>/dev/null | grep -q 'Linger=yes'";
    int result = std::system(check_cmd.c_str());
    return result == 0;
}

} // namespace thinr::installer