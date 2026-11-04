#include "watchdog_core.hpp"

namespace thinr::update {

boot_decision decide_boot(const std::optional<update_marker>& marker,
                          const std::string& running_version) {
    boot_decision d;

    if (!marker || marker->phase == update_phase::none) {
        d.action = boot_action::clean_start;
        d.detail = "no update marker";
        return d;
    }

    if (marker->phase == update_phase::rolled_back) {
        d.action = boot_action::report_rolled_back;
        d.marker = *marker;
        d.detail = "previous rollback pending report";
        return d;
    }

    // phase == pending
    if (running_version != marker->to) {
        // The running binary is not the one the swap installed. Either the swap
        // never completed (still on `from`) or the marker is unrelated. Clear it
        // without reverting: we must never revert a binary that is already the
        // known-good `from`.
        d.action = boot_action::clear_no_revert;
        d.marker = *marker;
        d.detail = "running version is not the swap target; clearing without revert";
        return d;
    }

    // The new binary is running. Count this boot; if it has used up its budget
    // without confirming, it is crash-looping and must be reverted.
    update_marker armed = *marker;
    armed.attempts += 1;
    if (armed.attempts > WATCHDOG_MAX_ATTEMPTS) {
        d.action = boot_action::rollback;
        d.marker = armed;
        d.detail = "crash loop: attempts exhausted";
        return d;
    }

    d.action = boot_action::arm;
    d.marker = armed;
    d.detail = "new binary on trial; arming watchdog";
    return d;
}

runtime_action decide_runtime(runtime_event event) {
    switch (event) {
        case runtime_event::streams_ready:
            return runtime_action::confirm;
        case runtime_event::auth_failed:
            return runtime_action::rollback;
        case runtime_event::deadline:
            return runtime_action::rollback;
        case runtime_event::auth_failed_io:
            // A transport error during the handshake, not a server rejection:
            // keep waiting for a real outcome or the deadline.
            return runtime_action::ignore;
        case runtime_event::state_other:
            return runtime_action::ignore;
    }
    return runtime_action::ignore;
}

bool skip_reverted_version(const reverted_info& rev,
                           const std::string& candidate_version,
                           int64_t now, int64_t cooldown_s) {
    if (rev.version != candidate_version) return false;
    // Only a missed deadline is treated as possibly transient; a crash loop or
    // a real auth rejection means the binary itself is bad, so never retry it
    // (a genuine fix ships under a new version string and no longer matches).
    if (rev.reason != "deadline") return true;
    return (now - rev.at) < cooldown_s;
}

} // namespace thinr::update
