#include "base_service_installer.hpp"
#include "../utils/console.hpp"
#include <unistd.h>
#include <pwd.h>
#include <filesystem>
#include <iostream>
#include <cstdlib>
#include <spdlog/spdlog.h>

#ifdef __APPLE__
#include <mach-o/dyld.h>
#include <limits.h>
#endif

namespace thinr::installer {

base_service_installer::base_service_installer() {
}

bool base_service_installer::is_running_as_root() const {
    return getuid() == 0;
}

bool base_service_installer::can_install_system_service() const {
    return is_running_as_root();
}

base_service_installer::InstallMode base_service_installer::get_recommended_install_mode() {
    if (can_install_system_service()) {
        return InstallMode::SYSTEM;
    }
    return InstallMode::USER;
}

std::string base_service_installer::get_binary_install_path(bool system_wide) {
    return config_.get_binary_install_path(system_wide);
}

std::string base_service_installer::get_config_path() {
    return config_.get_config_path();
}

std::string base_service_installer::get_service_file_path_public(bool system_wide) {
    return get_service_file_path(system_wide);
}

std::string base_service_installer::get_current_binary_path() {
    // Try to get the path of the current executable
    char path[1024];
    
#ifdef __APPLE__
    // macOS: use _NSGetExecutablePath
    uint32_t size = sizeof(path);
    if (_NSGetExecutablePath(path, &size) == 0) {
        // Resolve to absolute path
        char resolved_path[PATH_MAX];
        if (realpath(path, resolved_path) != nullptr) {
            return std::string(resolved_path);
        }
        return std::string(path);
    }
#else
    // Linux: use /proc/self/exe
    ssize_t count = readlink("/proc/self/exe", path, sizeof(path) - 1);
    if (count != -1) {
        path[count] = '\0';
        return std::string(path);
    }
#endif
    
    // Fallback: assume we're in the current directory
    return "./thinr-agent";
}

std::string base_service_installer::get_username() {
    const char* user = getenv("USER");
    if (user) {
        return std::string(user);
    }
    
    struct passwd* pw = getpwuid(getuid());
    if (pw && pw->pw_name) {
        return std::string(pw->pw_name);
    }
    
    return "unknown";
}

std::string base_service_installer::get_home_directory() {
    return config_.get_home_directory();
}

bool base_service_installer::execute_system_command(const std::string& command, const std::string& operation_desc) {
    int result = std::system(command.c_str());
    
    if (result != 0) {
        if (!operation_desc.empty()) {
            spdlog::debug("{} failed with exit code: {}", operation_desc, result);
        } else {
            spdlog::debug("Command failed with exit code: {} - {}", result, command);
        }
        return false;
    }
    return true;
}

bool base_service_installer::copy_binary_to_install_path(bool system_wide) {
    std::string current_binary = get_current_binary_path();
    std::string target_binary = get_binary_install_path(system_wide);
    
    try {
        // Ensure target directory exists
        std::filesystem::path target_dir = std::filesystem::path(target_binary).parent_path();
        if (!std::filesystem::exists(target_dir)) {
            std::filesystem::create_directories(target_dir);
            spdlog::info("Created binary directory: {}", target_dir.string());
        }
        
        std::filesystem::copy_file(current_binary, target_binary, 
                                   std::filesystem::copy_options::overwrite_existing);
        std::filesystem::permissions(target_binary, 
                                     std::filesystem::perms::owner_all | 
                                     std::filesystem::perms::group_read | 
                                     std::filesystem::perms::group_exec |
                                     std::filesystem::perms::others_read | 
                                     std::filesystem::perms::others_exec);
        spdlog::info("Copied binary to {}", target_binary);
        std::cout << utils::Console::success("Binary installed: " + target_binary) << "\n";
        return true;
    } catch (const std::exception& e) {
        spdlog::error("Failed to copy binary: {}", e.what());
        return false;
    }
}

bool base_service_installer::install_service(InstallMode mode) {
    InstallMode actual_mode = mode;
    if (mode == InstallMode::AUTO) {
        actual_mode = get_recommended_install_mode();
    }
    
    bool system_wide = (actual_mode == InstallMode::SYSTEM);
    
    // Check if this init system supports user services
    if (!system_wide && !can_install_user_service()) {
        spdlog::warn("{} user service bus not available, switching to system mode", get_init_system_name());
        system_wide = true;
        if (!can_install_system_service()) {
            spdlog::error("Cannot install system service without root privileges");
            std::cout << utils::Console::error("User service bus not available and not running as root") << "\n";
            std::cout << utils::Console::info("Install as root using:") << "\n";
            std::cout << utils::Console::info("  curl -fsSL https://get.thinremote.io/install.sh | sudo sh") << "\n";
            return false;
        }
    }
    
    spdlog::info("Installing {} service in {} mode using {}", 
                 config_.get_service_identifier(), system_wide ? "system" : "user", get_init_system_name());
    
    // Copy binary to installation path
    if (!copy_binary_to_install_path(system_wide)) {
        return false;
    }
    
    // Install service using init-specific implementation
    try {
        bool result = install_service_impl(system_wide);
        if (result) {
            spdlog::info("{} service installed successfully", get_init_system_name());
        } else {
            spdlog::error("Failed to install {} service", get_init_system_name());
        }
        return result;
    } catch (const std::exception& e) {
        spdlog::error("Failed to install {} service: {}", get_init_system_name(), e.what());
        return false;
    }
}

bool base_service_installer::uninstall_service() {
    spdlog::info("Uninstalling {} service using {}", config_.get_service_identifier(), get_init_system_name());
    
    // Only try to uninstall based on current privileges
    bool success = true;
    bool is_root = (geteuid() == 0);
    try {
        // If running as root, only try system uninstall
        // If running as user, only try user uninstall
        if (is_root) {
            success = uninstall_service_impl(true);   // system
        } else if (can_install_user_service()) {
            success = uninstall_service_impl(false);  // user
        } else {
            std::cout << utils::Console::error("Cannot uninstall: user services not supported by " + get_init_system_name()) << "\n";
            success = false;
        }
        
        if (success) {
            spdlog::info("{} service uninstalled successfully", get_init_system_name());
        }
    } catch (const std::exception& e) {
        spdlog::error("Failed to uninstall {} service: {}", get_init_system_name(), e.what());
        success = false;
    }
    
    // Remove binaries based on current privileges
    std::string binary_path = get_binary_install_path(is_root);
    
    try {
        if (std::filesystem::exists(binary_path)) {
            std::filesystem::remove(binary_path);
            spdlog::info("Removed binary: {}", binary_path);
            std::cout << utils::Console::success("Binary removed: " + binary_path) << "\n";
        }
    } catch (const std::exception& e) {
        spdlog::warn("Failed to remove binary: {}", e.what());
        std::cout << utils::Console::error("Failed to remove binary: " + binary_path) << "\n";
        if (!is_root) {
            std::cout << utils::Console::info("You may need to run as root to remove system-wide installations") << "\n";
        }
        success = false;
    }
    
    return success;
}

base_service_installer::ServiceStatus base_service_installer::get_service_status() {
    // Check both system and user installations
    bool system_wide = can_install_system_service();
    
    try {
        ServiceStatus status = check_service_status_impl(system_wide);
        if (status == ServiceStatus::NOT_INSTALLED && system_wide && can_install_user_service()) {
            // If system check failed and we have system permissions, try user
            status = check_service_status_impl(false);
        }
        return status;
    } catch (const std::exception& e) {
        spdlog::error("Failed to check {} service status: {}", get_init_system_name(), e.what());
        return ServiceStatus::UNKNOWN;
    }
}

base_service_installer::ServiceStatus base_service_installer::check_service_status(bool system_wide) {
    try {
        return check_service_status_impl(system_wide);
    } catch (const std::exception& e) {
        spdlog::error("Failed to check {} service status: {}", get_init_system_name(), e.what());
        return ServiceStatus::UNKNOWN;
    }
}

bool base_service_installer::start_service() {
    spdlog::info("Starting {} service using {}", config_.get_service_identifier(), get_init_system_name());
    try {
        bool result = start_service_impl();
        if (result) {
            spdlog::info("{} service started successfully", get_init_system_name());
        } else {
            spdlog::error("Failed to start {} service", get_init_system_name());
        }
        return result;
    } catch (const std::exception& e) {
        spdlog::error("Failed to start {} service: {}", get_init_system_name(), e.what());
        return false;
    }
}

bool base_service_installer::stop_service() {
    spdlog::info("Stopping {} service using {}", config_.get_service_identifier(), get_init_system_name());
    try {
        bool result = stop_service_impl();
        if (result) {
            spdlog::info("{} service stopped successfully", get_init_system_name());
        } else {
            spdlog::error("Failed to stop {} service", get_init_system_name());
        }
        return result;
    } catch (const std::exception& e) {
        spdlog::error("Failed to stop {} service: {}", get_init_system_name(), e.what());
        return false;
    }
}

} // namespace thinr::installer