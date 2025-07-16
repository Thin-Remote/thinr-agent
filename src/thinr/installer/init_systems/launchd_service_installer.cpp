#include "launchd_service_installer.hpp"
#include "../../utils/console.hpp"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <iostream>
#include <spdlog/spdlog.h>

namespace thinr::installer {

std::string LaunchdServiceInstaller::get_service_file_path(bool system_wide) {
    return config_.get_launchd_plist_path(system_wide);
}

std::string LaunchdServiceInstaller::get_launchd_identifier() const {
    return config_.get_launchd_identifier();
}

bool LaunchdServiceInstaller::install_service_impl(bool system_wide) {
    // Generate plist file content
    std::string plist_content = generate_service_file(system_wide);
    std::string plist_file_path = get_service_file_path(system_wide);
    
    // Create log directory
    std::string log_dir = config_.get_log_directory(system_wide);
    try {
        std::filesystem::create_directories(log_dir);
        std::cout << utils::Console::success("Log directory created: " + log_dir) << "\n";
    } catch (const std::exception& e) {
        spdlog::warn("Failed to create log directory {}: {}", log_dir, e.what());
    }
    
    // Write plist file
    std::ofstream plist_file(plist_file_path);
    if (!plist_file) {
        spdlog::error("Failed to create plist file: {}", plist_file_path);
        return false;
    }
    
    plist_file << plist_content;
    plist_file.close();
    
    if (!plist_file.good()) {
        spdlog::error("Failed to write plist file: {}", plist_file_path);
        return false;
    }
    
    spdlog::info("Created plist file: {}", plist_file_path);
    std::cout << utils::Console::success("Service file created: " + plist_file_path) << "\n";
    
    // Load the service using launchctl
    std::string identifier = get_launchd_identifier();
    std::string load_cmd = system_wide ? 
        "launchctl load " + plist_file_path + " >/dev/null 2>&1" :
        "launchctl load " + plist_file_path + " >/dev/null 2>&1";
    
    if (!execute_system_command(load_cmd, "launchctl load")) {
        spdlog::error("Failed to load launchd service");
        return false;
    }
    
    return true;
}

bool LaunchdServiceInstaller::uninstall_service_impl(bool system_wide) {
    std::string identifier = get_launchd_identifier();
    std::string plist_file_path = get_service_file_path(system_wide);
    
    // Check if plist file exists
    if (!std::filesystem::exists(plist_file_path)) {
        spdlog::debug("Plist file not found: {}", plist_file_path);
        std::cout << utils::Console::info("Service not installed at " + plist_file_path) << "\n";
        return true; // Not installed, so uninstall is successful
    }
    
    // Unload service first
    std::string unload_cmd = "launchctl unload " + plist_file_path + " >/dev/null 2>&1";
    if (execute_system_command(unload_cmd)) {
        std::cout << utils::Console::success("Service disabled: " + identifier) << "\n";
    }
    
    // Remove from launchctl (in case it's still registered)
    std::string remove_cmd = "launchctl remove " + identifier + " >/dev/null 2>&1";
    execute_system_command(remove_cmd);
    
    // Remove plist file
    std::filesystem::remove(plist_file_path);
    spdlog::info("Removed plist file: {}", plist_file_path);
    std::cout << utils::Console::success("Service file removed: " + plist_file_path) << "\n";
    
    return true;
}

bool LaunchdServiceInstaller::start_service_impl() {
    std::string identifier = get_launchd_identifier();
    
    // Try to start the service using launchctl
    std::string start_cmd = "launchctl start " + identifier + " >/dev/null 2>&1";
    
    if (execute_system_command(start_cmd, "launchctl start")) {
        return true;
    } else {
        // If start fails, try loading the plist file instead
        bool system_wide = can_install_system_service();
        std::string plist_file_path = get_service_file_path(system_wide);
        
        if (std::filesystem::exists(plist_file_path)) {
            std::string load_cmd = "launchctl load " + plist_file_path + " >/dev/null 2>&1";
            return execute_system_command(load_cmd, "launchctl load");
        } else {
            return false;
        }
    }
}

bool LaunchdServiceInstaller::stop_service_impl() {
    std::string identifier = get_launchd_identifier();
    
    // Try to stop the service using launchctl
    std::string stop_cmd = "launchctl stop " + identifier + " >/dev/null 2>&1";
    return execute_system_command(stop_cmd, "launchctl stop");
}

LaunchdServiceInstaller::ServiceStatus LaunchdServiceInstaller::check_service_status_impl(bool system_wide) {
    std::string plist_file_path = get_service_file_path(system_wide);
    
    // Check if plist file exists
    if (!std::filesystem::exists(plist_file_path)) {
        return ServiceStatus::NOT_INSTALLED;
    }
    
    // Check service status using launchctl
    std::string identifier = get_launchd_identifier();
    std::string list_cmd = "launchctl list " + identifier + " >/dev/null 2>&1";
    
    // launchctl list returns 0 if service exists, non-zero otherwise
    if (execute_system_command(list_cmd)) {
        // Service is loaded, now check if it's running by checking the PID
        std::string domain = system_wide ? "system/" : "gui/$(id -u)/";
        std::string print_cmd = "launchctl print " + domain + identifier + " 2>/dev/null | grep -q 'state = running'";
        
        if (execute_system_command(print_cmd)) {
            return ServiceStatus::INSTALLED_RUNNING;
        } else {
            return ServiceStatus::INSTALLED_STOPPED;
        }
    } else {
        return ServiceStatus::INSTALLED_STOPPED;
    }
}

std::string LaunchdServiceInstaller::generate_service_file(bool system_wide) {
    std::string binary_path = config_.get_binary_install_path(system_wide);
    std::string config_path = config_.get_config_path(system_wide);
    std::string username = get_username();
    std::string identifier = get_launchd_identifier();
    
    std::ostringstream plist_content;
    plist_content << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    plist_content << "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n";
    plist_content << "<plist version=\"1.0\">\n";
    plist_content << "<dict>\n";
    
    // Service identifier
    plist_content << "    <key>Label</key>\n";
    plist_content << "    <string>" << identifier << "</string>\n";
    
    // Program arguments
    plist_content << "    <key>ProgramArguments</key>\n";
    plist_content << "    <array>\n";
    plist_content << "        <string>" << binary_path << "</string>\n";
    plist_content << "        <string>--config</string>\n";
    plist_content << "        <string>" << config_path << "</string>\n";
    plist_content << "    </array>\n";
    
    // Auto-start and keep alive
    plist_content << "    <key>RunAtLoad</key>\n";
    plist_content << "    <true/>\n";
    plist_content << "    <key>KeepAlive</key>\n";
    plist_content << "    <true/>\n";
    
    // Process type (adaptive for better resource management)
    plist_content << "    <key>ProcessType</key>\n";
    plist_content << "    <string>Adaptive</string>\n";
    
    // User specification for system-wide services
    if (system_wide && !username.empty()) {
        plist_content << "    <key>UserName</key>\n";
        plist_content << "    <string>" << username << "</string>\n";
        plist_content << "    <key>GroupName</key>\n";
        plist_content << "    <string>" << username << "</string>\n";
    }
    
    // Working directory
    std::string working_dir = config_.get_working_directory(system_wide);
    plist_content << "    <key>WorkingDirectory</key>\n";
    plist_content << "    <string>" << working_dir << "</string>\n";
    
    // Standard output and error (logs)
    std::string log_dir = config_.get_log_directory(system_wide);
    plist_content << "    <key>StandardOutPath</key>\n";
    plist_content << "    <string>" << log_dir << "/stdout.log</string>\n";
    plist_content << "    <key>StandardErrorPath</key>\n";
    plist_content << "    <string>" << log_dir << "/stderr.log</string>\n";
    
    // Environment variables (if needed)
    plist_content << "    <key>EnvironmentVariables</key>\n";
    plist_content << "    <dict>\n";
    plist_content << "        <key>PATH</key>\n";
    plist_content << "        <string>/usr/local/bin:/usr/bin:/bin</string>\n";
    plist_content << "    </dict>\n";
    
    // Throttle settings (restart policy)
    plist_content << "    <key>ThrottleInterval</key>\n";
    plist_content << "    <integer>10</integer>\n";
    
    // Network requirement (similar to systemd After=network.target)
    plist_content << "    <key>LaunchOnlyOnce</key>\n";
    plist_content << "    <false/>\n";
    
    plist_content << "</dict>\n";
    plist_content << "</plist>\n";
    
    return plist_content.str();
}

} // namespace thinr::installer