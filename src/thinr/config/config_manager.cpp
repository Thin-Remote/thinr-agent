#include "config_manager.hpp"
#include <fstream>
#include <iostream>
#include <iomanip>
#include <spdlog/spdlog.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/aes.h>
#include <cstdlib>
#include <pwd.h>
#include <unistd.h>

namespace thinr::config {

ConfigManager::ConfigManager() {
    // Try system-wide config first, fall back to user config
    if (geteuid() == 0) {
        config_path_ = installer::InstallationConfig::SYSTEM_CONFIG_PATH;
    } else {
        config_path_ = expand_path(installer::InstallationConfig::USER_CONFIG_PATH);
    }
}

ConfigManager::ConfigManager(const std::string& config_path) 
    : config_path_(expand_path(config_path)) {
}

std::string ConfigManager::expand_path(const std::string& path) {
    if (path.empty() || path[0] != '~') {
        return path;
    }
    
    const char* home = getenv("HOME");
    if (!home) {
        struct passwd* pw = getpwuid(getuid());
        if (pw) {
            home = pw->pw_dir;
        }
    }
    
    if (!home) {
        throw std::runtime_error("Cannot determine home directory");
    }
    
    return std::string(home) + path.substr(1);
}

bool ConfigManager::exists() const {
    return std::filesystem::exists(config_path_);
}

void ConfigManager::ensure_config_directory() {
    std::filesystem::path config_file(config_path_);
    std::filesystem::path config_dir = config_file.parent_path();
    
    spdlog::debug("Ensuring config directory exists: {}", config_dir.string());
    spdlog::debug("Config file path: {}", config_path_);
    
    if (!std::filesystem::exists(config_dir)) {
        try {
            std::filesystem::create_directories(config_dir);
            spdlog::info("Created configuration directory: {}", config_dir.string());
        } catch (const std::filesystem::filesystem_error& e) {
            throw std::runtime_error("Failed to create config directory '" + config_dir.string() + "': " + e.what());
        }
    }
}

void ConfigManager::save(const DeviceCredentials& credentials) {
    if (!credentials.is_valid()) {
        throw std::invalid_argument("Invalid device credentials");
    }
    
    ensure_config_directory();
    
    nlohmann::json config = {
        {"host", credentials.host},
        {"device", {
            {"id", credentials.device_id},
            {"name", credentials.device_name},
            {"user", credentials.device_user},
            {"token", credentials.device_token}  // Will be encrypted
        }},
        {"version", credentials.version.empty() ? "1.0.0" : credentials.version}
    };
    
    encrypt_and_save(config);
    
    // Set proper permissions (600 - read/write for owner only)
    std::filesystem::permissions(config_path_, 
                               std::filesystem::perms::owner_read | 
                               std::filesystem::perms::owner_write,
                               std::filesystem::perm_options::replace);
    
    spdlog::info("Configuration saved to: {}", config_path_);
}

DeviceCredentials ConfigManager::load() {
    if (!exists()) {
        throw std::runtime_error("Configuration file does not exist: " + config_path_);
    }
    
    nlohmann::json config = decrypt_and_load();
    
    DeviceCredentials credentials;
    credentials.host = config.value("host", "");
    credentials.version = config.value("version", "1.0.0");
    
    if (config.contains("device")) {
        const auto& device = config["device"];
        credentials.device_id = device.value("id", "");
        credentials.device_name = device.value("name", "");
        credentials.device_user = device.value("user", "");
        credentials.device_token = device.value("token", "");
    }
    
    if (!credentials.is_valid()) {
        throw std::runtime_error("Invalid configuration data in: " + config_path_);
    }
    
    return credentials;
}

void ConfigManager::remove() {
    if (exists()) {
        std::filesystem::remove(config_path_);
        spdlog::info("Configuration removed: {}", config_path_);
    }
}

void ConfigManager::encrypt_and_save(const nlohmann::json& config) {
    // For now, implement basic encryption
    // In production, use proper key derivation and stronger encryption
    
    nlohmann::json encrypted_config = config;
    
    // Encrypt the device token
    if (encrypted_config.contains("device") && 
        encrypted_config["device"].contains("token")) {
        std::string token = encrypted_config["device"]["token"];
        encrypted_config["device"]["token"] = encrypt_token(token);
    }
    
    std::ofstream file(config_path_);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open config file for writing: " + config_path_);
    }
    
    file << std::setw(2) << encrypted_config << std::endl;
    file.close();
}

nlohmann::json ConfigManager::decrypt_and_load() {
    std::ifstream file(config_path_);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open config file for reading: " + config_path_);
    }
    
    nlohmann::json config;
    file >> config;
    file.close();
    
    // Decrypt the device token
    if (config.contains("device") && 
        config["device"].contains("token")) {
        std::string encrypted_token = config["device"]["token"];
        config["device"]["token"] = decrypt_token(encrypted_token);
    }
    
    return config;
}

std::string ConfigManager::encrypt_token(const std::string& token) {
    // Simple XOR encryption for now
    // In production, use proper AES encryption with key derivation
    std::string encrypted = token;
    const char key = 0x5A; // Simple key
    
    for (char& c : encrypted) {
        c ^= key;
    }
    
    // Convert to hex for storage
    std::stringstream ss;
    for (unsigned char c : encrypted) {
        ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(c);
    }
    
    return ss.str();
}

std::string ConfigManager::decrypt_token(const std::string& encrypted_token) {
    // Convert from hex
    std::string binary;
    for (size_t i = 0; i < encrypted_token.length(); i += 2) {
        std::string byte_str = encrypted_token.substr(i, 2);
        unsigned char byte = static_cast<unsigned char>(std::stoi(byte_str, nullptr, 16));
        binary.push_back(byte);
    }
    
    // Decrypt with XOR
    const char key = 0x5A;
    for (char& c : binary) {
        c ^= key;
    }
    
    return binary;
}

} // namespace thinr::config