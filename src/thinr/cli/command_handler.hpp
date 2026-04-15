#pragma once

#ifndef THINREMOTE_COMMAND_HANDLER_HPP
#define THINREMOTE_COMMAND_HANDLER_HPP

#include "argument_parser.hpp"
#include "service_manager.hpp"
#include "../config/config_manager.hpp"
#include "../auth/auth_manager.hpp"

namespace thinr::cli {

class command_handler {
public:
    command_handler();

    // Main execution method
    int execute(const ParseResult& parse_result);

    // Command handlers
    int handle_install(const InstallOptions& options);
    int handle_uninstall();
    int handle_status();
    int handle_test();
    int handle_reconfigure();
    int handle_update(const UpdateOptions& options);
    int handle_no_command(const std::string& config_path);

private:
    service_manager service_manager_;
    config::config_manager config_manager_;
    auth::auth_manager auth_manager_;
    installer::service_installer service_installer_;
    bool verify_ssl_ = true;

    // Install helpers
    bool install_with_token(const InstallOptions& options);
    bool install_interactive(const InstallOptions& options);
    bool save_and_install_service(const config::DeviceCredentials& credentials, bool no_start);
    std::string determine_device_id(const std::string& provided_device_id);
    std::string determine_device_name(const std::string& fallback);
    std::string resolve_product(const std::string& host, const std::string& username,
                                const std::string& access_token, const std::string& provided_product);

    // System-wide detection
    bool check_system_installation() const;
    int handle_system_installation_detected();
    bool is_running_as_root() const;
    int handle_no_configuration_but_run(const std::string& config_path);

    // Utility methods
    bool is_interactive_terminal() const;
    std::string read_password_securely();
    void setup_logging(int verbosity_level);
};

} // namespace thinr::cli

#endif // THINREMOTE_COMMAND_HANDLER_HPP