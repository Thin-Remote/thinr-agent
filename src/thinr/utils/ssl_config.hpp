#pragma once

#include <string>
#include <vector>
#include <filesystem>
#include <cstdlib>

namespace thinr::utils {

class SSLConfig {
public:
    // Common certificate bundle locations across different Linux distributions
    static const std::vector<std::string> CERT_FILE_PATHS;

    /**
     * Configure SSL certificate paths automatically based on what's available
     * in the system. This sets SSL_CERT_FILE and/or SSL_CERT_DIR environment
     * variables if they're not already set.
     */
    static void configure_ssl_certificates();

private:
    static bool is_env_set(const char* env_name);
    static std::string find_cert_file();
};

} // namespace thinr::utils