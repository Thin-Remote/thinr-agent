#pragma once

#ifndef THINGER_MONITOR_MONITOR_HPP
#define THINGER_MONITOR_MONITOR_HPP

// TODO: include in parent
#include <thinger/iotmp/client.hpp>

// auxiliar header files
#include "resources/cpu.hpp"
#include "resources/io.hpp"
#include "resources/memory.hpp"
#include "resources/network.hpp"
#include "resources/storage.hpp"
#include "resources/system.hpp"
#include "../config/config.hpp"

#define STR_(x) #x
#define STR(x) STR_(x)

#define VERSION STR(BUILD_VERSION) // BUILD_VERSION must always be defined

constexpr int SECTOR_SIZE = 512;

// Conversion constants. //
[[maybe_unused]] const long minute = 60;
[[maybe_unused]] const long hour = minute * 60;
[[maybe_unused]] const long day = hour * 24;

[[maybe_unused]] const std::uintmax_t btokb = 1024;
[[maybe_unused]] const std::uintmax_t kbtogb = btokb * 1024;
[[maybe_unused]] const std::uintmax_t btomb = kbtogb;
[[maybe_unused]] const std::uintmax_t btogb = kbtogb * 1024;

namespace thinger::monitor {

  class monitor{

  public:
    explicit monitor(iotmp::client& client, Config& config, httplib::Server& server);
    virtual ~monitor() = default;

    void reload();

    void get_system_values();
    void get_cpu_values();
    void get_storage_values();
    void get_io_values();
    void get_network_values();
    void get_ram_values();

    template<typename T>
    auto get(const std::string& path) -> T {
      return out_.at(path);
    }

  private:

    iotmp::client& client_;
    Config& config_;
    httplib::Server& server_;

    nlohmann::json out_;

    // network
    std::vector<network::interface> interfaces_;

    // storage
    std::vector<storage::filesystem> filesystems_;

    // io
    std::vector<io::drive> drives_;

    // CPU
    std::array<float, 3> cpu_loads{}; // 1, 5 and 15 mins loads
    unsigned int cpu_cores;

    // ram -> 0: total; 1: available; 2: swap total; 3: swap free
    std::array<unsigned long, 4> ram_{};

    // thinger.io platform
    std::string console_version;

  };

}

namespace thinger::monitor::platform {

  inline std::string getConsoleVersion() {
    httplib::Client cli("http://127.0.0.1");
    auto res = cli.Get("/v1/server/version");
    if (res.error() != httplib::Error::Success) {
      return "Could not retrieve";
    } else {
      auto res_json = nlohmann::json::parse(res->body);
      return res_json["version"].get<std::string>();
    }
  }

}

#endif //THINGER_MONITOR_MONITOR_HPP
