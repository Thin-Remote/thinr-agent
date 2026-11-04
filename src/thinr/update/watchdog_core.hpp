#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "update_marker.hpp"

namespace thinr::update {

// A new binary gets this many boots to prove itself before the watchdog treats
// it as a crash loop and reverts.
inline constexpr int WATCHDOG_MAX_ATTEMPTS = 3;

// Give up re-sending the rollback report after this many boots.
inline constexpr int WATCHDOG_MAX_REPORT_ATTEMPTS = 5;

// ---- Early-boot decision ---------------------------------------------------

enum class boot_action {
    clean_start,         // no actionable marker: clear any stale marker/prev and start
    report_rolled_back,  // a previous rollback still owes a report: send it, then clear
    arm,                 // this is the new binary and within the attempt budget: arm the watchdog
    rollback,            // the new binary is crash-looping: revert now, before bringing anything up
    clear_no_revert,     // marker present but this is not the target (swap never happened): clear, never revert
};

struct boot_decision {
    boot_action action;
    update_marker marker;  // for `arm`, carries attempts already incremented and ready to persist
    std::string detail;    // human-readable reason for the log
};

// Decides what to do at process start. `running_version` is the AGENT_VERSION of
// the process that just started. The marker is whatever was read from disk (or
// nullopt if absent / unreadable).
boot_decision decide_boot(const std::optional<update_marker>& marker,
                          const std::string& running_version);

// ---- Runtime decision (once armed) ----------------------------------------

enum class runtime_event {
    streams_ready,    // client reached STREAMS_READY: fully operational
    auth_failed,      // server rejected authentication (a real rejection)
    auth_failed_io,   // AUTH_FAILED emitted by a transport error, not a rejection
    state_other,      // any other intermediate state
    deadline,         // the watchdog deadline elapsed without STREAMS_READY
};

enum class runtime_action {
    ignore,   // not decisive: keep waiting for STREAMS_READY or the deadline
    confirm,  // success: clear the marker and the saved previous binary, report success
    rollback, // failure: restore the previous binary, mark rolled_back and restart
};

runtime_action decide_runtime(runtime_event event);

// ---- Reverted-version retry decision (auto poll loop) ---------------------

// Decides whether the auto-poll loop should skip a candidate version because it
// was just reverted. A binary reverted for being bad (a crash loop or a real
// auth rejection) is skipped for good; one reverted for a possibly-transient
// missed deadline is skipped only until `cooldown_s` has elapsed since the
// revert, so a brief outage does not pin the device to the old version. A
// revert of a different version never blocks the candidate.
bool skip_reverted_version(const reverted_info& rev,
                           const std::string& candidate_version,
                           int64_t now, int64_t cooldown_s);

} // namespace thinr::update
