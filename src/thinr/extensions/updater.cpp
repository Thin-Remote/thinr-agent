#include "updater.hpp"
#include <spdlog/spdlog.h>
#include <thinger/http_client.hpp>
#include <thinger/iotmp/core/iotmp_resource.hpp>
#include <thinger/iotmp/core/iotmp_stream_session.hpp>
#include <thinger/iotmp/extensions/cmd/cmd.hpp>
#include <filesystem>
#include <fstream>
#include <cstdio>
#include <cstdlib>
#include <csignal>
#include <array>
#include <atomic>
#include <ctime>

#include <fcntl.h>
#include <unistd.h>

#include <openssl/evp.h>

#include "../update/update_marker.hpp"
#include "../utils/atomic_file.hpp"
#include "../config/config_manager.hpp"

#ifdef __APPLE__
#include <mach-o/dyld.h>
#include <limits.h>
#endif

namespace thinr::extensions {

namespace {

// fsync a directory so a rename inside it is durable across a power cut.
void fsync_dir(const std::string& dir) {
    int fd = ::open(dir.c_str(), O_RDONLY | O_DIRECTORY);
    if (fd >= 0) {
        ::fsync(fd);
        ::close(fd);
    }
}

} // namespace

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

        // The update resource (and the playbooks that drive it) apply
        // immediately, like an explicit operator action.
        result r = run(act, channel, update_origin::iotmp);

        out["status"] = r.status;
        out["current"] = r.current;
        out["latest"] = r.latest;
        out["arch"] = r.arch;
        out["message"] = r.message;

        // Restart AFTER the response is sent so the caller sees the ack instead
        // of a connection reset. The init system brings up the new binary.
        if (r.status == "updated") {
            out.after_response([] { restart_into_new_binary(); });
        }
    };

    spdlog::info("Updater extension initialized (binary suffix: {})", AGENT_BINARY_SUFFIX);
}

updater::result updater::run(action act, const std::string& channel, update_origin origin) {
    result r;
    r.current = AGENT_VERSION;
    r.arch = AGENT_BINARY_SUFFIX;

#ifdef __ANDROID__
    // The binary lives in the read-only nativeLibraryDir; updates are
    // delivered through the Android app package.
    r.status = "unsupported";
    r.message = "updates are delivered through the Android app package";
    return r;
#endif

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

    // 4. Hand the actual swap to the single watchdog-guarded apply path.
    apply_request req;
    req.version = latest.version;
    req.channel = channel;
    req.url = std::string(CDN_BASE_URL) + "/binaries/" + channel + "/thinr-agent." + AGENT_BINARY_SUFFIX;
    req.checksum = latest.checksum;
    req.state_dir = default_state_dir();
    req.origin = origin;
    req.rollback_deadline_s = 90;

    apply_result ar = apply_with_watchdog(req);
    switch (ar.st) {
        case apply_result::status::updated:
            r.status = "updated";
            r.message = "Update applied. Restarting...";
            break;
        case apply_result::status::deferred:
            r.status = "deferred";
            r.message = ar.message.empty() ? ar.reason : ar.message;
            break;
        case apply_result::status::error:
            r.status = "error";
            r.message = ar.message.empty() ? ar.reason : ar.message;
            break;
    }
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
    // Run the downloaded binary's --version through the shared command
    // executor: it bounds the probe with a timeout (a hung binary cannot
    // block the updater) and closes the descriptors the child would otherwise
    // inherit from the daemon.
    constexpr int VERIFY_TIMEOUT_SECONDS = 10;
    std::string output, err;
    bool timed_out = false;
    int exit_code = thinger::iotmp::cmd::exec(
        binary_path, {"--version"}, output, err, "", VERIFY_TIMEOUT_SECONDS, &timed_out);

    if (timed_out || exit_code != 0) return {};

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

std::string updater::default_state_dir() {
    thinr::config::config_manager cm;
    return std::filesystem::path(cm.get_config_path()).parent_path().string();
}

std::optional<thinr::update::manifest> updater::fetch_manifest(const std::string& channel) {
    std::string url = std::string(CDN_BASE_URL) + "/" + channel + ".json";
    try {
        thinger::http::client client;
        client.timeout(std::chrono::seconds(HTTP_TIMEOUT_SECONDS))
              .verify_ssl(true)
              .user_agent("ThinRemote/" AGENT_VERSION);
        auto res = client.get(url);
        if (res.has_error() || res.status() != 200) {
            spdlog::warn("Manifest fetch failed for channel {}", channel);
            return std::nullopt;
        }
        return thinr::update::manifest::parse(res.body());
    } catch (const std::exception& e) {
        spdlog::error("Failed to fetch manifest: {}", e.what());
        return std::nullopt;
    }
}

std::string updater::binary_url(const std::string& channel) {
    return std::string(CDN_BASE_URL) + "/binaries/" + channel + "/thinr-agent." + AGENT_BINARY_SUFFIX;
}

void updater::restart_into_new_binary() {
    spdlog::info("Restarting with the updated binary");
    // Normal signal-driven shutdown (workers stop, client disconnect, extension
    // teardown) rather than std::exit, which would run static destructors under
    // live io threads and abort. The init system restarts the new binary.
    std::raise(SIGTERM);
}

std::optional<thinr::update::reverted_info> updater::last_reverted(const std::string& state_dir) {
    const std::string path =
        (std::filesystem::path(state_dir) / thinr::update::UPDATE_REVERTED_FILE).string();
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::nullopt;
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return thinr::update::reverted_info::parse(content);
}

updater::apply_result updater::apply_with_watchdog(const apply_request& req) {
    using thinr::update::update_marker;
    using thinr::update::update_phase;
    namespace fs = std::filesystem;

    apply_result r;

#ifdef __ANDROID__
    r.st = apply_result::status::error;
    r.reason = "unsupported";
    r.message = "updates are delivered through the Android app package";
    return r;
#else
    if (req.version.empty()) {
        r.st = apply_result::status::error;
        r.reason = "error";
        r.message = "empty target version";
        return r;
    }

    // Only one apply at a time in this process.
    static std::atomic<bool> in_progress{false};
    bool expected = false;
    if (!in_progress.compare_exchange_strong(expected, true)) {
        r.st = apply_result::status::error;
        r.reason = "busy";
        r.message = "another update is already in progress";
        return r;
    }
    struct release_guard {
        std::atomic<bool>& flag;
        ~release_guard() { flag.store(false); }
    } releaser{in_progress};

    const std::string state_file =
        (fs::path(req.state_dir) / thinr::update::UPDATE_STATE_FILE).string();

    // A marker already on disk means a swap is waiting for a restart (or a
    // rollback report is still owed): refuse a second swap until it clears.
    if (fs::exists(state_file)) {
        r.st = apply_result::status::error;
        r.reason = "restart_pending";
        r.message = "a previous update is pending a restart";
        return r;
    }

    // Activity gate: only the automatic poll defers on an open interactive
    // session. The CLI and the orchestrated action apply immediately.
    auto sessions_busy = [] { return thinger::iotmp::stream_session::active_count() > 0; };
    if (req.origin == update_origin::auto_poll && sessions_busy()) {
        r.st = apply_result::status::deferred;
        r.reason = "sessions_active";
        r.message = "deferred: interactive session in progress";
        return r;
    }

    const std::string install_path = get_current_binary_path();
    const std::string temp_path = install_path + ".new";
    const std::string prev_path = install_path + ".prev";
    std::error_code ec;

    spdlog::info("Applying update {} -> {} (channel {}, origin {})",
                 std::string(AGENT_VERSION), req.version, req.channel,
                 static_cast<int>(req.origin));

    // 1. Download next to install_path (same filesystem, so the swap rename is atomic).
    if (!download_binary(req.url, temp_path)) {
        fs::remove(temp_path, ec);
        r.reason = "download";
        r.message = "failed to download the update binary";
        return r;
    }

    // 2. Verify the checksum when the manifest carries one.
    if (!req.checksum.empty()) {
        std::string actual = "sha256:" + compute_sha256(temp_path);
        if (actual != req.checksum) {
            spdlog::error("Checksum mismatch: expected={}, actual={}", req.checksum, actual);
            fs::remove(temp_path, ec);
            r.reason = "checksum";
            r.message = "checksum verification failed";
            return r;
        }
        spdlog::info("Checksum verified: {}", req.checksum);
    }

    // 3. Make it executable.
    fs::permissions(temp_path,
        fs::perms::owner_exec | fs::perms::owner_read |
        fs::perms::group_exec | fs::perms::group_read |
        fs::perms::others_exec | fs::perms::others_read,
        fs::perm_options::add, ec);

    // 4. Preflight: it must run and report a version. A mismatch is logged (the
    //    main/develop channels use non-release version strings) but, with a
    //    verified checksum, the binary is the one the manifest describes.
    std::string probed = verify_binary(temp_path);
    if (probed.empty()) {
        fs::remove(temp_path, ec);
        r.reason = "preflight";
        r.message = "downloaded binary failed the --version preflight";
        return r;
    }
    if (probed != req.version) {
        spdlog::warn("Preflight version mismatch: manifest {}, binary reports {}", req.version, probed);
    }

    // 5. Re-check activity right before the irreversible steps (auto only).
    if (req.origin == update_origin::auto_poll && sessions_busy()) {
        fs::remove(temp_path, ec);
        r.st = apply_result::status::deferred;
        r.reason = "sessions_active";
        r.message = "deferred: interactive session in progress";
        return r;
    }

    // 6. Back up the current (good) binary to .prev via an atomic rename.
    const std::string prev_tmp = prev_path + ".tmp";
    fs::remove(prev_tmp, ec);
    fs::copy_file(install_path, prev_tmp, fs::copy_options::overwrite_existing, ec);
    if (ec) {
        fs::remove(temp_path, ec);
        r.reason = (ec == std::errc::no_space_on_device) ? "no_space" : "prev_failed";
        r.message = "failed to back up the current binary: " + ec.message();
        return r;
    }
    fs::rename(prev_tmp, prev_path, ec);
    if (ec) {
        fs::remove(prev_tmp, ec);
        fs::remove(temp_path, ec);
        r.reason = "prev_failed";
        r.message = "failed to stage the backup: " + ec.message();
        return r;
    }

    // 7. Write the pending marker (atomic) before the swap, so a crash between
    //    here and the rename is recoverable at the next start.
    update_marker m;
    m.phase = update_phase::pending;
    m.from = AGENT_VERSION;
    m.to = req.version;
    m.prev_binary = prev_path;
    m.started_at = static_cast<int64_t>(::time(nullptr));
    m.attempts = 0;
    m.deadline_s = req.rollback_deadline_s > 0 ? req.rollback_deadline_s : 90;
    m.report_attempts = 0;
    if (!thinr::utils::write_atomic(state_file, m.dump(), 0644)) {
        fs::remove(prev_path, ec);
        fs::remove(temp_path, ec);
        r.reason = "marker_failed";
        r.message = "failed to write the update marker";
        return r;
    }

    // 8. Swap atomically: install_path is never truncated, so a power cut here
    //    leaves either the old or the new binary whole.
    fs::rename(temp_path, install_path, ec);
    if (ec) {
        fs::remove(state_file, ec);
        fs::remove(prev_path, ec);
        fs::remove(temp_path, ec);
        r.reason = "swap_failed";
        r.message = "failed to swap the binary: " + ec.message();
        return r;
    }
    fsync_dir(fs::path(install_path).parent_path().string());

    spdlog::info("Update staged: {} -> {}; pending watchdog confirmation", std::string(AGENT_VERSION), req.version);
    r.st = apply_result::status::updated;
    r.reason = "ok";
    r.message = std::string("update applied: ") + AGENT_VERSION + " -> " + req.version;
    return r;
#endif
}

} // namespace thinr::extensions
