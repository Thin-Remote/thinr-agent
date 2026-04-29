#include "scripts.hpp"
#include "../installer/installation_config.hpp"
#include <spdlog/spdlog.h>
#include <thinger/iotmp/core/iotmp_resource.hpp>
#include <thinger/iotmp/extensions/cmd/cmd.hpp>
#include <algorithm>
#include <filesystem>

namespace thinr::extensions {

scripts::scripts(thinger::iotmp::client& client)
    : client_(client)
{
    register_management_resources();

    // Initial population runs during agent construction, before
    // client.start(), so the message loop can't race with us and we can
    // mutate resources_ directly without going through client.run().
    std::lock_guard<std::mutex> lock(mutex_);
    scripts_ = scan_directory();
    for (const auto& info : scripts_) {
        register_resource(info);
    }
    spdlog::info("Scripts extension initialised: {} script(s) found in {}",
                 scripts_.size(), scripts_directory());
}

std::string scripts::scripts_directory() const {
    installer::InstallationConfig config;
    return config.get_base_directory() + "/scripts";
}

std::vector<scripts::script_info> scripts::scan_directory() {
    namespace fs = std::filesystem;
    std::vector<script_info> found;

    auto dir = scripts_directory();
    if (!fs::is_directory(dir)) {
        spdlog::debug("Scripts directory not found: {}", dir);
        return found;
    }

    for (const auto& entry : fs::directory_iterator(dir)) {
        if (!entry.is_regular_file()) continue;

        auto perms = entry.status().permissions();
        if ((perms & fs::perms::owner_exec) == fs::perms::none &&
            (perms & fs::perms::group_exec) == fs::perms::none &&
            (perms & fs::perms::others_exec) == fs::perms::none) {
            spdlog::debug("Skipping non-executable file: {}", entry.path().string());
            continue;
        }

        script_info info;
        info.path = entry.path().string();
        info.name = entry.path().stem().string();
        info.describe = run_describe(info.path);
        found.push_back(std::move(info));
    }

    return found;
}

nlohmann::json scripts::reload() {
    // Scan first (I/O heavy) outside any lock — this doesn't touch the
    // client's resource map.
    auto discovered = scan_directory();

    // Mutate the resource map on the client's io_context thread so there
    // is no race with concurrent dispatch / DESCRIBE enumeration. client.run
    // posts the lambda to that thread and blocks until it returns.
    client_.run([this, &discovered]() -> bool {
        std::lock_guard<std::mutex> lock(mutex_);

        // Drop previously registered script resources before re-registering.
        // The management resources ($scripts/info, $scripts/reload) stay put.
        for (const auto& s : scripts_) {
            client_.erase_resource(s.name);
        }
        scripts_ = std::move(discovered);
        for (const auto& info : scripts_) {
            register_resource(info);
        }
        return true;
    });

    spdlog::info("Scripts reloaded from {}", scripts_directory());

    return info();
}

nlohmann::json scripts::info() const {
    std::lock_guard<std::mutex> lock(mutex_);
    nlohmann::json payload;
    payload["path"] = scripts_directory();
    payload["scripts"] = nlohmann::json::array();
    for (const auto& s : scripts_) {
        nlohmann::json entry;
        entry["name"] = s.name;
        entry["path"] = s.path;
        if (!s.describe.is_null()) entry["describe"] = s.describe;
        payload["scripts"].push_back(entry);
    }
    return payload;
}

nlohmann::json scripts::run_describe(const std::string& script_path) {
    std::string out, err;
    int exit_code = thinger::iotmp::cmd::exec(script_path, {"--describe"}, out, err, "", DESCRIBE_TIMEOUT_SECONDS);

    if (exit_code != 0) return nullptr;

    try {
        auto json = nlohmann::json::parse(out);
        if (json.is_object() && (json.contains("input") || json.contains("output"))) {
            return json;
        }
    } catch (const nlohmann::json::parse_error&) {}

    return nullptr;
}

void scripts::register_resource(const script_info& info) {
    // Scripts register at the root of the device's resource namespace so
    // they appear alongside built-in resources in the API explorer. Name
    // collisions with native resources overwrite them — the user picks
    // the filename and is responsible for avoiding clashes.
    const std::string& resource_name = info.name;
    std::string script_path = info.path;
    nlohmann::json describe_schema = info.describe;

    // Invoke the script with a JSON input (may be empty) and populate
    // the output wrapper from its stdout. Shared between the output-only
    // and input/output handler flavours below so both report failures
    // consistently.
    auto execute_script = [script_path](const std::string& input_json,
                                        thinger::iotmp::output& out)
    {
        std::string pout, perr;
        bool timeout_flag = false;
        int exit_code = thinger::iotmp::cmd::exec(
            script_path, {}, pout, perr, input_json, EXEC_TIMEOUT_SECONDS, &timeout_flag);

        if (!perr.empty()) {
            spdlog::warn("Script '{}' stderr: {}", script_path, perr);
        }

        if (timeout_flag) {
            out.set_error(408, "script timed out");
            out["exit_code"] = exit_code;
            out["stderr"] = perr;
            return;
        }

        if (exit_code != 0) {
            auto msg = perr.empty()
                ? "Script exited with code " + std::to_string(exit_code)
                : perr;
            out.set_error(500, msg.c_str());
            out["exit_code"] = exit_code;
            out["stderr"] = perr;
            return;
        }

        try {
            auto json_output = nlohmann::json::parse(pout);
            if (json_output.is_object()) {
                for (const auto& [key, value] : json_output.items()) {
                    out[key.c_str()] = value;
                }
            } else {
                out["output"] = json_output;
            }
        } catch (const nlohmann::json::parse_error&) {
            out["output"] = pout;
        }
    };

    // Scripts whose --describe advertises an input object register as
    // input/output resources. Describe copies the advertised schema
    // verbatim; normal invocation runs the script with stdin=payload.
    const bool has_input = !describe_schema.is_null()
        && describe_schema.is_object()
        && describe_schema.contains("input")
        && describe_schema["input"].is_object()
        && !describe_schema["input"].empty();

    if (has_input) {
        client_[resource_name.c_str()] = [execute_script, describe_schema](
            thinger::iotmp::input& in, thinger::iotmp::output& out)
        {
            if (in.describe()) {
                for (const auto& [key, value] : describe_schema["input"].items()) {
                    in[key.c_str()] = value;
                }
                if (describe_schema.contains("output") && describe_schema["output"].is_object()) {
                    for (const auto& [key, value] : describe_schema["output"].items()) {
                        out[key.c_str()] = value;
                    }
                }
                return;
            }
            execute_script(in.payload().dump(), out);
        };
        spdlog::debug("Registered input/output resource: {}", resource_name);
    } else {
        // Output-only: match how native Thinger resources (monitoring,
        // system, agent, …) behave — describe executes the handler so
        // /api returns the current state, not a placeholder. Safe now
        // that describe runs on the resource pool thread.
        client_[resource_name.c_str()] = [execute_script](thinger::iotmp::output& out) {
            execute_script("", out);
        };
        spdlog::debug("Registered output-only resource: {}", resource_name);
    }
}

void scripts::register_management_resources() {
    // $scripts/info — output-only: returns {path, scripts: [...]} with
    // the agent's scripts directory and the currently registered scripts.
    client_["$scripts/info"] = [this](thinger::iotmp::output& out) {
        auto payload = info();
        out["path"] = payload["path"];
        out["scripts"] = payload["scripts"];
    };

    // $scripts/reload — input-less action that rescans the directory and
    // returns the updated listing so callers don't need a second round-trip.
    client_["$scripts/reload"] = [this](thinger::iotmp::input& in, thinger::iotmp::output& out) {
        if (in.describe()) {
            out["path"] = "";
            out["scripts"] = nlohmann::json::array();
            return;
        }
        auto payload = reload();
        out["path"] = payload["path"];
        out["scripts"] = payload["scripts"];
    };

    spdlog::debug("Registered management resources: $scripts/info, $scripts/reload");
}

} // namespace thinr::extensions
