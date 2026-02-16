#include "service_installer.hpp"
#include "service_installer_factory.hpp"
#include <spdlog/spdlog.h>

namespace thinr::installer {

service_installer::service_installer() {
    try {
        impl_ = service_installer_factory::create();
    } catch (const std::exception& e) {
        spdlog::error("Failed to create service installer: {}", e.what());
        throw;
    }
}

bool service_installer::is_running_as_root() {
    return impl_->is_running_as_root();
}

bool service_installer::can_install_system_service() {
    return impl_->can_install_system_service();
}

service_installer::InstallMode service_installer::get_recommended_install_mode() {
    return impl_->get_recommended_install_mode();
}

std::string service_installer::get_binary_install_path(bool system_wide) {
    return impl_->get_binary_install_path(system_wide);
}

std::string service_installer::get_config_path() {
    return impl_->get_config_path();
}

std::string service_installer::get_service_file_path(bool system_wide) {
    return impl_->get_service_file_path_public(system_wide);
}

bool service_installer::install_service(InstallMode mode) {
    return impl_->install_service(mode);
}

bool service_installer::uninstall_service() {
    return impl_->uninstall_service();
}

service_installer::ServiceStatus service_installer::get_service_status() {
    return impl_->get_service_status();
}

bool service_installer::start_service() {
    return impl_->start_service();
}

bool service_installer::stop_service() {
    return impl_->stop_service();
}

service_installer::ServiceStatus service_installer::get_service_status_for_privilege(bool system_wide) {
    // Use the public check_service_status method
    return impl_->check_service_status(system_wide);
}

} // namespace thinr::installer