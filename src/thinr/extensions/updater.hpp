#pragma once

#include <thinger/iotmp/client.hpp>
#include <nlohmann/json.hpp>
#include <string>

namespace thinr::extensions {

class updater {
public:
    enum class action { check, apply };

    struct result {
        std::string status;   // "up_to_date" | "update_available" | "updated" | "error"
        std::string current;
        std::string latest;
        std::string arch;
        std::string message;
    };

    explicit updater(thinger::iotmp::client& client);

    // Standalone entry point that can be invoked without an IOTMP client
    // (e.g. from the CLI). When it returns status == "updated" the caller is
    // responsible for exiting the process so the init system restarts it.
    static result run(action act, const std::string& channel);

private:
    static constexpr const char* CDN_BASE_URL = "https://get.thinremote.io";
    static constexpr int HTTP_TIMEOUT_SECONDS = 30;
    static constexpr int DOWNLOAD_TIMEOUT_SECONDS = 300;

    struct version_info {
        std::string version;
        std::string checksum;  // expected sha256 for current platform
    };

    static version_info fetch_latest_version(const std::string& channel);
    static bool download_binary(const std::string& url, const std::string& dest_path);
    static std::string verify_binary(const std::string& binary_path);
    static std::string get_current_binary_path();
    static std::string compute_sha256(const std::string& file_path);
};

} // namespace thinr::extensions
