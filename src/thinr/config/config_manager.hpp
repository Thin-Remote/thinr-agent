#pragma once

#ifndef THINREMOTE_CONFIG_MANAGER_HPP
#define THINREMOTE_CONFIG_MANAGER_HPP

#include <string>
#include <filesystem>
#include <nlohmann/json.hpp>
#include "../installer/installation_config.hpp"

namespace thinr::config {

struct DeviceCredentials {
    std::string host;
    std::string device_id;
    std::string device_name;  // Human-friendly name for the device
    std::string device_user;
    std::string device_token;
    std::string version;
    
    bool is_valid() const {
        return !host.empty() && !device_id.empty() && 
               !device_user.empty() && !device_token.empty();
    }
};

class ConfigManager {
public:
    
    ConfigManager();
    explicit ConfigManager(const std::string& config_path);
    
    bool exists() const;
    void save(const DeviceCredentials& credentials);
    DeviceCredentials load();
    void remove();
    
    std::string get_config_path() const { return config_path_; }
    
private:
    std::string config_path_;
    
    void ensure_config_directory();
    std::string expand_path(const std::string& path);
    void encrypt_and_save(const nlohmann::json& config);
    nlohmann::json decrypt_and_load();
    std::string encrypt_token(const std::string& token);
    std::string decrypt_token(const std::string& encrypted_token);
};

} // namespace thinr::config

#endif // THINREMOTE_CONFIG_MANAGER_HPP