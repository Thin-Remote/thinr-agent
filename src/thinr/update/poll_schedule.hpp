#pragma once

#include <algorithm>
#include <cstdint>

namespace thinr::update {

// Pure helpers for the poll cadence. `u` is a uniform random value in [0,1)
// supplied by the caller (injectable, so tests are deterministic).

// First poll after start: uniform in [0, 30 min].
inline double first_poll_delay_s(double u) {
    return std::clamp(u, 0.0, 1.0) * 30.0 * 60.0;
}

// Normal interval: check_interval_h hours with +/-10% jitter.
inline double normal_poll_delay_s(int check_interval_h, double u) {
    const double jitter = 0.9 + 0.2 * std::clamp(u, 0.0, 1.0);
    return static_cast<double>(check_interval_h) * 3600.0 * jitter;
}

// Retry after deferring for an active session.
inline constexpr int DEFERRED_RETRY_DELAY_S = 10 * 60;

// A version reverted for a possibly-transient reason (a missed connect deadline,
// e.g. a brief cloud outage) is retried after this cooldown rather than skipped
// forever, so a short outage does not pin a slice of the fleet to the old
// version until the next release.
inline constexpr int64_t REVERTED_RETRY_COOLDOWN_S = 24 * 60 * 60;

} // namespace thinr::update
