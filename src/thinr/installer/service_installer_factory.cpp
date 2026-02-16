#include "service_installer_factory.hpp"
#include "init_systems/systemd_service_installer.hpp"
#include "init_systems/launchd_service_installer.hpp"
#include "init_systems/sysv_service_installer.hpp"
#include "init_systems/openrc_service_installer.hpp"
#include "init_systems/upstart_service_installer.hpp"
#include <sys/utsname.h>
#include <filesystem>
#include <spdlog/spdlog.h>

namespace thinr::installer {

std::unique_ptr<base_service_installer> service_installer_factory::create() {
    std::string init_system = detect_init_system();
    spdlog::debug("Detected init system: {}", init_system);

    if (init_system == "systemd") {
        return std::make_unique<systemd_service_installer>();
    } else if (init_system == "launchd") {
        return std::make_unique<launchd_service_installer>();
    } else if (init_system == "OpenRC") {
        return std::make_unique<openrc_service_installer>();
    } else if (init_system == "SysV init") {
        return std::make_unique<sysv_service_installer>();
    } else if (init_system == "Upstart") {
        return std::make_unique<upstart_service_installer>();
    } else {
        throw std::runtime_error("Unsupported init system: " + init_system);
    }
}

std::string service_installer_factory::detect_init_system() {
    // Check if running on macOS (Darwin) - uses launchd
    struct utsname system_info;
    if (uname(&system_info) == 0) {
        std::string sysname = system_info.sysname;
        if (sysname == "Darwin") {
            return "launchd";
        }
    }
    
    // Check for systemd (most common modern init system)
    if (std::filesystem::exists("/run/systemd/system") || 
        std::filesystem::exists("/systemd")) {
        return "systemd";
    }
    
    // Check for OpenRC (Alpine Linux, Gentoo)
    if (std::filesystem::exists("/sbin/openrc") || 
        (std::filesystem::exists("/etc/init.d/") && std::filesystem::exists("/sbin/rc-service"))) {
        return "OpenRC";
    }
    
    // Check for Upstart (older Ubuntu versions)
    if (std::filesystem::exists("/sbin/upstart") || 
        (std::filesystem::exists("/etc/init/") && std::filesystem::exists("/sbin/initctl"))) {
        return "Upstart";
    }
    
    // Check for SysV init (traditional Unix init)
    if (std::filesystem::exists("/etc/inittab") || 
        std::filesystem::exists("/etc/rc.d/") || 
        std::filesystem::exists("/etc/init.d/")) {
        return "SysV init";
    }
    
    return "Unknown";
}

} // namespace thinr::installer