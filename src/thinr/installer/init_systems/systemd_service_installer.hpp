#pragma once

#ifndef THINR_SYSTEMD_SERVICE_INSTALLER_HPP
#define THINR_SYSTEMD_SERVICE_INSTALLER_HPP

#include "../base_service_installer.hpp"

namespace thinr::installer {

class SystemdServiceInstaller : public BaseServiceInstaller {
public:
    SystemdServiceInstaller() = default;
    ~SystemdServiceInstaller() = default;
    
protected:
    // Implement pure virtual methods
    std::string get_service_file_path(bool system_wide) override;
    bool install_service_impl(bool system_wide) override;
    bool uninstall_service_impl(bool system_wide) override;
    bool start_service_impl() override;
    bool stop_service_impl() override;
    ServiceStatus check_service_status_impl(bool system_wide) override;
    std::string generate_service_file(bool system_wide) override;
    
    std::string get_init_system_name() override { return "systemd"; }
    
private:
    std::string get_systemctl_command(bool system_wide) const;
    std::string get_systemctl_command_for_current_context() const;
    std::string get_service_name() const;
};

} // namespace thinr::installer

#endif // THINR_SYSTEMD_SERVICE_INSTALLER_HPP