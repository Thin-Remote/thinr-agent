#pragma once

#include <thinger/iotmp/client.hpp>
#include <nlohmann/json.hpp>
#include <string>

namespace thinr::extensions {

class updater {
public:
    explicit updater(thinger::iotmp::client& client);

private:
    static constexpr const char* CDN_BASE_URL = "https://get.thinremote.io";
    static constexpr int HTTP_TIMEOUT_SECONDS = 30;
    static constexpr int DOWNLOAD_TIMEOUT_SECONDS = 300;

    struct version_info {
        std::string version;
        std::string checksum;  // expected sha256 for current platform
    };

    void perform_update(thinger::iotmp::input& in, thinger::iotmp::output& out);

    version_info fetch_latest_version(const std::string& channel);
    bool download_binary(const std::string& url, const std::string& dest_path);
    std::string verify_binary(const std::string& binary_path);
    static std::string get_current_binary_path();
    static std::string compute_sha256(const std::string& file_path);
};

} // namespace thinr::extensions
