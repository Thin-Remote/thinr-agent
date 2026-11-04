#include "update_watchdog.hpp"

#include "../extensions/updater.hpp"
#include "../update/watchdog_core.hpp"
#include "../utils/atomic_file.hpp"

#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include <boost/asio/co_spawn.hpp>

#include <chrono>
#include <csignal>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <optional>

#include <fcntl.h>
#include <unistd.h>

namespace thinr::runtime {

namespace {

namespace up = thinr::update;
namespace fs = std::filesystem;

void fsync_dir(const std::string& dir) {
    int fd = ::open(dir.c_str(), O_RDONLY | O_DIRECTORY);
    if (fd >= 0) {
        ::fsync(fd);
        ::close(fd);
    }
}

void remove_quiet(const std::string& path) {
    std::error_code ec;
    fs::remove(path, ec);
}

std::optional<up::update_marker> read_marker_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::nullopt;
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return up::update_marker::parse(content);
}

// Restore the previous binary over the running one and record the revert.
// Shared by the boot-time crash-loop rollback and the runtime deadline/auth
// rollback. Returns true when the previous binary was restored.
bool do_rollback(const std::string& state_dir, const up::update_marker& m, const std::string& reason) {
    const std::string install = thinr::extensions::updater::get_current_binary_path();
    const std::string reverted_file = (fs::path(state_dir) / up::UPDATE_REVERTED_FILE).string();
    const std::string marker_file = (fs::path(state_dir) / up::UPDATE_STATE_FILE).string();

    std::error_code ec;
    if (m.prev_binary.empty() || !fs::exists(m.prev_binary)) {
        // No backup to restore. Do not touch the marker and do not record a
        // revert that did not happen: leaving the state intact keeps protection
        // armed and lets the caller decide to keep running this binary rather
        // than disarm and loop on it forever.
        spdlog::error("Watchdog cannot revert {}: previous binary missing ({})", m.to, m.prev_binary);
        return false;
    }

    fs::rename(m.prev_binary, install, ec);
    if (ec) {
        spdlog::error("Watchdog failed to restore previous binary: {}", ec.message());
        return false;
    }
    fsync_dir(fs::path(install).parent_path().string());

    // The previous binary is back in place. Now (and only now) record the
    // reverted version so the poll loop does not immediately re-apply it, and
    // leave a rolled_back marker so the next start reports the failure.
    up::reverted_info rv;
    rv.version = m.to;
    rv.restored = m.from;
    rv.reason = reason;
    rv.at = static_cast<int64_t>(::time(nullptr));
    thinr::utils::write_atomic(reverted_file, rv.dump(), 0644);

    up::update_marker done = m;
    done.phase = up::update_phase::rolled_back;
    thinr::utils::write_atomic(marker_file, done.dump(), 0644);

    spdlog::warn("Watchdog reverted {} -> {} ({})", m.to, m.from, reason);
    return true;
}

} // namespace

update_watchdog::boot_outcome update_watchdog::check_boot(const std::string& state_dir,
                                                          const std::string& running_version) {
    const std::string marker_file = (fs::path(state_dir) / up::UPDATE_STATE_FILE).string();
    auto marker = read_marker_file(marker_file);

    auto decision = up::decide_boot(marker, running_version);
    switch (decision.action) {
        case up::boot_action::clean_start:
            return boot_outcome::proceed;

        case up::boot_action::clear_no_revert:
            spdlog::info("Watchdog: {}", decision.detail);
            remove_quiet(marker_file);
            if (!decision.marker.prev_binary.empty()) remove_quiet(decision.marker.prev_binary);
            return boot_outcome::proceed;

        case up::boot_action::report_rolled_back:
            // Keep the marker; the runtime half reports it once connected.
            return boot_outcome::proceed;

        case up::boot_action::arm:
            if (!thinr::utils::write_atomic(marker_file, decision.marker.dump(), 0644)) {
                spdlog::error("Watchdog failed to persist attempt count");
            }
            spdlog::info("Watchdog armed: {} on trial (attempt {})",
                         decision.marker.to, decision.marker.attempts);
            return boot_outcome::proceed;

        case up::boot_action::rollback:
            spdlog::warn("Watchdog: {}", decision.detail);
            if (do_rollback(state_dir, decision.marker, "crash_loop")) {
                return boot_outcome::reverted_exit;
            }
            // The revert failed (backup gone or the swap could not be undone).
            // Exiting now would just restart into the same failing binary
            // forever; instead keep the marker and let this binary run so the
            // device stays reachable for manual recovery.
            spdlog::error("Watchdog: could not revert a crash-looping update; "
                          "continuing on the current binary");
            return boot_outcome::proceed;
    }
    return boot_outcome::proceed;
}

update_watchdog::update_watchdog(thinger::iotmp::client& client,
                                 client_state_dispatcher& dispatcher,
                                 std::string state_dir,
                                 std::string running_version)
    : client_(client),
      state_dir_(std::move(state_dir)),
      running_version_(std::move(running_version)) {

    if (auto marker = read_marker_file(marker_path())) {
        marker_ = *marker;
        if (marker_.phase == up::update_phase::pending && running_version_ == marker_.to) {
            armed_ = true;
            deadline_s_ = marker_.deadline_s > 0 ? marker_.deadline_s : 90;
        } else if (marker_.phase == up::update_phase::rolled_back) {
            report_pending_ = true;
        }
    }

    // Subscribe before the client starts so STREAMS_READY is never missed.
    sub_ = dispatcher.subscribe([this](thinger::iotmp::client_state s, const std::string& r) {
        on_state(s, r);
    });

    if (armed_) {
        deadline_thread_ = std::thread([this] { deadline_loop(); });
        spdlog::info("Watchdog watching {} for up to {}s", marker_.to, deadline_s_);
    }
}

update_watchdog::~update_watchdog() {
    stop();
}

void update_watchdog::stop() {
    {
        std::lock_guard<std::mutex> lk(deadline_mutex_);
        deadline_stop_ = true;
    }
    deadline_cv_.notify_all();
    if (deadline_thread_.joinable()) deadline_thread_.join();
    sub_.reset();
}

std::string update_watchdog::marker_path() const {
    return (fs::path(state_dir_) / up::UPDATE_STATE_FILE).string();
}

void update_watchdog::deadline_loop() {
    std::unique_lock<std::mutex> lk(deadline_mutex_);
    deadline_cv_.wait_for(lk, std::chrono::seconds(deadline_s_), [this] { return deadline_stop_; });
    bool stop = deadline_stop_;
    lk.unlock();
    if (stop) return;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (resolved_) return;
        resolved_ = true;
    }
    rollback("deadline");
}

void update_watchdog::on_state(thinger::iotmp::client_state state, const std::string& reason) {
    using cs = thinger::iotmp::client_state;
    if (!armed_ && !report_pending_) return;

    up::runtime_event ev;
    if (state == cs::STREAMS_READY) {
        ev = up::runtime_event::streams_ready;
    } else if (state == cs::AUTH_FAILED) {
        ev = (reason == "io_error") ? up::runtime_event::auth_failed_io : up::runtime_event::auth_failed;
    } else {
        ev = up::runtime_event::state_other;
    }

    if (ev == up::runtime_event::streams_ready) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (resolved_) return;
            resolved_ = true;
        }
        if (report_pending_) {
            auto rv = thinr::extensions::updater::last_reverted(state_dir_);
            report("rolled_back", marker_.from, marker_.to, rv ? rv->reason : "reverted");
            remove_quiet(marker_path());
            report_pending_ = false;
        } else if (armed_) {
            confirm();
        }
        return;
    }

    if (!armed_) return;
    if (up::decide_runtime(ev) == up::runtime_action::rollback) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (resolved_) return;
            resolved_ = true;
        }
        rollback("auth_failed");
    }
}

void update_watchdog::confirm() {
    {
        std::lock_guard<std::mutex> lk(deadline_mutex_);
        deadline_stop_ = true;
    }
    deadline_cv_.notify_all();

    remove_quiet(marker_path());
    if (!marker_.prev_binary.empty()) remove_quiet(marker_.prev_binary);
    spdlog::info("Watchdog confirmed update to {}", marker_.to);
    report("confirmed", marker_.from, marker_.to, "ok");
}

void update_watchdog::rollback(const std::string& reason) {
    {
        std::lock_guard<std::mutex> lk(deadline_mutex_);
        deadline_stop_ = true;
    }
    deadline_cv_.notify_all();

    if (do_rollback(state_dir_, marker_, reason)) {
        // Restart into the restored (previous) binary.
        std::raise(SIGTERM);
    }
}

void update_watchdog::report(const char* event, const std::string& from, const std::string& to,
                             const std::string& reason) {
    if (!client_.is_connected()) {
        spdlog::warn("Watchdog: update_report '{}' skipped (not connected)", event);
        return;
    }
    nlohmann::json payload = {
        {"event", event},
        {"from", from},
        {"to", to},
        {"reason", reason},
        {"at", static_cast<int64_t>(::time(nullptr))},
    };
    try {
        std::string ev(event);
        boost::asio::co_spawn(client_.get_io_context(),
            client_.set_property("update_report", payload, false),
            [ev](std::exception_ptr e, bool ok) {
                if (e || !ok) {
                    spdlog::warn("Watchdog: update_report '{}' not delivered", ev);
                } else {
                    spdlog::info("Watchdog: update_report '{}' delivered", ev);
                }
            });
    } catch (const std::exception& e) {
        spdlog::warn("Watchdog: update_report '{}' failed to schedule: {}", event, e.what());
    }
}

} // namespace thinr::runtime
