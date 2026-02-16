#include "sysv_service_installer.hpp"
#include "../../utils/console.hpp"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <iostream>
#include <spdlog/spdlog.h>

namespace thinr::installer {

std::string sysv_service_installer::get_service_file_path(bool system_wide) {
    // SysV init only supports system-wide installation
    if (!system_wide) {
        spdlog::warn("{} only supports system-wide installation", get_init_system_name());
    }
    
    // Check which init.d path exists
    if (std::filesystem::exists("/etc/rc.d/init.d/")) {
        return "/etc/rc.d/init.d/" + config_.get_service_identifier();
    } else if (std::filesystem::exists("/etc/init.d/")) {
        return "/etc/init.d/" + config_.get_service_identifier();
    } else {
        // Default to /etc/init.d/
        return "/etc/init.d/" + config_.get_service_identifier();
    }
}

bool sysv_service_installer::install_service_impl(bool system_wide) {
    try {
        // SysV init only supports system-wide installation
        if (!system_wide || !is_running_as_root()) {
            spdlog::error("SysV init requires root privileges for installation");
            return false;
        }
        
        // Generate init script content
        std::string script_content = generate_service_file(system_wide);
        std::string script_path = get_service_file_path(system_wide);
        
        // Write init script
        std::ofstream script_file(script_path);
        if (!script_file) {
            spdlog::error("Failed to create init script: {}", script_path);
            return false;
        }
        
        script_file << script_content;
        script_file.close();
        
        if (!script_file.good()) {
            spdlog::error("Failed to write init script: {}", script_path);
            return false;
        }
        
        // Make script executable
        std::filesystem::permissions(script_path,
            std::filesystem::perms::owner_all |
            std::filesystem::perms::group_read | std::filesystem::perms::group_exec |
            std::filesystem::perms::others_read | std::filesystem::perms::others_exec);
        
        spdlog::info("Created init script: {}", script_path);
        std::cout << utils::Console::success("Init script created: " + script_path) << "\n";
        
        // Enable service in runlevels
        if (enable_service_runlevels(config_.get_service_identifier())) {
            return true;
        }
        
        return false;
        
    } catch (const std::exception& e) {
        spdlog::error("Failed to install SysV service: {}", e.what());
        return false;
    }
}

bool sysv_service_installer::uninstall_service_impl(bool system_wide) {
    if (!system_wide) {
        spdlog::error("SysV init only supports system-wide installation");
        return false;
    }
    
    std::string script_path = get_service_file_path(system_wide);
    std::string service_name = config_.get_service_identifier();
    
    // Check if init script exists first
    if (!std::filesystem::exists(script_path)) {
        std::cout << utils::Console::info("Init script not found: " + script_path) << "\n";
    }
    
    // Remove from runlevels
    disable_service_runlevels(service_name);
    
    // Remove the init script
    if (std::filesystem::exists(script_path)) {
        try {
            std::filesystem::remove(script_path);
            spdlog::info("Removed init script: {}", script_path);
            std::cout << utils::Console::success("Init script removed: " + script_path) << "\n";
        } catch (const std::filesystem::filesystem_error& e) {
            spdlog::error("Failed to remove init script: {}", e.what());
            std::cout << utils::Console::error("Failed to remove init script: " + script_path) << "\n";
            return false;
        }
    }
    
    // Clean up PID file if it exists
    std::string pid_file = "/var/run/" + service_name + ".pid";
    if (std::filesystem::exists(pid_file)) {
        try {
            std::filesystem::remove(pid_file);
            spdlog::info("Removed PID file: {}", pid_file);
            std::cout << utils::Console::success("PID file removed: " + pid_file) << "\n";
        } catch (const std::filesystem::filesystem_error& e) {
            spdlog::warn("Failed to remove PID file: {}", e.what());
        }
    }
    
    // Clean up log file if it exists
    std::string log_file = "/var/log/" + service_name + ".log";
    if (std::filesystem::exists(log_file)) {
        try {
            std::filesystem::remove(log_file);
            spdlog::info("Removed log file: {}", log_file);
            std::cout << utils::Console::success("Log file removed: " + log_file) << "\n";
        } catch (const std::filesystem::filesystem_error& e) {
            spdlog::warn("Failed to remove log file: {}", e.what());
        }
    }
    
    return true;
}

bool sysv_service_installer::start_service_impl() {
    std::string script_path = get_service_file_path(true);
    std::string service_name = config_.get_service_identifier();
    
    // Check if init script exists
    if (!std::filesystem::exists(script_path)) {
        spdlog::error("Init script not found: {}", script_path);
        return false;
    }
    
    // Start service using the init script
    std::string start_cmd = script_path + " start";
    
    if (execute_system_command(start_cmd, "init script start")) {
        return true;
    } else {
        // Fallback to service command if available
        if (std::filesystem::exists("/usr/sbin/service")) {
            start_cmd = "service " + service_name + " start";
            if (execute_system_command(start_cmd, "service command start")) {
                return true;
            }
        }
        
        return false;
    }
}

bool sysv_service_installer::stop_service_impl() {
    std::string script_path = get_service_file_path(true);
    std::string service_name = config_.get_service_identifier();
    
    // Check if init script exists
    if (!std::filesystem::exists(script_path)) {
        spdlog::error("Init script not found: {}", script_path);
        return false;
    }
    
    // Stop service using the init script
    std::string stop_cmd = script_path + " stop";
    
    if (execute_system_command(stop_cmd, "init script stop")) {
        return true;
    } else {
        // Fallback to service command if available
        if (std::filesystem::exists("/usr/sbin/service")) {
            stop_cmd = "service " + service_name + " stop";
            if (execute_system_command(stop_cmd, "service command stop")) {
                return true;
            }
        }
        
        return false;
    }
}

sysv_service_installer::ServiceStatus sysv_service_installer::check_service_status_impl(bool system_wide) {
    std::string script_path = get_service_file_path(system_wide);
    
    // Check if init script exists
    if (!std::filesystem::exists(script_path)) {
        return ServiceStatus::NOT_INSTALLED;
    }
    
    // Check service status using the init script
    std::string service_name = config_.get_service_identifier();
    std::string status_cmd = script_path + " status >/dev/null 2>&1";
    
    // LSB init script exit codes:
    // 0 = service is running
    // 1 = service is dead but PID file exists
    // 3 = service is not running
    // 4 = service status is unknown
    if (execute_system_command(status_cmd)) {
        return ServiceStatus::INSTALLED_RUNNING;
    } else {
        // For any non-zero exit code, consider it stopped
        return ServiceStatus::INSTALLED_STOPPED;
    }
}

std::string sysv_service_installer::generate_service_file(bool system_wide) {
    std::stringstream script;
    
    // Check if this is an OpenWrt/procd system
    bool is_procd_system = std::filesystem::exists("/sbin/procd") && 
                          std::filesystem::exists("/etc/rc.common");
    
    if (is_procd_system) {
        // Generate OpenWrt/procd compatible script
        script << "#!/bin/sh /etc/rc.common\n";
        script << "# ThinRemote Agent service\n\n";
        
        script << "START=90\n";
        script << "STOP=10\n\n";
        
        script << "USE_PROCD=1\n";
        script << "PROG=" << config_.get_binary_install_path(system_wide) << "\n\n";
        
        script << "start_service() {\n";
        script << "    procd_open_instance\n";
        script << "    procd_set_param command $PROG\n";
        script << "    procd_set_param respawn\n";
        script << "    procd_set_param stdout 1\n";
        script << "    procd_set_param stderr 1\n";
        script << "    procd_set_param pidfile /var/run/" << config_.get_service_identifier() << ".pid\n";
        script << "    procd_close_instance\n";
        script << "}\n\n";
        
        script << "stop_service() {\n";
        script << "    # Don't use killall as it would kill the uninstaller too\n";
        script << "    local pid_file=\"/var/run/" << config_.get_service_identifier() << ".pid\"\n";
        script << "    if [ -f \"$pid_file\" ]; then\n";
        script << "        local pid=$(cat \"$pid_file\")\n";
        script << "        if [ -n \"$pid\" ] && kill -0 \"$pid\" 2>/dev/null; then\n";
        script << "            kill \"$pid\"\n";
        script << "        fi\n";
        script << "        rm -f \"$pid_file\"\n";
        script << "    fi\n";
        script << "}\n\n";
        
        script << "restart() {\n";
        script << "    stop\n";
        script << "    start\n";
        script << "}\n\n";
        
        // Override the stop function to handle our special case
        script << "stop() {\n";
        script << "    # Check if we're being called from our own uninstaller\n";
        script << "    local caller_pid=$PPID\n";
        script << "    local caller_name=$(cat /proc/$caller_pid/comm 2>/dev/null)\n";
        script << "    \n";
        script << "    if [ \"$caller_name\" = \"thinr-agent\" ]; then\n";
        script << "        # Called from thinr-agent, use PID-based stop\n";
        script << "        stop_service\n";
        script << "        return $?\n";
        script << "    else\n";
        script << "        # Normal stop using procd\n";
        script << "        rc_procd stop_service \"$@\"\n";
        script << "        return $?\n";
        script << "    fi\n";
        script << "}\n";
        
        return script.str();
    }
    
    // Standard SysV init script
    script << "#!/bin/sh\n\n";
    
    // Configuration
    script << "DAEMON=" << config_.get_binary_install_path(system_wide) << "\n";
    script << "NAME=thinr-agent\n";
    script << "PIDFILE=/var/run/$NAME.pid\n";
    script << "LOGFILE=/var/log/$NAME.log\n\n";
    
    // Check if daemon exists
    script << "if [ ! -x \"$DAEMON\" ]; then\n";
    script << "    echo \"$DAEMON not found\"\n";
    script << "    exit 0\n";
    script << "fi\n\n";
    
    // Create log directory if needed
    script << "mkdir -p /var/log\n";
    script << "mkdir -p /var/run\n\n";
    
    // Simple start function
    script << "start() {\n";
    script << "    echo -n \"Starting $NAME: \"\n";
    script << "    # Check if already running\n";
    script << "    if [ -f $PIDFILE ]; then\n";
    script << "        PID=$(cat $PIDFILE)\n";
    script << "        if kill -0 $PID 2>/dev/null; then\n";
    script << "            echo \"already running (PID $PID)\"\n";
    script << "            return 1\n";
    script << "        else\n";
    script << "            # Stale PID file\n";
    script << "            rm -f $PIDFILE\n";
    script << "        fi\n";
    script << "    fi\n";
    script << "    # Start daemon in background\n";
    script << "    $DAEMON >> $LOGFILE 2>&1 &\n";
    script << "    PID=$!\n";
    script << "    if [ -n \"$PID\" ]; then\n";
    script << "        echo $PID > $PIDFILE\n";
    script << "        echo \"OK\"\n";
    script << "        return 0\n";
    script << "    else\n";
    script << "        echo \"FAILED\"\n";
    script << "        return 1\n";
    script << "    fi\n";
    script << "}\n\n";
    
    // Improved stop function with proper wait and signal escalation
    script << "stop() {\n";
    script << "    echo -n \"Stopping $NAME: \"\n";
    script << "    if [ -f $PIDFILE ]; then\n";
    script << "        PID=$(cat $PIDFILE)\n";
    script << "        # First try graceful termination (SIGTERM)\n";
    script << "        if kill $PID 2>/dev/null; then\n";
    script << "            # Wait up to 10 seconds for process to terminate\n";
    script << "            TIMEOUT=10\n";
    script << "            while [ $TIMEOUT -gt 0 ]; do\n";
    script << "                if ! kill -0 $PID 2>/dev/null; then\n";
    script << "                    # Process terminated\n";
    script << "                    rm -f $PIDFILE\n";
    script << "                    echo \"OK\"\n";
    script << "                    return 0\n";
    script << "                fi\n";
    script << "                sleep 1\n";
    script << "                TIMEOUT=$((TIMEOUT - 1))\n";
    script << "            done\n";
    script << "            # Process still running, force kill (SIGKILL)\n";
    script << "            echo -n \"(force) \"\n";
    script << "            kill -9 $PID 2>/dev/null\n";
    script << "            sleep 1\n";
    script << "            rm -f $PIDFILE\n";
    script << "            echo \"OK\"\n";
    script << "        else\n";
    script << "            echo \"not running\"\n";
    script << "            rm -f $PIDFILE\n";
    script << "        fi\n";
    script << "    else\n";
    script << "        echo \"not running\"\n";
    script << "    fi\n";
    script << "    return 0\n";
    script << "}\n\n";
    
    // Status function following LSB conventions
    script << "status() {\n";
    script << "    if [ -f $PIDFILE ]; then\n";
    script << "        PID=$(cat $PIDFILE)\n";
    script << "        if kill -0 $PID 2>/dev/null; then\n";
    script << "            echo \"$NAME is running (PID $PID)\"\n";
    script << "            return 0\n";
    script << "        else\n";
    script << "            echo \"$NAME is not running (stale PID file)\"\n";
    script << "            rm -f $PIDFILE\n";
    script << "            return 1\n";
    script << "        fi\n";
    script << "    else\n";
    script << "        echo \"$NAME is not running\"\n";
    script << "        return 3\n";
    script << "    fi\n";
    script << "}\n\n";
    
    // Main case statement
    script << "case \"$1\" in\n";
    script << "    start)\n";
    script << "        start\n";
    script << "        exit $?\n";
    script << "        ;;\n";
    script << "    stop)\n";
    script << "        stop\n";
    script << "        exit $?\n";
    script << "        ;;\n";
    script << "    restart)\n";
    script << "        stop\n";
    script << "        sleep 1\n";
    script << "        start\n";
    script << "        exit $?\n";
    script << "        ;;\n";
    script << "    status)\n";
    script << "        status\n";
    script << "        exit $?\n";
    script << "        ;;\n";
    script << "    *)\n";
    script << "        echo \"Usage: $0 {start|stop|restart|status}\"\n";
    script << "        exit 1\n";
    script << "        ;;\n";
    script << "esac\n";
    
    return script.str();
}

bool sysv_service_installer::enable_service_runlevels(const std::string& service_name) {
    std::string enable_cmd;
    
    // Try update-rc.d first (Debian/Ubuntu)
    if (std::filesystem::exists("/usr/sbin/update-rc.d")) {
        enable_cmd = "update-rc.d " + service_name + " defaults >/dev/null 2>&1";
        if (execute_system_command(enable_cmd, "update-rc.d")) {
            spdlog::info("Service enabled in runlevels using update-rc.d");
            std::cout << utils::Console::success("Service enabled in runlevels") << "\n";
            return true;
        }
    }
    
    // Try chkconfig (RedHat/CentOS)
    if (std::filesystem::exists("/sbin/chkconfig")) {
        enable_cmd = "chkconfig --add " + service_name + " && chkconfig " + service_name + " on";
        if (execute_system_command(enable_cmd, "chkconfig")) {
            spdlog::info("Service enabled in runlevels using chkconfig");
            std::cout << utils::Console::success("Service enabled in runlevels") << "\n";
            return true;
        }
    }
    
    // Check for rc.conf based init system (embedded/BusyBox systems)
    if (std::filesystem::exists("/etc/rc.d/rc.conf")) {
        if (modify_rc_conf(service_name, true)) {
            return true;
        }
    }
    
    // Manual symlink creation as fallback for standard SysV
    spdlog::warn("No runlevel management tool found, checking for rc*.d directories");
    
    // Check for OpenWrt/procd system
    if (std::filesystem::exists("/sbin/procd") && std::filesystem::exists("/etc/rc.common")) {
        spdlog::info("Detected OpenWrt/procd init system");
        
        // For procd systems, use the enable command
        std::string enable_cmd = "/etc/init.d/" + service_name + " enable";
        
        if (execute_system_command(enable_cmd, "OpenWrt enable")) {
            spdlog::info("Service enabled using OpenWrt enable command");
            
            // Show what was created
            std::string start_link = "/etc/rc.d/S90" + service_name;
            std::string stop_link = "/etc/rc.d/K10" + service_name;
            
            std::cout << utils::Console::success("Service enabled for auto-start") << "\n";
            return true;
        } else {
            std::cout << utils::Console::error("Failed to enable service with OpenWrt enable command") << "\n";
            return false;
        }
    }
    // Check for traditional OpenWrt/LEDE style single rc.d directory (without procd)
    else if (std::filesystem::exists("/etc/rc.d/") && !std::filesystem::exists("/etc/rc2.d/")) {
        spdlog::info("Detected traditional OpenWrt style init system (non-procd)");
        
        // Create start symlink in /etc/rc.d/
        std::string script_path = get_service_file_path(true);
        std::string start_link = "/etc/rc.d/S90" + service_name;
        std::string stop_link = "/etc/rc.d/K10" + service_name;
        
        try {
            // Remove any existing symlinks first
            if (std::filesystem::exists(start_link)) {
                std::filesystem::remove(start_link);
            }
            if (std::filesystem::exists(stop_link)) {
                std::filesystem::remove(stop_link);
            }
            
            // Create start symlink
            std::filesystem::create_symlink(script_path, start_link);
            spdlog::info("Created OpenWrt start link: {}", start_link);
            
            // Create stop symlink
            std::filesystem::create_symlink(script_path, stop_link);
            spdlog::info("Created OpenWrt stop link: {}", stop_link);
            
            std::cout << utils::Console::success("Service enabled for auto-start") << "\n";
            std::cout << utils::Console::info("  Created: " + start_link) << "\n";
            std::cout << utils::Console::info("  Created: " + stop_link) << "\n";
            return true;
        } catch (const std::exception& e) {
            spdlog::error("Failed to create OpenWrt symlinks: {}", e.what());
            std::cout << utils::Console::error("Failed to create auto-start links: " + std::string(e.what())) << "\n";
            return false;
        }
    }
    
    // Check if standard rc*.d directories exist
    bool has_rc_dirs = false;
    for (const auto& runlevel : {"2", "3", "4", "5"}) {
        if (std::filesystem::exists("/etc/rc" + std::string(runlevel) + ".d/")) {
            has_rc_dirs = true;
            break;
        }
    }
    
    if (!has_rc_dirs) {
        spdlog::warn("No rc*.d directories found, service auto-start may not work");
        std::cout << utils::Console::warning("Could not enable auto-start: no rc*.d directories found") << "\n";
        std::cout << utils::Console::info("You may need to manually configure service startup") << "\n";
        return true;
    }
    
    // Create symlinks for standard SysV
    std::string script_path = get_service_file_path(true);
    std::vector<std::string> start_runlevels = {"2", "3", "4", "5"};
    std::vector<std::string> stop_runlevels = {"0", "1", "6"};
    
    // Create start links
    for (const auto& runlevel : start_runlevels) {
        std::string rc_dir = "/etc/rc" + runlevel + ".d/";
        if (std::filesystem::exists(rc_dir)) {
            std::string link_path = rc_dir + "S90" + service_name;
            std::filesystem::create_symlink(script_path, link_path);
            spdlog::debug("Created start link: {}", link_path);
        }
    }
    
    // Create stop links
    for (const auto& runlevel : stop_runlevels) {
        std::string rc_dir = "/etc/rc" + runlevel + ".d/";
        if (std::filesystem::exists(rc_dir)) {
            std::string link_path = rc_dir + "K10" + service_name;
            std::filesystem::create_symlink(script_path, link_path);
            spdlog::debug("Created stop link: {}", link_path);
        }
    }
    
    std::cout << utils::Console::success("Service enabled in runlevels (manual)") << "\n";
    return true;
}

bool sysv_service_installer::disable_service_runlevels(const std::string& service_name) {
    // Check for rc.conf based system first
    if (std::filesystem::exists("/etc/rc.d/rc.conf")) {
        spdlog::info("Detected rc.conf based init system, removing from rc.conf");
        modify_rc_conf(service_name, false);
    }
    // Check for OpenWrt/procd system
    else if (std::filesystem::exists("/sbin/procd") && std::filesystem::exists("/etc/rc.common")) {
        spdlog::info("Detected OpenWrt/procd init system");
        
        // For procd systems, use the disable command
        std::string disable_cmd = "/etc/init.d/" + service_name + " disable";
        
        if (execute_system_command(disable_cmd, "OpenWrt disable")) {
            spdlog::info("Service disabled using OpenWrt disable command");
            std::cout << utils::Console::success("Service auto-start disabled") << "\n";
        }
    }
    // Check for traditional OpenWrt/LEDE style single rc.d directory (without procd)
    else if (std::filesystem::exists("/etc/rc.d/") && !std::filesystem::exists("/etc/rc2.d/")) {
        spdlog::info("Detected traditional OpenWrt style init system, removing symlinks from /etc/rc.d/");
        
        // Remove OpenWrt style symlinks
        std::string start_link = "/etc/rc.d/S90" + service_name;
        std::string stop_link = "/etc/rc.d/K10" + service_name;
        std::vector<std::string> removed_links;
        
        if (std::filesystem::exists(start_link)) {
            try {
                std::filesystem::remove(start_link);
                spdlog::info("Removed OpenWrt start link: {}", start_link);
                removed_links.push_back(start_link);
            } catch (const std::exception& e) {
                spdlog::warn("Failed to remove start link: {}", e.what());
            }
        }
        
        if (std::filesystem::exists(stop_link)) {
            try {
                std::filesystem::remove(stop_link);
                spdlog::info("Removed OpenWrt stop link: {}", stop_link);
                removed_links.push_back(stop_link);
            } catch (const std::exception& e) {
                spdlog::warn("Failed to remove stop link: {}", e.what());
            }
        }
        
        if (!removed_links.empty()) {
            std::cout << utils::Console::success("Service auto-start disabled") << "\n";
            for (const auto& link : removed_links) {
                std::cout << utils::Console::info("  Removed: " + link) << "\n";
            }
        }
    }
    // Try update-rc.d first (Debian/Ubuntu)
    else if (std::filesystem::exists("/usr/sbin/update-rc.d")) {
        std::string update_rc_cmd = "update-rc.d -f " + service_name + " remove >/dev/null 2>&1";
        if (execute_system_command(update_rc_cmd, "update-rc.d remove")) {
            std::cout << utils::Console::success("Service disabled: " + service_name) << "\n";
        }
    }
    // Try chkconfig (RedHat/CentOS)
    else if (std::filesystem::exists("/sbin/chkconfig")) {
        std::string chkconfig_cmd = "chkconfig --del " + service_name + " >/dev/null 2>&1";
        if (execute_system_command(chkconfig_cmd, "chkconfig del")) {
            std::cout << utils::Console::success("Service disabled: " + service_name) << "\n";
        }
    }
    else {
        // Manual cleanup of symlinks
        remove_runlevel_symlinks(service_name);
    }
    
    // Try to disable with available commands for force stop
    if (std::filesystem::exists("/usr/sbin/update-rc.d")) {
        std::string disable_cmd = "update-rc.d " + service_name + " disable >/dev/null 2>&1";
        execute_system_command(disable_cmd);
    } else if (std::filesystem::exists("/sbin/chkconfig")) {
        std::string disable_cmd = "chkconfig " + service_name + " off";
        execute_system_command(disable_cmd);
    }
    
    return true;
}

bool sysv_service_installer::modify_rc_conf(const std::string& service_name, bool enable) {
    try {
        std::ifstream infile("/etc/rc.d/rc.conf");
        std::stringstream buffer;
        std::string line;
        bool modified = false;
        
        while (std::getline(infile, line)) {
            // Look for cfg_services line
            if (line.find("cfg_services=") == 0) {
                size_t quote_start = line.find('"');
                size_t quote_end = line.rfind('"');
                
                if (quote_start != std::string::npos && quote_end != std::string::npos && quote_start < quote_end) {
                    std::string services = line.substr(quote_start + 1, quote_end - quote_start - 1);
                    
                    if (enable && services.find(service_name) == std::string::npos) {
                        // Add our service at the end
                        services += " " + service_name;
                        line = "cfg_services=\"" + services + "\"";
                        modified = true;
                        spdlog::info("Added {} to cfg_services", service_name);
                    } else if (!enable && services.find(service_name) != std::string::npos) {
                        // Remove our service
                        size_t service_pos = services.find(service_name);
                        if (service_pos != std::string::npos) {
                            size_t start = service_pos;
                            size_t end = service_pos + service_name.length();
                            
                            // Remove leading space if exists
                            if (start > 0 && services[start - 1] == ' ') {
                                start--;
                            }
                            // Remove trailing space if exists
                            if (end < services.length() && services[end] == ' ') {
                                end++;
                            }
                            
                            services.erase(start, end - start);
                            line = "cfg_services=\"" + services + "\"";
                            modified = true;
                            spdlog::info("Removed {} from cfg_services", service_name);
                        }
                    }
                }
            }
            buffer << line << "\n";
        }
        infile.close();
        
        if (modified) {
            // Backup original file
            std::filesystem::copy_file("/etc/rc.d/rc.conf", "/etc/rc.d/rc.conf.bak", 
                                      std::filesystem::copy_options::overwrite_existing);
            
            // Write modified content
            std::ofstream outfile("/etc/rc.d/rc.conf");
            outfile << buffer.str();
            outfile.close();
            
            if (enable) {
                std::cout << utils::Console::success("Service enabled in rc.conf (auto-start configured)") << "\n";
            } else {
                std::cout << utils::Console::success("Service removed from /etc/rc.d/rc.conf") << "\n";
            }
            std::cout << utils::Console::info("Backup saved to /etc/rc.d/rc.conf.bak") << "\n";
            return true;
        } else {
            std::cout << utils::Console::warning("Could not modify rc.conf automatically") << "\n";
            if (enable) {
                std::cout << utils::Console::info("To enable auto-start, add '" + service_name + "' to cfg_services in /etc/rc.d/rc.conf") << "\n";
            }
        }
        
    } catch (const std::exception& e) {
        spdlog::warn("Failed to modify rc.conf: {}", e.what());
        std::cout << utils::Console::warning("Could not modify rc.conf: " + std::string(e.what())) << "\n";
        if (enable) {
            std::cout << utils::Console::info("To enable auto-start, add '" + service_name + "' to cfg_services in /etc/rc.d/rc.conf") << "\n";
        }
    }
    
    return false;
}

void sysv_service_installer::remove_runlevel_symlinks(const std::string& service_name) {
    spdlog::info("No runlevel management tool found, removing symlinks manually");
    int removed_count = 0;
    std::vector<std::string> removed_links;
    
    // Remove all rc*.d symlinks
    for (const auto& entry : std::filesystem::directory_iterator("/etc")) {
        if (entry.is_directory() && entry.path().filename().string().find("rc") == 0 && 
            entry.path().filename().string().find(".d") != std::string::npos) {
            
            for (const auto& link : std::filesystem::directory_iterator(entry.path())) {
                if (link.is_symlink() && link.path().filename().string().find(service_name) != std::string::npos) {
                    try {
                        std::filesystem::remove(link.path());
                        removed_count++;
                        removed_links.push_back(link.path().string());
                        spdlog::debug("Removed symlink: {}", link.path().string());
                    } catch (const std::exception& e) {
                        spdlog::warn("Failed to remove symlink {}: {}", link.path().string(), e.what());
                    }
                }
            }
        }
    }
    
    if (removed_count > 0) {
        std::cout << utils::Console::success("Removed " + std::to_string(removed_count) + " runlevel symlinks") << "\n";
        for (const auto& link : removed_links) {
            std::cout << utils::Console::info("  " + link) << "\n";
        }
    }
}

bool sysv_service_installer::kill_process_from_pid_file(const std::string& pid_file) {
    try {
        std::ifstream pid_stream(pid_file);
        std::string pid_str;
        if (std::getline(pid_stream, pid_str)) {
            pid_stream.close();
            
            // Check if process is still alive
            std::string check_cmd = "kill -0 " + pid_str + " 2>/dev/null";
            
            if (execute_system_command(check_cmd)) {
                // Process still running, force kill it
                spdlog::warn("Process {} still running after stop, sending SIGKILL", pid_str);
                std::string kill_cmd = "kill -9 " + pid_str + " 2>/dev/null";
                if (execute_system_command(kill_cmd, "kill -9")) {
                    spdlog::info("Forcefully killed process with PID: {}", pid_str);
                }
            }
        }
        
        // Always remove PID file to prevent stale state
        std::filesystem::remove(pid_file);
        spdlog::info("Removed PID file: {}", pid_file);
        return true;
    } catch (const std::exception& e) {
        spdlog::warn("Failed to handle PID file: {}", e.what());
        return false;
    }
}

} // namespace thinr::installer