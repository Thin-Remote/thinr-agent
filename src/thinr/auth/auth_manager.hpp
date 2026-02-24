#pragma once

#ifndef THINREMOTE_AUTH_MANAGER_HPP
#define THINREMOTE_AUTH_MANAGER_HPP

#include <string>
#include <memory>
#include <functional>
#include <nlohmann/json.hpp>
#include "../config/config_manager.hpp"

namespace thinr::auth {

// SSL verification callback function type
using SSLVerificationCallback = std::function<bool(const std::string& error_msg)>;

// Error classification for auth operations
enum class AuthError {
    none,
    invalid_credentials,
    access_denied,
    not_found,
    network_error,
    token_expired,
    user_denied,
    timeout,
    invalid_response,
    server_error,
    ssl_error
};

class auth_manager {
public:
    auth_manager();

    // Set callback for SSL verification decisions
    void set_ssl_verification_callback(SSLVerificationCallback callback);

    // Result of an OAuth/token operation
    struct AuthResult {
        bool success = false;
        AuthError error = AuthError::none;
        int status_code = 0;
        std::string access_token;
        std::string error_detail;
    };

    // OAuth password flow - returns AuthResult
    AuthResult oauth_password_flow(const std::string& host,
                                   const std::string& username,
                                   const std::string& password);

    // Result of a provisioning attempt
    struct ProvisionResult {
        bool success = false;
        int status_code = 0;
        config::DeviceCredentials credentials;
    };

    // Provision device with access token
    ProvisionResult provision_device(const std::string& host,
                                    const std::string& username,
                                    const std::string& device_id,
                                    const std::string& device_name,
                                    const std::string& access_token);

    // Auto-provision with token
    ProvisionResult auto_provision(const std::string& token);
    ProvisionResult auto_provision_with_device_id(const std::string& token, const std::string& device_id, const std::string& device_name = "");

    // Test connection with device credentials
    bool test_connection(const config::DeviceCredentials& credentials);

    // Delete existing device
    bool delete_device(const std::string& host,
                      const std::string& username,
                      const std::string& device_id,
                      const std::string& access_token);

    // Decode JWT payload (public for use by interactive setup)
    nlohmann::json decode_jwt_payload(const std::string& jwt_token);

    // OAuth2 Device Flow
    struct DeviceAuthResponse {
        std::string device_code;
        std::string user_code;
        std::string verification_uri;
        int expires_in;
        int interval;
    };

    // Result of a device flow initiation
    struct DeviceFlowResult {
        bool success = false;
        AuthError error = AuthError::none;
        int status_code = 0;
        DeviceAuthResponse response;
        std::string error_detail;
    };

    DeviceFlowResult start_device_flow(const std::string& host, const std::string& client_id);
    AuthResult poll_device_token(const std::string& host,
                                 const std::string& client_id,
                                 const std::string& device_code,
                                 int timeout_seconds = 300,
                                 int interval_seconds = 5);

private:
    struct HttpResponse {
        int status_code;
        std::string body;
        bool success;
    };

    HttpResponse make_http_request(const std::string& url,
                                  const std::string& method,
                                  const nlohmann::json& payload = {},
                                  const std::string& authorization = "",
                                  bool verify_ssl = true);

    HttpResponse make_http_request_form(const std::string& url,
                                       const std::string& method,
                                       const std::map<std::string, std::string>& form_data,
                                       const std::string& authorization = "",
                                       bool verify_ssl = true);

    std::string extract_host_from_url(const std::string& url);
    std::string ensure_https(const std::string& host);
    nlohmann::json parse_response(const HttpResponse& response, const std::string& operation);
    std::string generate_device_credentials();

    // Wrapper methods that try with SSL verification first, then fallback to insecure
    HttpResponse make_http_request_with_fallback(const std::string& url,
                                               const std::string& method,
                                               const nlohmann::json& payload = {},
                                               const std::string& authorization = "");

    HttpResponse make_http_request_form_with_fallback(const std::string& url,
                                                    const std::string& method,
                                                    const std::map<std::string, std::string>& form_data,
                                                    const std::string& authorization = "");

    static AuthError map_status_to_error(int status_code);
    static AuthError classify_exception(const std::exception& e);

    static constexpr int HTTP_TIMEOUT_SECONDS = 30;

    // SSL verification callback
    SSLVerificationCallback ssl_verification_callback_;
};

} // namespace thinr::auth

#endif // THINREMOTE_AUTH_MANAGER_HPP