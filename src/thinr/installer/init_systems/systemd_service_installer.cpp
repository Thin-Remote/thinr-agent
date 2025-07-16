#include "systemd_service_installer.hpp"
#include "../../utils/console.hpp"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <iostream>
#include <spdlog/spdlog.h>

namespace thinr::installer {

std::string SystemdServiceInstaller::get_service_file_path(bool system_wide) {
    return config_.get_systemd_service_path(system_wide);
}

std::string SystemdServiceInstaller::get_systemctl_command(bool system_wide) const {
    return system_wide ? "systemctl" : "systemctl --user";
}

std::string SystemdServiceInstaller::get_systemctl_command_for_current_context() const {
    // Check if we need system or user mode based on current permissions
    bool system_wide = can_install_system_service();
    return get_systemctl_command(system_wide);
}

std::string SystemdServiceInstaller::get_service_name() const {
    return config_.get_service_identifier() + ".service";
}

bool SystemdServiceInstaller::install_service_impl(bool system_wide) {
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
    
    return true;
}

bool SystemdServiceInstaller::uninstall_service_impl(bool system_wide) {
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
    
    return true;
}

bool SystemdServiceInstaller::start_service_impl() {
    std::string systemctl_cmd = get_systemctl_command_for_current_context();
    std::string start_cmd = systemctl_cmd + " start " + get_service_name();
    return execute_system_command(start_cmd, "systemctl start");
}

bool SystemdServiceInstaller::stop_service_impl() {
    std::string systemctl_cmd = get_systemctl_command_for_current_context();
    std::string stop_cmd = systemctl_cmd + " stop " + get_service_name();
    return execute_system_command(stop_cmd, "systemctl stop");
}

SystemdServiceInstaller::ServiceStatus SystemdServiceInstaller::check_service_status_impl(bool system_wide) {
    std::string service_file_path = get_service_file_path(system_wide);
    
    // Check if service file exists
    if (!std::filesystem::exists(service_file_path)) {
        return ServiceStatus::NOT_INSTALLED;
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

std::string SystemdServiceInstaller::generate_service_file(bool system_wide) {
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

} // namespace thinr::installer