#include "config_manager.hpp"
#include "../utils/atomic_file.hpp"
#include <fstream>
#include <iostream>
#include <iomanip>
#include <mutex>
#include <spdlog/spdlog.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/aes.h>
#include <cstdlib>
#include <pwd.h>
#include <unistd.h>

namespace thinr::config {

namespace {
// Serializes config writes within this process (CLI vs daemon are separate
// short-lived processes; this guards concurrent writers inside one process).
std::mutex& file_mutex() {
    static std::mutex m;
    return m;
}
} // namespace

config_manager::config_manager() {
    // Try system-wide config first, fall back to user config
    if (geteuid() == 0) {
        config_path_ = installer::InstallationConfig::SYSTEM_CONFIG_PATH;
    } else {
        config_path_ = expand_path(installer::InstallationConfig::USER_CONFIG_PATH);
    }
}

config_manager::config_manager(const std::string& config_path) 
    : config_path_(expand_path(config_path)) {
}

std::string config_manager::expand_path(const std::string& path) {
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

bool config_manager::exists() const {
    return std::filesystem::exists(config_path_);
}

void config_manager::ensure_config_directory() {
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

void config_manager::save(const DeviceCredentials& credentials) {
    if (!credentials.is_valid()) {
        throw std::invalid_argument("Invalid device credentials");
    }

    std::lock_guard<std::mutex> lock(file_mutex());
    ensure_config_directory();

    // Read-modify-write: keep any `update` section and unknown keys. A corrupt
    // file is replaced (credentials must always be storable).
    nlohmann::json config;
    try {
        config = read_raw();
    } catch (const std::exception&) {
        config = nlohmann::json::object();
    }

    config["host"] = credentials.host;
    config["device"] = {
        {"id", credentials.device_id},
        {"name", credentials.device_name},
        {"user", credentials.device_user},
        {"token", encrypt_token(credentials.device_token)},
    };
    config["version"] = credentials.version.empty() ? "1.0.0" : credentials.version;
    config["verify_ssl"] = credentials.verify_ssl;

    write_raw(config);
    spdlog::info("Configuration saved to: {}", config_path_);
}

nlohmann::json config_manager::read_raw() const {
    if (!std::filesystem::exists(config_path_)) {
        return nlohmann::json::object();
    }
    std::ifstream file(config_path_);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open config file for reading: " + config_path_);
    }
    nlohmann::json config = nlohmann::json::parse(file, nullptr, false);
    if (config.is_discarded() || !config.is_object()) {
        throw std::runtime_error("Corrupt config file: " + config_path_);
    }
    return config;
}

void config_manager::write_raw(const nlohmann::json& config) {
    ensure_config_directory();
    if (!thinr::utils::write_atomic(config_path_, config.dump(2), 0600)) {
        throw std::runtime_error("Failed to write config file: " + config_path_);
    }
}

nlohmann::json config_manager::read_update_section() const {
    try {
        nlohmann::json config = read_raw();
        if (config.contains("update")) return config["update"];
    } catch (const std::exception& e) {
        spdlog::warn("Ignoring update section: {}", e.what());
    }
    return nlohmann::json::object();
}

void config_manager::write_update_section(const nlohmann::json& update_obj) {
    std::lock_guard<std::mutex> lock(file_mutex());
    if (!std::filesystem::exists(config_path_)) {
        throw std::runtime_error("Config file does not exist: " + config_path_);
    }
    nlohmann::json config = read_raw();  // throws on corrupt: refuse to clobber
    config["update"] = update_obj;
    write_raw(config);
}

DeviceCredentials config_manager::load() {
    if (!exists()) {
        throw std::runtime_error("Configuration file does not exist: " + config_path_);
    }
    
    nlohmann::json config = decrypt_and_load();
    
    DeviceCredentials credentials;
    credentials.host = config.value("host", "");
    credentials.version = config.value("version", "1.0.0");
    credentials.verify_ssl = config.value("verify_ssl", true);

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

void config_manager::remove() {
    if (exists()) {
        std::filesystem::remove(config_path_);
        spdlog::info("Configuration removed: {}", config_path_);
    }
}

void config_manager::encrypt_and_save(const nlohmann::json& config) {
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

nlohmann::json config_manager::decrypt_and_load() {
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

std::string config_manager::encrypt_token(const std::string& token) {
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

std::string config_manager::decrypt_token(const std::string& encrypted_token) {
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