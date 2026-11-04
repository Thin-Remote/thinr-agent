#pragma once

#include <thinger/iotmp/client.hpp>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>

#include "../update/update_marker.hpp"
#include "../update/manifest.hpp"

namespace thinr::extensions {

class updater {
public:
    enum class action { check, apply };

    // Where an apply request comes from. Only `auto_poll` defers on an active
    // interactive session; `cli` (update --now) and `iotmp` (the update
    // resource / playbooks) apply immediately.
    enum class update_origin { auto_poll, cli, iotmp };

    struct result {
        std::string status;   // "up_to_date" | "update_available" | "updated" | "deferred" | "error"
        std::string current;
        std::string latest;
        std::string arch;
        std::string message;
    };

    // Request for the single binary-replacement path.
    struct apply_request {
        std::string version;             // target version, e.g. "v1.8.0" (non-empty)
        std::string url;                 // full URL of the binary for this arch
        std::string checksum;            // "sha256:<hex>" from the manifest, or empty
        std::string channel;             // informational / log only
        std::string state_dir;           // directory holding update.state / update.reverted
        update_origin origin = update_origin::iotmp;
        int rollback_deadline_s = 90;    // written into the marker for the watchdog
    };

    struct apply_result {
        enum class status { updated, deferred, error };
        status st = status::error;
        std::string reason;   // stable code: ok|sessions_active|busy|restart_pending|
                              // download|checksum|preflight|prev_failed|no_space|
                              // marker_failed|swap_failed|unsupported
        std::string message;  // human-readable text
    };

    explicit updater(thinger::iotmp::client& client);

    // Standalone entry point that can be invoked without an IOTMP client
    // (e.g. from the CLI). When it returns status == "updated" the caller is
    // responsible for restarting the process so the init system brings up the
    // new binary.
    static result run(action act, const std::string& channel,
                      update_origin origin = update_origin::iotmp);

    // The one and only binary-replacement path: download, verify (SHA256 +
    // --version preflight), back up the current binary to `.prev`, write the
    // pending watchdog marker and swap atomically. Never throws; on any failure
    // `install_path` is left intact. Does NOT restart: the caller restarts on
    // `updated`. Defers (without touching anything) only for `auto_poll` when an
    // interactive session is open.
    static apply_result apply_with_watchdog(const apply_request& req);

    // Clean restart into the swapped-in binary: the normal SIGTERM shutdown, so
    // the init system restarts us. Called by the daemon callers, not the CLI.
    static void restart_into_new_binary();

    // Durable record of the last reverted update (written by the watchdog on
    // rollback), so the auto poll loop does not immediately retry it.
    static std::optional<thinr::update::reverted_info> last_reverted(const std::string& state_dir);

    // Directory where the watchdog marker and reverted record live: the
    // directory of the agent config (system or user).
    static std::string default_state_dir();

    // Absolute path of the running binary (install path). Public so the
    // watchdog can restore the previous binary over it.
    static std::string get_current_binary_path();

    // Fetch and parse the full channel manifest (with the staging fields).
    // Returns nullopt on a network/HTTP error.
    static std::optional<thinr::update::manifest> fetch_manifest(const std::string& channel);

    // Full download URL of this architecture's binary for a channel.
    static std::string binary_url(const std::string& channel);

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
    static std::string compute_sha256(const std::string& file_path);
};

} // namespace thinr::extensions
