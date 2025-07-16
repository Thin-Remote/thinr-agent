#pragma once

#ifndef THINREMOTE_AGENT_HPP
#define THINREMOTE_AGENT_HPP

#include "../config/config_manager.hpp"
#include <atomic>
#include <thread>
#include <thinger/iotmp/client.hpp>
#include <nlohmann/json.hpp>
#include "thinger/iotmp/extensions/cmd/cmd.hpp"
#include "thinger/iotmp/extensions/proxy/proxy.hpp"
#include "thinger/iotmp/extensions/terminal/terminal.hpp"
#include "thinger/iotmp/extensions/version/version.hpp"

namespace thinr::agent {

class Agent {
public:
    explicit Agent(const config::DeviceCredentials& credentials);
    ~Agent();
    
    // Rule of 5 - disable copy/move for simplicity
    Agent(const Agent&) = delete;
    Agent& operator=(const Agent&) = delete;
    Agent(Agent&&) = delete;
    Agent& operator=(Agent&&) = delete;
    
    void start();
    void stop();
    void wait();
    
    bool is_running() const { return running_.load(); }
    
    // Access to IOTMP client for testing
    thinger::iotmp::client& get_client() { return client_; }
    
private:
    config::DeviceCredentials credentials_;
    std::atomic<bool> running_ = false;
    std::thread agent_thread_;

    // IOTMP client
    thinger::iotmp::client client_;

    // initialize client version extension
    std::optional<thinger::iotmp::version> version_;

    // initialize terminal extension
    std::optional<thinger::iotmp::terminal> shell_;

    // initialize cmd extension
    std::optional<thinger::iotmp::cmd> cmd_;

    // initialize proxy extension
    std::optional<thinger::iotmp::proxy> proxy_;
    
private:
    void run_agent();
    
    // Extension initialization methods
    void init_version(const nlohmann::json& config);
    void init_shell(const nlohmann::json& config);
    void init_cmd(const nlohmann::json& config);
    void init_proxy(const nlohmann::json& config);
};

} // namespace thinr::agent

#endif // THINREMOTE_AGENT_HPP