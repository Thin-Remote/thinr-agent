#pragma once

#ifndef THINREMOTE_INTERACTIVE_SETUP_HPP
#define THINREMOTE_INTERACTIVE_SETUP_HPP

#include <string>
#include <utility>
#include <stdexcept>
#include "../config/config_manager.hpp"
#include "../auth/auth_manager.hpp"
#include "../utils/console.hpp"
#include "service_installer.hpp"

namespace thinr::installer {

// SSL verification decision (cached for the session)
enum class SSLVerificationDecision {
    NOT_ASKED,
    VERIFY,
    NO_VERIFY
};

enum class MainMenuOption {
    INSTALL_SERVICE = 1,
    CONFIGURE_STANDALONE = 2,
    TEST_CONNECTION = 3,
    EXIT = 4
};

enum class AuthMethod {
    PASSWORD_FLOW = 1,
    AUTO_PROVISION = 2,
    DEVICE_FLOW = 3,
    DIRECT_CREDENTIALS = 4
};

// Thrown when the user explicitly cancels setup — not retryable
struct setup_cancelled : std::runtime_error {
    using std::runtime_error::runtime_error;
};

class interactive_setup {
public:
    interactive_setup();

    bool run();

private:
    void show_banner();
    void show_system_info();
    bool confirm_connectivity_test();
    MainMenuOption show_main_menu();
    AuthMethod select_auth_method();
    config::DeviceCredentials authenticate();
    config::DeviceCredentials password_flow_auth();
    config::DeviceCredentials auto_provision_auth();
    config::DeviceCredentials device_flow_auth();
    config::DeviceCredentials direct_credentials_auth();
    bool test_connection(const config::DeviceCredentials& creds);
    std::pair<bool, bool> offer_service_installation(const config::DeviceCredentials& credentials);
    bool install_as_service(const config::DeviceCredentials& credentials);
    bool save_configuration_only(const config::DeviceCredentials& credentials);
    std::string get_default_device_id();
    std::string get_default_device_name();
    std::pair<std::string, std::string> prompt_device_info();
    std::string detect_distribution();
    std::string detect_init_system();

    enum class ConflictResolution {
        CHOOSE_NEW_ID = 1,
        OVERWRITE = 2,
        GO_BACK = 3
    };

    ConflictResolution handle_device_conflict(const std::string& device_id);
    config::DeviceCredentials provision_with_conflict_resolution(
        const std::string& host,
        const std::string& username,
        const std::string& device_id,
        const std::string& device_name,
        const std::string& access_token);

    config::DeviceCredentials auto_provision_with_conflict_resolution(
        const std::string& token,
        const std::string& device_id,
        const std::string& device_name);

    using ProvisionFn = std::function<auth::auth_manager::ProvisionResult(const std::string& device_id, const std::string& device_name)>;
    using DeleteFn = std::function<bool(const std::string& device_id)>;
    config::DeviceCredentials provision_with_conflict_loop(
        const std::string& initial_device_id,
        const std::string& device_name,
        ProvisionFn provision_fn,
        DeleteFn delete_fn);

    std::string read_input(const std::string& prompt, bool hide_input = false);
    std::string read_password(const std::string& prompt);
    bool confirm(const std::string& prompt, bool default_yes = true);

    void clear_screen();
    void print_separator();
    void show_auth_error(const auth::auth_manager::AuthResult& result);
    void show_device_flow_error(const auth::auth_manager::DeviceFlowResult& result);

    auth::auth_manager auth_manager_;
    config::config_manager config_manager_;
    service_installer service_installer_;
    SSLVerificationDecision ssl_decision_ = SSLVerificationDecision::NOT_ASKED;
};

} // namespace thinr::installer

#endif // THINREMOTE_INTERACTIVE_SETUP_HPP