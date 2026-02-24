#pragma once

#ifndef THINREMOTE_SERVICE_MANAGER_HPP
#define THINREMOTE_SERVICE_MANAGER_HPP

#include "../installer/service_installer.hpp"
#include "../config/config_manager.hpp"

namespace thinr::cli {

class service_manager {
public:
    service_manager();

    // Main service management interface
    bool show_management_menu();

    // Specific operations
    bool uninstall_completely();
    bool start_service();
    bool stop_service();
    bool restart_service();
    bool view_logs();

    // Status and information
    installer::service_installer::ServiceStatus get_service_status() const;
    void show_service_status() const;

private:
    installer::service_installer service_installer_;
    config::config_manager config_manager_;

    // Menu helpers
    bool handle_running_service_choice(int choice);
    bool handle_stopped_service_choice(int choice);

    // Update helpers
    bool update_service();
    std::string get_installed_version() const;
    bool has_update_available() const;

    // Uninstall helpers
    bool confirm_uninstall();
    bool remove_configuration_and_data();
    void cleanup_directories();

    // Utility methods
    bool is_interactive_terminal() const;
};

} // namespace thinr::cli

#endif // THINREMOTE_SERVICE_MANAGER_HPP