#pragma once

#include <thread>
#include <chrono>
#include <nlohmann/json.hpp>
#include <httplib.h>
#include <spdlog/spdlog.h>

namespace platform::utils::influxdb {

    std::string get_version() { // I could always execute a command in the docker container¿?
        httplib::Client cli("http://localhost:8086");

        // Wait for influxdb to be up for up to 1 min
        auto res = cli.Get("/ping");
        for (int i = 0; i < 6; i++) {

            if (res.error() != httplib::Error::Success) {
              LOG_WARNING("[_INFLUX] Can't ping influxdb /ping endpoint. Trying again in 10 seconds.");
              std::this_thread::sleep_for(std::chrono::seconds(10));
              res = cli.Get("/ping");
            } else {
                break;
            }

        }

        if (res.error() != httplib::Error::Success) {
            LOG_ERROR("[_INFLUX] Can't ping influxdb /ping endpoint.");
            return "false"; // TODO: throw exception
        }

        std::string version = res->get_header_value("X-Influxdb-Version");

        return version;
    }

}
