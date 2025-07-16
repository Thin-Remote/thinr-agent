#pragma once

#ifndef THINR_SYSV_SERVICE_INSTALLER_HPP
#define THINR_SYSV_SERVICE_INSTALLER_HPP

#include "../base_service_installer.hpp"
#include <thread>
#include <chrono>

namespace thinr::installer {

class SysVServiceInstaller : public BaseServiceInstaller {
public:
    SysVServiceInstaller() = default;
    ~SysVServiceInstaller() = default;
    
protected:
    // Implement pure virtual methods
    std::string get_service_file_path(bool system_wide) override;
    bool install_service_impl(bool system_wide) override;
    bool uninstall_service_impl(bool system_wide) override;
    bool start_service_impl() override;
    bool stop_service_impl() override;
    ServiceStatus check_service_status_impl(bool system_wide) override;
    std::string generate_service_file(bool system_wide) override;
    
    std::string get_init_system_name() override { return "SysV init"; }
    bool can_install_user_service() override { return false; } // SysV only supports system-wide
    
private:
    bool enable_service_runlevels(const std::string& service_name);
    bool disable_service_runlevels(const std::string& service_name);
    bool modify_rc_conf(const std::string& service_name, bool enable);
    void remove_runlevel_symlinks(const std::string& service_name);
    bool kill_process_from_pid_file(const std::string& pid_file);
};

} // namespace thinr::installer

#endif // THINR_SYSV_SERVICE_INSTALLER_HPP