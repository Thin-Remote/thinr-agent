#include "service_installer.hpp"
#include "service_installer_factory.hpp"
#include <spdlog/spdlog.h>

namespace thinr::installer {

ServiceInstaller::ServiceInstaller() {
    try {
        impl_ = ServiceInstallerFactory::create();
    } catch (const std::exception& e) {
        spdlog::error("Failed to create service installer: {}", e.what());
        throw;
    }
}

bool ServiceInstaller::is_running_as_root() {
    return impl_->is_running_as_root();
}

bool ServiceInstaller::can_install_system_service() {
    return impl_->can_install_system_service();
}

ServiceInstaller::InstallMode ServiceInstaller::get_recommended_install_mode() {
    return impl_->get_recommended_install_mode();
}

std::string ServiceInstaller::get_binary_install_path(bool system_wide) {
    return impl_->get_binary_install_path(system_wide);
}

std::string ServiceInstaller::get_config_path() {
    return impl_->get_config_path();
}

std::string ServiceInstaller::get_service_file_path(bool system_wide) {
    return impl_->get_service_file_path_public(system_wide);
}

bool ServiceInstaller::install_service(InstallMode mode) {
    return impl_->install_service(mode);
}

bool ServiceInstaller::uninstall_service() {
    return impl_->uninstall_service();
}

ServiceInstaller::ServiceStatus ServiceInstaller::get_service_status() {
    return impl_->get_service_status();
}

bool ServiceInstaller::start_service() {
    return impl_->start_service();
}

bool ServiceInstaller::stop_service() {
    return impl_->stop_service();
}

ServiceInstaller::ServiceStatus ServiceInstaller::get_service_status_for_privilege(bool system_wide) {
    // Use the public check_service_status method
    return impl_->check_service_status(system_wide);
}

} // namespace thinr::installer