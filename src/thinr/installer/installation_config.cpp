#include "installation_config.hpp"
#include <filesystem>
#include <cstdlib>
#include <pwd.h>
#include <unistd.h>
#include <spdlog/spdlog.h>

namespace thinr::installer {

InstallationConfig::InstallationConfig() {
}

std::string InstallationConfig::get_service_identifier() const {
    return SERVICE_NAME;
}

std::string InstallationConfig::get_launchd_identifier() const {
    return std::string(APP_DOMAIN);
}

std::string InstallationConfig::get_binary_install_path(bool system_wide) const {
    if (system_wide) {
        // Check if /usr/local/bin is in PATH
        const char* path_env = std::getenv("PATH");
        if (path_env != nullptr) {
            std::string path(path_env);
            // Check if /usr/local/bin is in PATH
            if (path.find("/usr/local/bin") != std::string::npos) {
                // /usr/local/bin is in PATH, prefer it
                return std::string("/usr/local/bin/") + BINARY_NAME;
            }
        }
        
        // /usr/local/bin not in PATH, use /usr/bin instead
        spdlog::debug("/usr/local/bin not found in PATH, using /usr/bin for installation");
        return std::string("/usr/bin/") + BINARY_NAME;
    } else {
        std::string home = get_home_directory();
        std::string local_bin = home + "/.local/bin";
        
        // Create ~/.local/bin if it doesn't exist
        try {
            std::filesystem::create_directories(local_bin);
        } catch (const std::exception&) {
            // Ignore errors, will fail later if needed
        }
        
        return local_bin + "/" + BINARY_NAME;
    }
}

std::string InstallationConfig::get_config_path() const {
    // Use same logic as ConfigManager
    if (getuid() == 0) {
        // Running as root - use system config
        return SYSTEM_CONFIG_PATH;
    } else {
        // Running as user - use user config
        return expand_path(USER_CONFIG_PATH);
    }
}

std::string InstallationConfig::get_config_path(bool system_wide) const {
    if (system_wide) {
        return SYSTEM_CONFIG_PATH;
    } else {
        return expand_path(USER_CONFIG_PATH);
    }
}

std::string InstallationConfig::get_base_directory() const {
    std::filesystem::path config_path(get_config_path());
    return config_path.parent_path().string();
}

std::string InstallationConfig::get_systemd_service_path(bool system_wide) const {
    if (system_wide) {
        return "/etc/systemd/system/" + std::string(SERVICE_NAME) + ".service";
    } else {
        std::string home = get_home_directory();
        std::string user_systemd = home + "/.config/systemd/user";
        try {
            std::filesystem::create_directories(user_systemd);
        } catch (const std::exception&) {
            // Ignore errors
        }
        return user_systemd + "/" + std::string(SERVICE_NAME) + ".service";
    }
}

std::string InstallationConfig::get_launchd_plist_path(bool system_wide) const {
    std::string identifier = get_launchd_identifier();
    
    if (system_wide) {
        return "/Library/LaunchDaemons/" + identifier + ".plist";
    } else {
        std::string home = get_home_directory();
        std::string launch_agents = home + "/Library/LaunchAgents";
        try {
            std::filesystem::create_directories(launch_agents);
        } catch (const std::exception&) {
            // Ignore errors
        }
        return launch_agents + "/" + identifier + ".plist";
    }
}

std::string InstallationConfig::get_log_directory(bool system_wide) const {
    if (system_wide) {
        return "/var/log/" + std::string(SERVICE_NAME);
    } else {
        return get_home_directory() + "/.local/share/" + std::string(SERVICE_NAME) + "/logs";
    }
}

std::string InstallationConfig::get_working_directory(bool system_wide) const {
    if (system_wide) {
        return "/var/log";
    } else {
        return get_home_directory() + "/.local/share/" + std::string(SERVICE_NAME);
    }
}

std::string InstallationConfig::get_home_directory() const {
    const char* home = getenv("HOME");
    if (home) {
        return std::string(home);
    }
    
    // Fallback: get from passwd
    struct passwd* pw = getpwuid(getuid());
    if (pw && pw->pw_dir) {
        return std::string(pw->pw_dir);
    }
    
    return "/tmp"; // Last resort
}

std::string InstallationConfig::expand_path(const std::string& path) const {
    if (path.empty() || path[0] != '~') {
        return path;
    }
    
    std::string home = get_home_directory();
    return home + path.substr(1);
}

} // namespace thinr::installer