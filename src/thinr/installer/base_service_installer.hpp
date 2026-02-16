#pragma once

#ifndef THINR_BASE_SERVICE_INSTALLER_HPP
#define THINR_BASE_SERVICE_INSTALLER_HPP

#include <string>
#include <filesystem>
#include "installation_config.hpp"

namespace thinr::installer {

class base_service_installer {
public:
    enum class InstallMode {
        SYSTEM,     // Install system-wide (requires root)
        USER,       // Install for current user only
        AUTO        // Auto-detect based on permissions
    };

    enum class ServiceStatus {
        NOT_INSTALLED,
        INSTALLED_STOPPED,
        INSTALLED_RUNNING,
        UNKNOWN
    };

    base_service_installer();
    virtual ~base_service_installer() = default;

    // Public interface - Template Method pattern
    bool install_service(InstallMode mode = InstallMode::AUTO);
    bool uninstall_service();
    bool start_service();
    bool stop_service();
    ServiceStatus get_service_status();
    ServiceStatus check_service_status(bool system_wide);

    // Permission detection
    bool is_running_as_root() const;
    bool can_install_system_service() const;
    InstallMode get_recommended_install_mode();

    // Path management
    std::string get_binary_install_path(bool system_wide);
    std::string get_config_path();
    std::string get_service_file_path_public(bool system_wide);

protected:
    // Helper method for executing system commands with consistent logging
    bool execute_system_command(const std::string& command, const std::string& operation_desc = "");

    // Pure virtual methods - must be implemented by subclasses
    virtual std::string get_service_file_path(bool system_wide) = 0;
    virtual bool install_service_impl(bool system_wide) = 0;
    virtual bool uninstall_service_impl(bool system_wide) = 0;
    virtual bool start_service_impl() = 0;
    virtual bool stop_service_impl() = 0;
    virtual ServiceStatus check_service_status_impl(bool system_wide) = 0;
    virtual std::string generate_service_file(bool system_wide) = 0;

    // Virtual methods with default implementation
    virtual bool can_install_user_service() { return true; }
    virtual std::string get_init_system_name() = 0;

    // Shared utilities for subclasses
    std::string get_current_binary_path();
    std::string get_username();
    std::string get_home_directory();
    bool copy_binary_to_install_path(bool system_wide);

    InstallationConfig config_;

private:
    bool system_wide_installed_ = false;
    bool user_installed_ = false;
};

} // namespace thinr::installer

#endif // THINR_BASE_SERVICE_INSTALLER_HPP