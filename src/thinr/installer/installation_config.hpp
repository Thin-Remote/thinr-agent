#pragma once

#ifndef THINREMOTE_INSTALLATION_CONFIG_HPP
#define THINREMOTE_INSTALLATION_CONFIG_HPP

#include <string>
#include "../config/config_manager.hpp"

namespace thinr::installer {

class InstallationConfig {
public:
    // Service configuration constants
    static constexpr const char* SERVICE_NAME = "thinr-agent";
    static constexpr const char* SERVICE_DESCRIPTION = "ThinRemote Agent";
    static constexpr const char* BINARY_NAME = "thinr-agent";
    
    // Domain and identifiers
    static constexpr const char* APP_DOMAIN = "io.thinremote.agent";
    
    // Paths
    static constexpr const char* SYSTEM_CONFIG_PATH = "/etc/thinr-agent/config.json";
    static constexpr const char* USER_CONFIG_PATH = "~/.config/thinr-agent/config.json";
    
    InstallationConfig();
    
    // Service identifiers
    std::string get_service_identifier() const;
    std::string get_launchd_identifier() const;
    
    // Binary paths
    std::string get_binary_install_path(bool system_wide) const;
    
    // Configuration paths (using ConfigManager logic)
    std::string get_config_path() const;
    std::string get_config_path(bool system_wide) const;
    
    // Service file paths
    std::string get_systemd_service_path(bool system_wide) const;
    std::string get_launchd_plist_path(bool system_wide) const;
    
    // Log directories
    std::string get_log_directory(bool system_wide) const;
    
    // Working directories
    std::string get_working_directory(bool system_wide) const;
    
    // Utility methods
    std::string get_home_directory() const;
    
private:
    std::string expand_path(const std::string& path) const;
};

} // namespace thinr::installer

#endif // THINREMOTE_INSTALLATION_CONFIG_HPP