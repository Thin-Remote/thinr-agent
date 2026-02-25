#pragma once

#include <thinger/iotmp/client.hpp>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace thinr::extensions {

class scripts {
public:
    explicit scripts(thinger::iotmp::client& client);

private:
    static constexpr int EXEC_TIMEOUT_SECONDS = 30;
    static constexpr int DESCRIBE_TIMEOUT_SECONDS = 2;

    struct script_info {
        std::string path;
        std::string name;
        nlohmann::json describe;
    };

    std::vector<script_info> scripts_;

    void discover_scripts(thinger::iotmp::client& client);
    nlohmann::json run_describe(const std::string& script_path);
    void register_resource(thinger::iotmp::client& client, const script_info& info);
};

} // namespace thinr::extensions
