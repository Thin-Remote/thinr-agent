#include "agent.hpp"
#include "../utils/system_info.hpp"
#include <spdlog/spdlog.h>
#include <thinger/thinger.h>
#include <thinger/iotmp/client.hpp>
#include <thinger/util/logger.hpp>
#include <sys/utsname.h>
#include <filesystem>

namespace thinr::agent {

agent::agent(const config::DeviceCredentials& credentials)
    : credentials_(credentials)
{
    if (!credentials_.is_valid()) {
        throw std::invalid_argument("Invalid device credentials");
    }

    client_.set_host(credentials_.host);
    client_.set_transport(thinger::iotmp::transport_type::WEBSOCKET);

    client_.set_credentials(
        credentials_.device_user,
        credentials_.device_id,
        credentials_.device_token);
    
    // Initialize all extensions by default
    nlohmann::json empty_config = {};
    init_agent_info();
    init_monitoring();
    init_updater();
    init_scripts();
    init_shell(empty_config);
    init_cmd(empty_config);
    init_proxy(empty_config);
    init_filesystem(empty_config);

    // Subscribe to remote config property
    init_property_streams();
}

agent::~agent() {
    stop();
}

void agent::start() {
    // Enable IOTMP/HTTP library logging and sync with current spdlog level
    thinger::logging::enable();
    thinger::logging::set_log_level(spdlog::get_level());

    spdlog::info("Starting ThinRemote agent");
    spdlog::info("Device: {} ({})", credentials_.device_id, credentials_.host);
    client_.start();  // Launches run_loop() on worker thread (auto-starts workers)
}

void agent::stop() {
    spdlog::info("Stopping ThinRemote agent");

    // Clear extensions
    scripts_.reset();
    monitoring_.reset();
    updater_.reset();
    shell_.reset();
    cmd_.reset();
    proxy_.reset();
    filesystem_.reset();

    // Stop workers - this calls client_.stop() first (via worker_client)
    // before destroying io_contexts, ensuring proper shutdown order
    thinger::asio::get_workers().stop();
}

void agent::wait() {
    // Wait for signals (SIGINT, SIGTERM, etc.)
    // When signal arrives: workers.stop() -> client_.stop() -> cleanup
    thinger::asio::get_workers().wait();
}

void agent::init_agent_info() {
    spdlog::info("Initializing agent info extension");

    // Override IOTMP library version with agent version
    client_["version"] = [](thinger::iotmp::output& out) {
        out["version"] = AGENT_VERSION;
    };

    // System information resource
    client_["system_info"] = [](thinger::iotmp::output& out) {
        struct utsname info;
        if (uname(&info) == 0) {
            out["system"]       = info.sysname;
            out["architecture"] = info.machine;
            out["hostname"]     = info.nodename;
            out["kernel"]       = info.release;
        }
        out["os"] = utils::SystemInfo::get_os_description();
    };
}

void agent::init_shell(const nlohmann::json& config) {
    spdlog::info("Initializing shell extension");
    shell_.emplace(client_);
    
    // Apply any configuration from config JSON if needed
    // For example: shell_->set_shell(config.value("shell", "/bin/bash"));
}

void agent::init_cmd(const nlohmann::json& config) {
    spdlog::info("Initializing cmd extension");
    cmd_.emplace(client_);
    
    // Apply any configuration from config JSON if needed
    // For example: cmd_->set_timeout(config.value("timeout", 30));
}

void agent::init_proxy(const nlohmann::json& config) {
    spdlog::info("Initializing proxy extension");
    proxy_.emplace(client_);
    
    // Apply any configuration from config JSON if needed
    // For example: proxy_->set_port(config.value("port", 8080));
}

void agent::init_filesystem(const nlohmann::json &config) {
    spdlog::info("Initializing filesystem extension");
    filesystem_.emplace(client_);
}

void agent::init_monitoring() {
    spdlog::info("Initializing monitoring extension");
    monitoring_.emplace(client_);
}

void agent::init_updater() {
    spdlog::info("Initializing updater extension");
    updater_.emplace(client_);
}

void agent::init_scripts() {
    spdlog::info("Initializing scripts extension");
    scripts_.emplace(client_);
}

void agent::init_property_streams() {
    spdlog::info("Subscribing to remote config property");

    auto& config_stream = client_.property_stream("config", true);
    config_stream.set_input([this](thinger::iotmp::input& in) {
        auto& payload = in.payload();
        if (payload.is_null() || !payload.is_object()) {
            spdlog::debug("Config property is empty, using defaults");
            return;
        }
        spdlog::info("Received remote config update");
        apply_config(payload);
    });
}

void agent::apply_config(const nlohmann::json& config) {
    // Monitoring: configurable disk paths
    if (config.contains("monitoring") && monitoring_.has_value()) {
        const auto& mon = config["monitoring"];
        if (mon.contains("disks") && mon["disks"].is_object()) {
            std::map<std::string, std::string> paths;
            for (const auto& [name, path] : mon["disks"].items()) {
                if (!path.is_string()) continue;
                auto p = path.get<std::string>();
                if (p.empty() || p[0] != '/' || p.find("..") != std::string::npos) {
                    spdlog::warn("Ignoring invalid disk path '{}': {}", name, p);
                    continue;
                }
                paths[name] = std::move(p);
            }
            monitoring_->set_disk_paths(std::move(paths));
        }
    }

    // Filesystem: configurable base path
    if (config.contains("filesystem")) {
        const auto& fs = config["filesystem"];
        if (fs.contains("base_path") && fs["base_path"].is_string()) {
            auto base_path = fs["base_path"].get<std::string>();
            if (!base_path.empty() && base_path[0] == '/' &&
                base_path.find("..") == std::string::npos &&
                std::filesystem::is_directory(base_path)) {
                spdlog::info("Reconfiguring filesystem base_path: {}", base_path);
                filesystem_->set_base_path(base_path);
            } else {
                spdlog::warn("Ignoring invalid filesystem base_path: {}", base_path);
            }
        }
    }
}

} // namespace thinr::agent