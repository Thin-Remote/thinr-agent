#pragma once

#ifndef THINR_OPENRC_SERVICE_INSTALLER_HPP
#define THINR_OPENRC_SERVICE_INSTALLER_HPP

#include "../base_service_installer.hpp"

namespace thinr::installer {

class openrc_service_installer : public base_service_installer {
public:
    openrc_service_installer() = default;
    ~openrc_service_installer() = default;
    
protected:
    // Implement pure virtual methods
    std::string get_service_file_path(bool system_wide) override;
    bool install_service_impl(bool system_wide) override;
    bool uninstall_service_impl(bool system_wide) override;
    bool start_service_impl() override;
    bool stop_service_impl() override;
    ServiceStatus check_service_status_impl(bool system_wide) override;
    std::string generate_service_file(bool system_wide) override;
    
    std::string get_init_system_name() override { return "OpenRC"; }
    bool can_install_user_service() override { return false; } // OpenRC only supports system-wide
};

} // namespace thinr::installer

#endif // THINR_OPENRC_SERVICE_INSTALLER_HPP