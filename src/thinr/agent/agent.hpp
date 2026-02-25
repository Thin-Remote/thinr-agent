#pragma once

#ifndef THINREMOTE_AGENT_HPP
#define THINREMOTE_AGENT_HPP

#include "../config/config_manager.hpp"
#include <thinger/iotmp/client.hpp>
#include <nlohmann/json.hpp>
#include "thinger/iotmp/extensions/cmd/cmd.hpp"
#include "thinger/iotmp/extensions/proxy/proxy.hpp"
#include "thinger/iotmp/extensions/terminal/terminal.hpp"
#include "thinger/iotmp/extensions/fs/filesystem.hpp"
#include "../extensions/monitoring.hpp"
#include "../extensions/updater.hpp"
#include "../extensions/scripts.hpp"

namespace thinr::agent {

class agent {
public:
    explicit agent(const config::DeviceCredentials& credentials);
    ~agent();

    // Rule of 5 - disable copy/move for simplicity
    agent(const agent&) = delete;
    agent& operator=(const agent&) = delete;
    agent(agent&&) = delete;
    agent& operator=(agent&&) = delete;

    void start();
    void stop();
    void wait();

    bool is_running() const { return client_.is_running(); }

    // Access to IOTMP client for testing
    thinger::iotmp::client& get_client() { return client_; }

private:
    config::DeviceCredentials credentials_;

    // IOTMP client (inherits from worker_client)
    thinger::iotmp::client client_;

    // Extensions
    std::optional<thinger::iotmp::terminal> shell_;
    std::optional<thinger::iotmp::cmd> cmd_;
    std::optional<thinger::iotmp::proxy> proxy_;
    std::optional<thinger::iotmp::filesystem> filesystem_;
    std::optional<thinr::extensions::monitoring> monitoring_;
    std::optional<thinr::extensions::updater> updater_;
    std::optional<thinr::extensions::scripts> scripts_;

    // Extension initialization methods
    void init_agent_info();
    void init_shell(const nlohmann::json& config);
    void init_cmd(const nlohmann::json& config);
    void init_proxy(const nlohmann::json& config);
    void init_filesystem(const nlohmann::json& config);
    void init_monitoring();
    void init_updater();
    void init_scripts();

    // Remote config via IOTMP properties
    void init_property_streams();
    void apply_config(const nlohmann::json& config);
};

} // namespace thinr::agent

#endif // THINREMOTE_AGENT_HPP