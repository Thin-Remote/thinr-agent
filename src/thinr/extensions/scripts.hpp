#pragma once

#include <thinger/iotmp/client.hpp>
#include <nlohmann/json.hpp>
#include <mutex>
#include <string>
#include <vector>

namespace thinr::extensions {

class scripts {
public:
    explicit scripts(thinger::iotmp::client& client);

    // Rescan the scripts directory, unregistering scripts that are no
    // longer present on disk and (re-)registering the rest with fresh
    // --describe schemas. Thread-safe. Returns the post-reload info
    // payload so handlers can surface it to the caller.
    nlohmann::json reload();

    // Snapshot of the current registered scripts. Safe to call from
    // any thread.
    nlohmann::json info() const;

private:
    static constexpr int EXEC_TIMEOUT_SECONDS = 30;
    static constexpr int DESCRIBE_TIMEOUT_SECONDS = 2;

    struct script_info {
        std::string path;
        std::string name;
        nlohmann::json describe;
    };

    thinger::iotmp::client& client_;
    mutable std::mutex mutex_;
    std::vector<script_info> scripts_;  // guarded by mutex_

    // Absolute path to the scripts directory, derived from the agent's
    // installation config.
    std::string scripts_directory() const;

    // Scan the scripts directory and build a fresh vector of script_info.
    // Caller owns synchronisation.
    std::vector<script_info> scan_directory();

    // Register internal management resources ($scripts/info, $scripts/reload).
    // Called once from the constructor.
    void register_management_resources();

    // Register a single script as an iotmp resource under scripts/<name>.
    void register_resource(const script_info& info);

    // Run the script with --describe and parse its JSON output as the
    // {input, output} schema. Returns nullptr on any failure.
    nlohmann::json run_describe(const std::string& script_path);
};

} // namespace thinr::extensions
