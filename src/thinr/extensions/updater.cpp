#include "updater.hpp"
#include <spdlog/spdlog.h>
#include <thinger/http_client.hpp>
#include <thinger/iotmp/core/iotmp_resource.hpp>
#include <filesystem>
#include <fstream>
#include <cstdio>
#include <cstdlib>
#include <array>

#include <openssl/evp.h>

#ifdef __APPLE__
#include <mach-o/dyld.h>
#include <limits.h>
#endif

namespace thinr::extensions {

updater::updater(thinger::iotmp::client& client) {
    client["update"] = [](thinger::iotmp::input& in, thinger::iotmp::output& out) {
        if (in.describe()) {
            in["action"] = "check";
            in["channel"] = "latest";
            out["status"] = "";
            out["current"] = "";
            out["latest"] = "";
            out["arch"] = "";
            out["message"] = "";
            return;
        }

        auto channel = thinger::iotmp::get_value(in.payload(), "channel", std::string("latest"));
        auto action_str = thinger::iotmp::get_value(in.payload(), "action", std::string("check"));
        action act = (action_str == "apply") ? action::apply : action::check;

        result r = run(act, channel);

        out["status"] = r.status;
        out["current"] = r.current;
        out["latest"] = r.latest;
        out["arch"] = r.arch;
        out["message"] = r.message;

        // Exit AFTER the response is sent so the caller sees the ack instead
        // of a connection-reset / timeout. The init system will restart the
        // agent with the new binary.
        if (r.status == "updated") {
            out.after_response([] {
                spdlog::info("Exiting for restart with updated binary");
                std::exit(0);
            });
        }
    };

    spdlog::info("Updater extension initialized (binary suffix: {})", AGENT_BINARY_SUFFIX);
}

updater::result updater::run(action act, const std::string& channel) {
    result r;
    r.current = AGENT_VERSION;
    r.arch = AGENT_BINARY_SUFFIX;

    spdlog::info("Update check requested (current version: {}, channel: {})", r.current, channel);

    // 1. Fetch latest version info from CDN
    version_info latest;
    try {
        latest = fetch_latest_version(channel);
    } catch (const std::exception& e) {
        spdlog::error("Failed to fetch latest version: {}", e.what());
        r.status = "error";
        r.message = std::string("Failed to check for updates: ") + e.what();
        return r;
    }

    if (latest.version.empty()) {
        r.status = "error";
        r.message = "Empty version response from CDN";
        return r;
    }

    r.latest = latest.version;
    spdlog::info("Latest version available: {}", latest.version);

    // 2. Compare versions
    if (r.current == latest.version) {
        spdlog::info("Agent is already up to date");
        r.status = "up_to_date";
        return r;
    }

    // 3. Check action: "check" (default) only reports, "apply" performs the update
    if (act != action::apply) {
        spdlog::info("Update available but action is 'check', not applying");
        r.status = "update_available";
        return r;
    }

    // 4. Build download URL and download
    std::string download_url = std::string(CDN_BASE_URL) +
                               "/binaries/" + channel + "/thinr-agent." + AGENT_BINARY_SUFFIX;
    std::string temp_path = "/tmp/thinr-agent-update";

    spdlog::info("Downloading update from: {}", download_url);

    if (!download_binary(download_url, temp_path)) {
        r.status = "error";
        r.message = "Failed to download update binary";
        return r;
    }

    // 5. Verify downloaded binary
    if (!latest.checksum.empty()) {
        // Checksum available: verify integrity via SHA256
        std::string actual_hash = "sha256:" + compute_sha256(temp_path);
        if (actual_hash != latest.checksum) {
            spdlog::error("Checksum mismatch: expected={}, actual={}", latest.checksum, actual_hash);
            std::filesystem::remove(temp_path);
            r.status = "error";
            r.message = "Checksum verification failed";
            return r;
        }
        spdlog::info("Checksum verified: {}", latest.checksum);
    } else {
        // No checksum: fallback to executing --version to verify it's a valid binary
        std::filesystem::permissions(temp_path,
            std::filesystem::perms::owner_exec | std::filesystem::perms::owner_read |
            std::filesystem::perms::group_exec | std::filesystem::perms::group_read,
            std::filesystem::perm_options::add);

        std::string downloaded_version = verify_binary(temp_path);
        if (downloaded_version.empty()) {
            spdlog::error("Downloaded binary verification failed (no checksum available)");
            std::filesystem::remove(temp_path);
            r.status = "error";
            r.message = "Downloaded binary failed verification";
            return r;
        }
        spdlog::info("Binary verified via --version: {}", downloaded_version);
    }

    // 6. Get current binary path and replace
    std::string install_path = get_current_binary_path();
    spdlog::info("Replacing binary at: {}", install_path);

    // Remove running binary first (Linux allows deleting a busy executable,
    // the process keeps running via its open file descriptor)
    std::error_code ec;
    std::filesystem::remove(install_path, ec);
    if (ec) {
        spdlog::error("Failed to remove current binary: {}", ec.message());
    }

    ec.clear();
    std::filesystem::copy_file(temp_path, install_path, ec);

    // Clean up temp file
    std::filesystem::remove(temp_path);

    if (ec) {
        spdlog::error("Failed to replace binary: {}", ec.message());
        r.status = "error";
        r.message = std::string("Failed to replace binary: ") + ec.message();
        return r;
    }

    // 7. Ensure executable permissions on installed binary
    std::filesystem::permissions(install_path,
        std::filesystem::perms::owner_exec | std::filesystem::perms::owner_read |
        std::filesystem::perms::group_exec | std::filesystem::perms::group_read |
        std::filesystem::perms::others_exec | std::filesystem::perms::others_read,
        std::filesystem::perm_options::add);

    spdlog::info("Update successful: {} -> {}", r.current, latest.version);
    r.status = "updated";
    r.message = "Update applied. Restarting...";
    return r;
}

updater::version_info updater::fetch_latest_version(const std::string& channel) {
    std::string url = std::string(CDN_BASE_URL) + "/" + channel + ".json";

    thinger::http::client client;
    client.timeout(std::chrono::seconds(HTTP_TIMEOUT_SECONDS))
          .verify_ssl(true)
          .user_agent("ThinRemote/" AGENT_VERSION);

    auto res = client.get(url);

    if (res.has_error()) {
        throw std::runtime_error(res.error());
    }

    if (res.status() != 200) {
        throw std::runtime_error("HTTP " + std::to_string(res.status()));
    }

    auto json = nlohmann::json::parse(res.body());

    version_info info;
    info.version = json.at("version").get<std::string>();

    // Extract checksum for current platform if available
    if (json.contains("checksums") && json["checksums"].contains(AGENT_BINARY_SUFFIX)) {
        info.checksum = json["checksums"][AGENT_BINARY_SUFFIX].get<std::string>();
    }

    return info;
}

bool updater::download_binary(const std::string& url, const std::string& dest_path) {
    try {
        thinger::http::client client;
        client.timeout(std::chrono::seconds(DOWNLOAD_TIMEOUT_SECONDS))
              .verify_ssl(true)
              .user_agent("ThinRemote/" AGENT_VERSION);

        auto res = client.request(url).download(dest_path);

        if (res.has_network_error()) {
            spdlog::error("Download error: {}", res.error);
            return false;
        }

        if (res.status_code != 200) {
            spdlog::error("Download failed with HTTP {}", res.status_code);
            return false;
        }

        if (res.bytes_transferred == 0) {
            spdlog::error("Downloaded empty binary");
            return false;
        }

        spdlog::info("Streamed {} bytes to {}", res.bytes_transferred, dest_path);
        return true;

    } catch (const std::exception& e) {
        spdlog::error("Download exception: {}", e.what());
        return false;
    }
}

std::string updater::verify_binary(const std::string& binary_path) {
    std::string cmd = binary_path + " --version 2>/dev/null";
    std::array<char, 256> buffer{};
    std::string output;

    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return {};

    while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
        output += buffer.data();
    }

    int status = pclose(pipe);
    if (status != 0) return {};

    // Parse "thinr-agent version <VERSION>"
    const std::string prefix = "thinr-agent version ";
    auto pos = output.find(prefix);
    if (pos == std::string::npos) return {};

    std::string version = output.substr(pos + prefix.size());
    while (!version.empty() && (version.back() == '\n' || version.back() == '\r' || version.back() == ' ')) {
        version.pop_back();
    }

    return version;
}

std::string updater::compute_sha256(const std::string& file_path) {
    std::ifstream file(file_path, std::ios::binary);
    if (!file) return {};

    auto ctx = EVP_MD_CTX_new();
    if (!ctx) return {};

    EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr);

    char buffer[8192];
    while (file.read(buffer, sizeof(buffer)) || file.gcount() > 0) {
        EVP_DigestUpdate(ctx, buffer, static_cast<size_t>(file.gcount()));
        if (file.eof()) break;
    }

    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int hash_len = 0;
    EVP_DigestFinal_ex(ctx, hash, &hash_len);
    EVP_MD_CTX_free(ctx);

    // Convert to hex string
    std::string hex;
    hex.reserve(hash_len * 2);
    for (unsigned int i = 0; i < hash_len; ++i) {
        char buf[3];
        snprintf(buf, sizeof(buf), "%02x", hash[i]);
        hex += buf;
    }

    return hex;
}

std::string updater::get_current_binary_path() {
    char path[1024];

#ifdef __APPLE__
    uint32_t size = sizeof(path);
    if (_NSGetExecutablePath(path, &size) == 0) {
        char resolved_path[PATH_MAX];
        if (realpath(path, resolved_path) != nullptr) {
            return std::string(resolved_path);
        }
        return std::string(path);
    }
#else
    ssize_t count = readlink("/proc/self/exe", path, sizeof(path) - 1);
    if (count != -1) {
        path[count] = '\0';
        return std::string(path);
    }
#endif

    return "./thinr-agent";
}

} // namespace thinr::extensions
