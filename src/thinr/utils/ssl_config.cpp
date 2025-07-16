#include "ssl_config.hpp"
#include <spdlog/spdlog.h>

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


void SSLConfig::configure_ssl_certificates() {
    // If environment variables are already set, respect them
    if (is_env_set("SSL_CERT_FILE") || is_env_set("SSL_CERT_DIR")) {
        spdlog::debug("SSL certificate paths already configured via environment variables");
        return;
    }

    // Try to find certificate bundle file
    std::string cert_file = find_cert_file();
    if (!cert_file.empty()) {
        spdlog::info("Found SSL certificate bundle: {}", cert_file);
        setenv("SSL_CERT_FILE", cert_file.c_str(), 0);
        
        // Also set the directory (parent of the bundle file)
        std::filesystem::path cert_path(cert_file);
        std::string cert_dir = cert_path.parent_path().string();
        if (!cert_dir.empty()) {
            spdlog::info("Setting SSL certificate directory: {}", cert_dir);
            setenv("SSL_CERT_DIR", cert_dir.c_str(), 0);
        }
        return;
    }

    // Warn if no certificates found
    spdlog::warn("Could not find SSL certificates in standard locations");
    spdlog::warn("SSL verification may fail. Consider setting SSL_CERT_FILE or SSL_CERT_DIR");
    
    // Log checked paths for debugging
    spdlog::debug("Checked certificate files:");
    for (const auto& path : CERT_FILE_PATHS) {
        spdlog::debug("  - {}", path);
    }
}

} // namespace thinr::utils