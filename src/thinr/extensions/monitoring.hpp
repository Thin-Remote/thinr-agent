#pragma once

#include <thinger/iotmp/client.hpp>
#include <nlohmann/json.hpp>
#include <chrono>
#include <cstdint>
#include <map>
#include <string>
#include <sys/utsname.h>

namespace thinr::extensions {

class monitoring {
public:
    explicit monitoring(thinger::iotmp::client& client);

    void set_disk_paths(std::map<std::string, std::string> paths);

private:
    struct cpu_sample {
        uint64_t user = 0, nice = 0, system = 0, idle = 0;
        uint64_t iowait = 0, irq = 0, softirq = 0, steal = 0;

        uint64_t total() const;
        uint64_t active() const;
    };

    struct network_sample {
        uint64_t rx_bytes = 0;
        uint64_t tx_bytes = 0;
        std::chrono::steady_clock::time_point timestamp;
    };

    cpu_sample prev_cpu_;
    network_sample prev_net_;
    std::map<std::string, std::string> disk_paths_ = {{"root", "/"}};
    nlohmann::json system_info_;
    nlohmann::json agent_info_;

    void collect(thinger::iotmp::output& out);
    double collect_cpu();
    void collect_memory(nlohmann::json& out);
    void collect_disk(nlohmann::json& out);
    void collect_load(nlohmann::json& out);
    void collect_network(nlohmann::json& out);
    uint64_t collect_uptime();
    uint32_t collect_processes();

    cpu_sample read_cpu_sample();
    network_sample read_network_sample();
#ifdef __linux__
    double read_cpu_temperature();
#endif
};

} // namespace thinr::extensions
