#include "agent.hpp"
#include "../utils/system_info.hpp"
#include <spdlog/spdlog.h>
#include <thinger/thinger.h>
#include <thinger/iotmp/client.hpp>
#include <thinger/util/logger.hpp>
#include <sys/utsname.h>

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
    init_shell(empty_config);
    init_cmd(empty_config);
    init_proxy(empty_config);
    init_filesystem(empty_config);
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

} // namespace thinr::agent