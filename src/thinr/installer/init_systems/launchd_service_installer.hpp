#pragma once

#ifndef THINR_LAUNCHD_SERVICE_INSTALLER_HPP
#define THINR_LAUNCHD_SERVICE_INSTALLER_HPP

#include "../base_service_installer.hpp"

namespace thinr::installer {

class LaunchdServiceInstaller : public BaseServiceInstaller {
public:
    LaunchdServiceInstaller() = default;
    ~LaunchdServiceInstaller() = default;
    
protected:
    // Implement pure virtual methods
    std::string get_service_file_path(bool system_wide) override;
    bool install_service_impl(bool system_wide) override;
    bool uninstall_service_impl(bool system_wide) override;
    bool start_service_impl() override;
    bool stop_service_impl() override;
    ServiceStatus check_service_status_impl(bool system_wide) override;
    std::string generate_service_file(bool system_wide) override;
    
    std::string get_init_system_name() override { return "launchd"; }
    
private:
    std::string get_launchd_identifier() const;
};

} // namespace thinr::installer

#endif // THINR_LAUNCHD_SERVICE_INSTALLER_HPP