#pragma once

#ifndef THINR_ANDROID_SERVICE_INSTALLER_HPP
#define THINR_ANDROID_SERVICE_INSTALLER_HPP

#include "../base_service_installer.hpp"

namespace thinr::installer {

// On Android the app's Foreground Service owns the agent lifecycle; there is
// no init system to register with. Operations succeed as no-ops so the shared
// CLI paths (version, install, status) work unchanged.
class android_service_installer : public base_service_installer {
protected:
    std::string get_service_file_path(bool) override { return ""; }
    bool install_service_impl(bool) override { return true; }
    bool uninstall_service_impl(bool) override { return true; }
    bool start_service_impl() override { return true; }
    bool stop_service_impl() override { return true; }
    ServiceStatus check_service_status_impl(bool) override { return ServiceStatus::NOT_INSTALLED; }
    std::string generate_service_file(bool) override { return ""; }
    bool can_install_user_service() override { return false; }
    std::string get_init_system_name() override { return "android"; }
};

} // namespace thinr::installer

#endif // THINR_ANDROID_SERVICE_INSTALLER_HPP
