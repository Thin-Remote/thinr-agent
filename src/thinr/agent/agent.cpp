#include "agent.hpp"
#include <spdlog/spdlog.h>
#include <thinger/thinger.h>
#include <thinger/iotmp/client.hpp>

namespace thinr::agent {

Agent::Agent(const config::DeviceCredentials& credentials)
    : credentials_(credentials)
{
    if (!credentials_.is_valid()) {
        throw std::invalid_argument("Invalid device credentials");
    }

    client_.set_host(credentials_.host.c_str());

    client_.set_credentials(
        credentials_.device_user,
        credentials_.device_id,
        credentials_.device_token);
    
    // Initialize all extensions by default
    nlohmann::json empty_config = {};
    init_version(empty_config);
    init_shell(empty_config);
    init_cmd(empty_config);
    init_proxy(empty_config);
}

Agent::~Agent() {
    stop();
}

void Agent::start() {
    if (running_.load()) {
        spdlog::warn("Agent is already running");
        return;
    }

    spdlog::info("Starting ThinRemote agent");
    spdlog::info("Device: {} ({})", credentials_.device_id, credentials_.host);

    running_.store(true);
    agent_thread_ = std::thread(&Agent::run_agent, this);
}

void Agent::stop() {
    if (running_.load()) {
        spdlog::info("Stopping ThinRemote agent");
        running_.store(false);

        // First clear extensions that might be using the client
        version_.reset();
        shell_.reset();
        cmd_.reset();
        proxy_.reset();

        // Then stop the client
        client_.stop();

        // Stop workers
        thinger::asio::workers.stop();

        // Wait for agent thread to finish
        if (agent_thread_.joinable()) {
            agent_thread_.join();
        }
    }
}

void Agent::wait() {
    if (agent_thread_.joinable()) {
        agent_thread_.join();
    }
}

void Agent::run_agent() {
    try {
        thinger::asio::workers.start();
        client_.start();
        thinger::asio::workers.wait();
    } catch (const std::exception& e) {
        spdlog::error("Agent error: {}", e.what());
    }

    // Clean shutdown
    running_.store(false);
    spdlog::debug("Agent thread exiting");
}

void Agent::init_version(const nlohmann::json& config) {
    spdlog::info("Initializing version extension");
    version_.emplace(client_);
    
    // Apply any configuration from config JSON if needed
    // For example: version_->set_version(config.value("version", "1.0.0"));
}

void Agent::init_shell(const nlohmann::json& config) {
    spdlog::info("Initializing shell extension");
    shell_.emplace(client_);
    
    // Apply any configuration from config JSON if needed
    // For example: shell_->set_shell(config.value("shell", "/bin/bash"));
}

void Agent::init_cmd(const nlohmann::json& config) {
    spdlog::info("Initializing cmd extension");
    cmd_.emplace(client_);
    
    // Apply any configuration from config JSON if needed
    // For example: cmd_->set_timeout(config.value("timeout", 30));
}

void Agent::init_proxy(const nlohmann::json& config) {
    spdlog::info("Initializing proxy extension");
    proxy_.emplace(client_);
    
    // Apply any configuration from config JSON if needed
    // For example: proxy_->set_port(config.value("port", 8080));
}

} // namespace thinr::agent