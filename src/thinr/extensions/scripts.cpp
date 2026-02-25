#include "scripts.hpp"
#include "../installer/installation_config.hpp"
#include <spdlog/spdlog.h>
#include <thinger/iotmp/core/iotmp_resource.hpp>
#include <thinger/iotmp/extensions/cmd/cmd.hpp>
#include <filesystem>

namespace thinr::extensions {

scripts::scripts(thinger::iotmp::client& client) {
    discover_scripts(client);
}

void scripts::discover_scripts(thinger::iotmp::client& client) {
    namespace fs = std::filesystem;

    installer::InstallationConfig config;
    auto scripts_dir = config.get_base_directory() + "/scripts";

    if (!fs::is_directory(scripts_dir)) {
        spdlog::debug("Scripts directory not found: {}", scripts_dir);
        return;
    }

    for (const auto& entry : fs::directory_iterator(scripts_dir)) {
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

        if (info.describe.is_null()) {
            spdlog::info("Discovered script: {} (no schema)", info.name);
        } else {
            spdlog::info("Discovered script: {} (with schema)", info.name);
        }

        register_resource(client, info);
        scripts_.push_back(std::move(info));
    }

    if (scripts_.empty()) {
        spdlog::info("No executable scripts found in {}", scripts_dir);
    } else {
        spdlog::info("Scripts extension initialized: {} script(s) registered", scripts_.size());
    }
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

void scripts::register_resource(thinger::iotmp::client& client, const script_info& info) {
    std::string resource_name = std::string("scripts/") + info.name;
    std::string script_path = info.path;
    nlohmann::json describe_schema = info.describe;

    client[resource_name.c_str()] = [script_path, describe_schema](
        thinger::iotmp::input& in, thinger::iotmp::output& out)
    {
        if (in.describe()) {
            if (!describe_schema.is_null()) {
                if (describe_schema.contains("input") && describe_schema["input"].is_object()) {
                    for (const auto& [key, value] : describe_schema["input"].items()) {
                        in[key.c_str()] = value;
                    }
                }
                if (describe_schema.contains("output") && describe_schema["output"].is_object()) {
                    for (const auto& [key, value] : describe_schema["output"].items()) {
                        out[key.c_str()] = value;
                    }
                }
            } else {
                in["input"] = "";
                out["output"] = "";
            }
            return;
        }

        // serialize input and execute
        std::string input_json = in.payload().dump();
        std::string pout, perr;
        bool timeout_flag = false;
        int exit_code = thinger::iotmp::cmd::exec(script_path, {}, pout, perr, input_json, EXEC_TIMEOUT_SECONDS, &timeout_flag);

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

        // try to parse stdout as JSON
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

    spdlog::debug("Registered resource: {}", resource_name);
}

} // namespace thinr::extensions
