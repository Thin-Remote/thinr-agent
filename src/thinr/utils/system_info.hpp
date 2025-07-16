#pragma once

#ifndef THINR_SYSTEM_INFO_HPP
#define THINR_SYSTEM_INFO_HPP

#include <string>

namespace thinr::utils {

class SystemInfo {
public:
    // Get OS description (e.g., "Ubuntu 22.04 LTS", "macOS 14.2 Sonoma")
    static std::string get_os_description();
    
private:
    // Platform-specific implementations
    static std::string get_linux_description();
    static std::string get_macos_description();
    static std::string get_macos_version_name(const std::string& version);
};

} // namespace thinr::utils

#endif // THINR_SYSTEM_INFO_HPP