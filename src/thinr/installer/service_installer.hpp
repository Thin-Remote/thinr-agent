#pragma once

#ifndef THINREMOTE_SERVICE_INSTALLER_HPP
#define THINREMOTE_SERVICE_INSTALLER_HPP

#include <memory>
#include "base_service_installer.hpp"

namespace thinr::installer {

// Facade class that maintains the original API while delegating to the refactored implementation
class service_installer {
public:
    using InstallMode = base_service_installer::InstallMode;
    using ServiceStatus = base_service_installer::ServiceStatus;

    service_installer();

    // Permission detection
    bool is_running_as_root();
    bool can_install_system_service();
    InstallMode get_recommended_install_mode();

    // Service management
    bool install_service(InstallMode mode = InstallMode::AUTO);
    bool uninstall_service();
    bool start_service();
    bool stop_service();
    ServiceStatus get_service_status();
    ServiceStatus get_service_status_for_privilege(bool system_wide);

    // Path management
    std::string get_binary_install_path(bool system_wide);
    std::string get_config_path();
    std::string get_service_file_path(bool system_wide);

private:
    std::unique_ptr<base_service_installer> impl_;
};

} // namespace thinr::installer

#endif // THINREMOTE_SERVICE_INSTALLER_HPP