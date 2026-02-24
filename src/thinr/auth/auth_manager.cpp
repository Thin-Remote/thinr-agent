#include "auth_manager.hpp"
#include "../utils/system_info.hpp"
#include <spdlog/spdlog.h>
#include <thinger/http_client.hpp>
#include <stdexcept>
#include <regex>
#include <chrono>
#include <map>
#include <sstream>
#include <random>
#include <algorithm>
#include <fstream>
#include <openssl/bio.h>
#include <openssl/buffer.h>
#include <openssl/evp.h>
#include <boost/asio.hpp>
#include <iostream>
#include "../utils/console.hpp"

namespace thinr::auth {

auth_manager::auth_manager() {
}

void auth_manager::set_ssl_verification_callback(SSLVerificationCallback callback) {
    ssl_verification_callback_ = callback;
}

AuthError auth_manager::map_status_to_error(int status_code) {
    switch (status_code) {
        case 401: return AuthError::invalid_credentials;
        case 403: return AuthError::access_denied;
        case 404: return AuthError::not_found;
        default:
            if (status_code >= 500) return AuthError::server_error;
            return AuthError::network_error;
    }
}

AuthError auth_manager::classify_exception(const std::exception& e) {
    std::string msg = e.what();
    if (msg.find("SSL") != std::string::npos ||
        msg.find("TLS") != std::string::npos ||
        msg.find("certificate") != std::string::npos ||
        msg.find("handshake") != std::string::npos ||
        msg.find("verification failed") != std::string::npos) {
        return AuthError::ssl_error;
    }
    if (msg.find("timeout") != std::string::npos ||
        msg.find("Timeout") != std::string::npos) {
        return AuthError::timeout;
    }
    return AuthError::network_error;
}

auth_manager::AuthResult auth_manager::oauth_password_flow(const std::string& host,
                                            const std::string& username,
                                            const std::string& password) {
    std::string base_url = ensure_https(host);
    std::string oauth_url = base_url + "/oauth/token";

    spdlog::debug("Attempting OAuth password flow to: {}", oauth_url);

    HttpResponse response;
    try {
        response = make_http_request_form_with_fallback(oauth_url, "POST", {
            {"grant_type", "password"},
            {"username", username},
            {"password", password}
        });
    } catch (const std::exception& e) {
        spdlog::debug("OAuth password flow exception: {}", e.what());
        return {false, classify_exception(e), 0, {}, e.what()};
    }

    if (!response.success || response.status_code != 200) {
        return {false, map_status_to_error(response.status_code), response.status_code, {}, response.body};
    }

    try {
        auto json_response = parse_response(response, "OAuth authentication");
        if (!json_response.contains("access_token")) {
            return {false, AuthError::invalid_response, response.status_code, {}, "OAuth response missing access_token"};
        }
        std::string access_token = json_response["access_token"];
        spdlog::debug("OAuth authentication successful");
        return {true, AuthError::none, response.status_code, access_token, {}};
    } catch (const std::exception& e) {
        return {false, AuthError::invalid_response, response.status_code, {}, e.what()};
    }
}

auth_manager::ProvisionResult auth_manager::provision_device(const std::string& host,
                                                            const std::string& username,
                                                            const std::string& device_id,
                                                            const std::string& device_name,
                                                            const std::string& access_token) {
    std::string base_url = ensure_https(host);
    std::string provision_url = base_url + "/v1/users/" + username + "/devices";

    // Generate device credentials
    std::string device_credentials = generate_device_credentials();

    nlohmann::json payload = {
        {"enabled", true},
        {"type", "Generic"},
        {"device", device_id},
        {"credentials", device_credentials},
        {"name", device_name.empty() ? device_id : device_name},
        {"description", utils::SystemInfo::get_os_description()}
    };

    spdlog::debug("Provisioning device: {} for user: {}", device_id, username);

    HttpResponse response = make_http_request_with_fallback(provision_url, "POST", payload,
                                                           "Bearer " + access_token);

    if (!response.success || (response.status_code != 200 && response.status_code != 201)) {
        spdlog::debug("Device provisioning failed: {} {}", response.status_code, response.body);
        return {false, response.status_code, {}};
    }

    auto json_response = parse_response(response, "Device provisioning");

    config::DeviceCredentials credentials;
    credentials.host = host;
    credentials.device_name = device_name;

    // Extract device ID from response or use the one we sent
    if (json_response.contains("device")) {
        credentials.device_id = json_response["device"];
    } else {
        credentials.device_id = device_id;
    }

    credentials.device_user = username;  // The user from OAuth
    credentials.device_token = device_credentials;  // The credentials we generated
    credentials.version = "1.0.0";

    if (!credentials.is_valid()) {
        spdlog::error("Invalid device credentials received from server");
        return {false, response.status_code, {}};
    }

    spdlog::debug("Device provisioned successfully: {}", credentials.device_id);

    return {true, response.status_code, credentials};
}

auth_manager::ProvisionResult auth_manager::auto_provision(const std::string& token) {
    spdlog::debug("Attempting auto-provision with JWT token");

    nlohmann::json payload = decode_jwt_payload(token);

    if (!payload.contains("svr") || !payload.contains("usr")) {
        return {false, 0, {}};
    }

    std::string host = payload["svr"];
    std::string username = payload["usr"];

    // Generate device ID from hostname
    std::string device_id;
    {
        boost::system::error_code ec;
        device_id = boost::asio::ip::host_name(ec);
        if (device_id.empty()) {
            device_id = "thinremote-device";
        }
    }

    spdlog::debug("Auto-provisioning device: {} for user: {} on host: {}", device_id, username, host);

    std::string device_name = device_id;
    return provision_device(host, username, device_id, device_name, token);
}

auth_manager::ProvisionResult auth_manager::auto_provision_with_device_id(const std::string& token, const std::string& device_id, const std::string& device_name) {
    spdlog::debug("Attempting auto-provision with JWT token and custom device ID");

    nlohmann::json payload = decode_jwt_payload(token);

    if (!payload.contains("svr") || !payload.contains("usr")) {
        return {false, 0, {}};
    }

    std::string host = payload["svr"];
    std::string username = payload["usr"];

    spdlog::debug("Auto-provisioning device: {} for user: {} on host: {}", device_id, username, host);

    return provision_device(host, username, device_id, device_name, token);
}

bool auth_manager::test_connection(const config::DeviceCredentials& credentials) {
    std::string base_url = ensure_https(credentials.host);
    std::string test_url = base_url + "/v1/users/" + credentials.device_user + 
                          "/devices/" + credentials.device_id;
    
    spdlog::debug("Testing connection to: {}", test_url);
    
    HttpResponse response = make_http_request_with_fallback(test_url, "GET", {}, 
                                                           "Bearer " + credentials.device_token);
    
    bool success = response.success && (response.status_code == 200 || response.status_code == 404);
    
    if (success) {
        spdlog::debug("Connection test successful");
    } else {
        spdlog::error("Connection test failed: {} {}", response.status_code, response.body);
    }
    
    return success;
}

bool auth_manager::delete_device(const std::string& host,
                               const std::string& username,
                               const std::string& device_id,
                               const std::string& access_token) {
    std::string base_url = ensure_https(host);
    std::string delete_url = base_url + "/v1/users/" + username + "/devices/" + device_id;
    
    spdlog::debug("Deleting device: {} for user: {}", device_id, username);
    
    HttpResponse response = make_http_request_with_fallback(delete_url, "DELETE", {}, 
                                                           "Bearer " + access_token);
    
    bool success = response.success && (response.status_code == 200 || response.status_code == 204);
    
    if (success) {
        spdlog::debug("Device deleted successfully: {}", device_id);
    } else {
        spdlog::error("Failed to delete device: {} {}", response.status_code, response.body);
    }
    
    return success;
}

auth_manager::HttpResponse auth_manager::make_http_request(const std::string& url,
                                                        const std::string& method,
                                                        const nlohmann::json& payload,
                                                        const std::string& authorization,
                                                        bool verify_ssl) {
    HttpResponse result;
    result.success = false;

    try {
        // Create and configure HTTP client
        thinger::http::client client;
        client.timeout(std::chrono::seconds(HTTP_TIMEOUT_SECONDS))
              .verify_ssl(verify_ssl)
              .user_agent("ThinRemote/1.0.0");

        if (!verify_ssl) {
            spdlog::warn("SSL certificate verification disabled - connection is insecure!");
        }

        // Prepare headers
        thinger::http::headers_map headers;
        if (!authorization.empty()) {
            headers["Authorization"] = authorization;
        }

        // Make request
        thinger::http::client_response res;

        if (method == "GET") {
            res = client.get(url, headers);
        } else if (method == "POST") {
            std::string body = payload.empty() ? "" : payload.dump();
            res = client.post(url, body, "application/json", headers);
        } else if (method == "DELETE") {
            res = client.del(url, headers);
        } else {
            throw std::runtime_error("Unsupported HTTP method: " + method);
        }

        if (res.has_error()) {
            throw std::runtime_error("HTTP request failed: " + res.error());
        }

        result.status_code = res.status();
        result.body = res.body();
        result.success = true;

        spdlog::debug("HTTP {} {} -> {}", method, url, result.status_code);

    } catch (const std::exception& e) {
        spdlog::error("HTTP request error: {}", e.what());
        // Re-throw the exception to let the fallback mechanism handle it
        throw;
    }

    return result;
}

auth_manager::HttpResponse auth_manager::make_http_request_form(const std::string& url,
                                                             const std::string& method,
                                                             const std::map<std::string, std::string>& form_data,
                                                             const std::string& authorization,
                                                             bool verify_ssl) {
    HttpResponse result;
    result.success = false;

    try {
        // Create and configure HTTP client
        thinger::http::client client;
        client.timeout(std::chrono::seconds(HTTP_TIMEOUT_SECONDS))
              .verify_ssl(verify_ssl)
              .user_agent("ThinRemote/1.0.0");

        if (!verify_ssl) {
            spdlog::warn("SSL certificate verification disabled - connection is insecure!");
        }

        // Prepare headers
        thinger::http::headers_map headers;
        if (!authorization.empty()) {
            headers["Authorization"] = authorization;
        }

        // Build form data
        std::stringstream form_body;
        bool first = true;
        for (const auto& pair : form_data) {
            if (!first) form_body << "&";
            first = false;

            // URL encode the values (basic implementation)
            std::string encoded_key = pair.first;
            std::string encoded_value = pair.second;

            // Replace spaces with +
            std::replace(encoded_value.begin(), encoded_value.end(), ' ', '+');

            form_body << encoded_key << "=" << encoded_value;
        }

        std::string body = form_body.str();

        // Make request
        thinger::http::client_response res;

        if (method == "POST") {
            res = client.post(url, body, "application/x-www-form-urlencoded", headers);
        } else {
            throw std::runtime_error("Unsupported HTTP method for form data: " + method);
        }

        if (res.has_error()) {
            throw std::runtime_error("HTTP request failed: " + res.error());
        }

        result.status_code = res.status();
        result.body = res.body();
        result.success = true;

        spdlog::debug("HTTP {} {} -> {}", method, url, result.status_code);

    } catch (const std::exception& e) {
        spdlog::error("HTTP form request error: {}", e.what());
        // Re-throw the exception to let the fallback mechanism handle it
        throw;
    }

    return result;
}

std::string auth_manager::extract_host_from_url(const std::string& url) {
    std::regex host_regex(R"(^(?:https?://)?([^/]+)(?:/.*)?$)");
    std::smatch match;
    
    if (std::regex_match(url, match, host_regex)) {
        return match[1].str();
    }
    
    return url;
}

std::string auth_manager::ensure_https(const std::string& host) {
    if (host.find("://") != std::string::npos) {
        return host;
    }
    return "https://" + host;
}

nlohmann::json auth_manager::parse_response(const HttpResponse& response, 
                                          const std::string& operation) {
    try {
        return nlohmann::json::parse(response.body);
    } catch (const nlohmann::json::parse_error& e) {
        throw std::runtime_error(operation + " response parse error: " + e.what());
    }
}

nlohmann::json auth_manager::decode_jwt_payload(const std::string& jwt_token) {
    // JWT format: header.payload.signature
    std::vector<std::string> parts;
    std::stringstream ss(jwt_token);
    std::string item;
    
    while (std::getline(ss, item, '.')) {
        parts.push_back(item);
    }
    
    if (parts.size() != 3) {
        throw std::runtime_error("Invalid JWT format");
    }
    
    // Decode base64 payload (second part)
    std::string payload_b64 = parts[1];
    
    // Add padding if needed
    while (payload_b64.length() % 4 != 0) {
        payload_b64 += '=';
    }
    
    // Simple base64 decode using OpenSSL
    BIO *bio, *b64;
    char *decoded_payload = new char[payload_b64.length()];
    
    b64 = BIO_new(BIO_f_base64());
    bio = BIO_new_mem_buf(payload_b64.c_str(), payload_b64.length());
    bio = BIO_push(b64, bio);
    
    BIO_set_flags(bio, BIO_FLAGS_BASE64_NO_NL);
    int decoded_length = BIO_read(bio, decoded_payload, payload_b64.length());
    
    BIO_free_all(bio);
    
    if (decoded_length <= 0) {
        delete[] decoded_payload;
        throw std::runtime_error("Failed to decode JWT payload");
    }
    
    std::string payload_json(decoded_payload, decoded_length);
    delete[] decoded_payload;
    
    return nlohmann::json::parse(payload_json);
}

std::string auth_manager::generate_device_credentials() {
    // Generate random credentials similar to "EJLU85e#-u&Q88$0"
    const std::string chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789#-&$@%";
    const int length = 16;
    
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, chars.size() - 1);
    
    std::string credentials;
    credentials.reserve(length);
    
    for (int i = 0; i < length; ++i) {
        credentials += chars[dis(gen)];
    }
    
    return credentials;
}

auth_manager::HttpResponse auth_manager::make_http_request_with_fallback(const std::string& url,
                                                                      const std::string& method,
                                                                      const nlohmann::json& payload,
                                                                      const std::string& authorization) {
    try {
        return make_http_request(url, method, payload, authorization, true);
    } catch (const std::exception& e) {
        if (classify_exception(e) != AuthError::ssl_error) throw;

        spdlog::info("SSL error: '{}'. Checking for callback...", e.what());
        if (ssl_verification_callback_) {
            if (ssl_verification_callback_(e.what())) {
                spdlog::warn("User accepted insecure connection - SSL verification disabled");
                return make_http_request(url, method, payload, authorization, false);
            }
            throw std::runtime_error("SSL verification failed and insecure connection was not allowed: " + std::string(e.what()));
        }
        throw;
    }
}

auth_manager::HttpResponse auth_manager::make_http_request_form_with_fallback(const std::string& url,
                                                                           const std::string& method,
                                                                           const std::map<std::string, std::string>& form_data,
                                                                           const std::string& authorization) {
    try {
        return make_http_request_form(url, method, form_data, authorization, true);
    } catch (const std::exception& e) {
        if (classify_exception(e) != AuthError::ssl_error) throw;

        spdlog::info("SSL error: '{}'. Checking for callback...", e.what());
        if (ssl_verification_callback_) {
            if (ssl_verification_callback_(e.what())) {
                spdlog::warn("User accepted insecure connection - SSL verification disabled");
                return make_http_request_form(url, method, form_data, authorization, false);
            }
            throw std::runtime_error("SSL verification failed and insecure connection was not allowed: " + std::string(e.what()));
        }
        throw;
    }
}

auth_manager::DeviceFlowResult auth_manager::start_device_flow(const std::string& host, const std::string& client_id) {
    std::string base_url = ensure_https(host);
    std::string device_auth_url = base_url + "/oauth/device/authorize";

    spdlog::debug("Starting device flow to: {}", device_auth_url);

    HttpResponse response;
    try {
        response = make_http_request_form_with_fallback(device_auth_url, "POST", {
            {"client_id", client_id}
        });
    } catch (const std::exception& e) {
        spdlog::debug("Device flow start exception: {}", e.what());
        return {false, classify_exception(e), 0, {}, e.what()};
    }

    if (!response.success || response.status_code != 200) {
        return {false, map_status_to_error(response.status_code), response.status_code, {}, response.body};
    }

    try {
        auto json_response = parse_response(response, "Device authorization");

        DeviceAuthResponse device_response;
        device_response.device_code = json_response.value("device_code", "");
        device_response.user_code = json_response.value("user_code", "");
        device_response.verification_uri = json_response.value("verification_uri", "");
        device_response.expires_in = json_response.value("expires_in", 600);
        device_response.interval = json_response.value("interval", 5);

        if (device_response.device_code.empty() || device_response.user_code.empty()) {
            return {false, AuthError::invalid_response, response.status_code, {}, "Missing device_code or user_code"};
        }

        spdlog::debug("Device flow started successfully, user_code: {}", device_response.user_code);
        return {true, AuthError::none, response.status_code, device_response, {}};
    } catch (const std::exception& e) {
        return {false, AuthError::invalid_response, response.status_code, {}, e.what()};
    }
}

auth_manager::AuthResult auth_manager::poll_device_token(const std::string& host,
                                          const std::string& client_id,
                                          const std::string& device_code,
                                          int timeout_seconds,
                                          int interval_seconds) {
    std::string base_url = ensure_https(host);
    std::string token_url = base_url + "/oauth/token";

    auto start_time = std::chrono::steady_clock::now();
    auto timeout = std::chrono::seconds(timeout_seconds);

    spdlog::debug("Starting token polling with timeout: {}s, interval: {}s", timeout_seconds, interval_seconds);

    while (true) {
        auto current_time = std::chrono::steady_clock::now();
        if (current_time - start_time > timeout) {
            return {false, AuthError::timeout, 0, {}, "User did not authorize within " + std::to_string(timeout_seconds) + " seconds"};
        }

        HttpResponse response;
        try {
            response = make_http_request_form_with_fallback(token_url, "POST", {
                {"grant_type", "urn:ietf:params:oauth:grant-type:device_code"},
                {"client_id", client_id},
                {"device_code", device_code}
            });
        } catch (const std::exception& e) {
            spdlog::debug("Token polling exception: {}", e.what());
            return {false, classify_exception(e), 0, {}, e.what()};
        }

        if (response.success && response.status_code == 200) {
            try {
                auto json_response = parse_response(response, "Token polling");
                if (json_response.contains("access_token")) {
                    std::string access_token = json_response["access_token"];
                    spdlog::debug("Device flow completed successfully");
                    return {true, AuthError::none, response.status_code, access_token, {}};
                }
                return {false, AuthError::invalid_response, response.status_code, {}, "Token response missing access_token"};
            } catch (const std::exception& e) {
                return {false, AuthError::invalid_response, response.status_code, {}, e.what()};
            }
        } else if (response.status_code == 400) {
            try {
                auto json_response = nlohmann::json::parse(response.body);
                std::string error = json_response.value("error", "");

                if (error == "authorization_pending") {
                    spdlog::debug("Authorization pending, continuing to poll...");
                } else if (error == "slow_down") {
                    spdlog::debug("Received slow_down, increasing polling interval");
                    std::this_thread::sleep_for(std::chrono::seconds(5));
                    continue;
                } else if (error == "access_denied") {
                    return {false, AuthError::user_denied, response.status_code, {}, "User denied the authorization request"};
                } else if (error == "expired_token") {
                    return {false, AuthError::token_expired, response.status_code, {}, "Device code expired"};
                } else {
                    return {false, AuthError::server_error, response.status_code, {}, "OAuth error: " + error};
                }
            } catch (const nlohmann::json::exception&) {
                return {false, AuthError::invalid_response, response.status_code, {}, response.body};
            }
        } else {
            return {false, map_status_to_error(response.status_code), response.status_code, {}, response.body};
        }

        std::this_thread::sleep_for(std::chrono::seconds(interval_seconds));
    }
}

} // namespace thinr::auth