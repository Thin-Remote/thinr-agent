#include "ssl_config.hpp"
#include "embedded_certs.hpp"
#include <fstream>
#include <spdlog/spdlog.h>
#include <unistd.h>
#include <pwd.h>

namespace thinr::utils {

// Common certificate bundle file locations
const std::vector<std::string> SSLConfig::CERT_FILE_PATHS = {
    "/etc/ssl/certs/ca-certificates.crt",                   // Debian/Ubuntu/Gentoo etc.
    "/etc/pki/tls/certs/ca-bundle.crt",                     // Fedora/RHEL 6
    "/etc/ssl/ca-bundle.pem",                               // OpenSUSE
    "/etc/pki/tls/cert.pem",                                // Fedora/RHEL 7
    "/etc/pki/ca-trust/extracted/pem/tls-ca-bundle.pem",    // CentOS/RHEL 7
    "/etc/ssl/cert.pem",                                    // Alpine Linux
    "/usr/local/share/certs/ca-root-nss.crt",               // FreeBSD
    "/etc/ssl/certs/ca-bundle.crt",                         // Old NixOS
    "/etc/ca-certificates/extracted/tls-ca-bundle.pem",     // Arch Linux
    "/usr/share/ca-certificates/ca-bundle.crt"              // Some embedded systems
};


bool SSLConfig::is_env_set(const char* env_name) {
    const char* value = std::getenv(env_name);
    return value != nullptr && std::strlen(value) > 0;
}

std::string SSLConfig::find_cert_file() {
    for (const auto& path : CERT_FILE_PATHS) {
        if (std::filesystem::exists(path) && std::filesystem::is_regular_file(path)) {
            return path;
        }
    }
    return "";
}

std::string SSLConfig::get_certs_directory() {
    if (geteuid() == 0) {
        return "/etc/thinr-agent";
    }

    const char* home = std::getenv("HOME");
    if (!home) {
        struct passwd* pw = getpwuid(getuid());
        if (pw) home = pw->pw_dir;
    }

    if (home) {
        return std::string(home) + "/.config/thinr-agent";
    }

    return "/tmp";
}

bool SSLConfig::write_embedded_certs_if_needed(const std::string& cert_path) {
    // Check if file already exists with correct content
    if (std::filesystem::exists(cert_path)) {
        std::ifstream existing(cert_path);
        std::string content((std::istreambuf_iterator<char>(existing)),
                            std::istreambuf_iterator<char>());
        if (content == EMBEDDED_CA_CERTS) {
            return true; // Already up to date
        }
    }

    // Ensure directory exists
    std::filesystem::path dir = std::filesystem::path(cert_path).parent_path();
    try {
        std::filesystem::create_directories(dir);
    } catch (const std::filesystem::filesystem_error& e) {
        spdlog::error("Failed to create certificate directory {}: {}", dir.string(), e.what());
        return false;
    }

    // Write embedded certificates
    std::ofstream file(cert_path);
    if (!file.is_open()) {
        spdlog::error("Failed to write embedded certificates to {}", cert_path);
        return false;
    }

    file << EMBEDDED_CA_CERTS;
    file.close();

    // Set permissions (644 - readable by all, writable by owner)
    try {
        std::filesystem::permissions(cert_path,
            std::filesystem::perms::owner_read | std::filesystem::perms::owner_write |
            std::filesystem::perms::group_read | std::filesystem::perms::others_read,
            std::filesystem::perm_options::replace);
    } catch (const std::filesystem::filesystem_error&) {
        // Non-fatal: file was written, permissions just couldn't be set
    }

    return true;
}

void SSLConfig::configure_ssl_certificates() {
    // If environment variables are already set, respect them
    if (is_env_set("SSL_CERT_FILE") || is_env_set("SSL_CERT_DIR")) {
        spdlog::debug("SSL certificate paths already configured via environment variables");
        return;
    }

    // Try to find certificate bundle file in standard system locations
    std::string cert_file = find_cert_file();
    if (!cert_file.empty()) {
        spdlog::info("Found SSL certificate bundle: {}", cert_file);
        setenv("SSL_CERT_FILE", cert_file.c_str(), 0);

        // Also set the directory (parent of the bundle file)
        std::filesystem::path cert_path(cert_file);
        std::string cert_dir = cert_path.parent_path().string();
        if (!cert_dir.empty()) {
            spdlog::debug("Setting SSL certificate directory: {}", cert_dir);
            setenv("SSL_CERT_DIR", cert_dir.c_str(), 0);
        }
        return;
    }

    // System certificates not found — write embedded Let's Encrypt root certificates to disk
    spdlog::info("System certificates not found, using embedded Let's Encrypt CA");

    std::string certs_dir = get_certs_directory();
    std::string embedded_cert_path = certs_dir + "/ca-certificates.crt";

    if (write_embedded_certs_if_needed(embedded_cert_path)) {
        spdlog::info("Embedded certificates available at: {}", embedded_cert_path);
        setenv("SSL_CERT_FILE", embedded_cert_path.c_str(), 0);
    } else {
        spdlog::warn("Failed to write embedded certificates, SSL verification may fail");
    }
}

} // namespace thinr::utils