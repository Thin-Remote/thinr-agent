#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

namespace thinr::update {

// File names of the watchdog state, written next to the agent config.
inline constexpr const char* UPDATE_STATE_FILE = "update.state";
inline constexpr const char* UPDATE_REVERTED_FILE = "update.reverted";

enum class update_phase {
    none,        // no update in flight
    pending,     // binary swapped, waiting for the new one to prove healthy
    rolled_back, // reverted to the previous binary, report not yet delivered
};

std::string to_string(update_phase phase);
std::optional<update_phase> phase_from_string(std::string_view text);

// State written just before swapping the binary and read very early at the next
// start. It records what to revert to and how many boots the new binary has
// attempted, so a crash loop is bounded.
struct update_marker {
    update_phase phase = update_phase::none;
    std::string from;         // version running before the swap (the known-good one)
    std::string to;           // target version the swap installed
    std::string prev_binary;  // path to the saved copy of the good binary
    int64_t started_at = 0;   // epoch seconds when the swap happened (informational)
    int attempts = 0;         // boots the new binary has made without confirming
    int deadline_s = 90;      // seconds to reach STREAMS_READY before reverting
    int report_attempts = 0;  // delivery attempts of the rollback report

    nlohmann::json to_json() const;
    std::string dump() const;

    // Returns nullopt when the text is not valid JSON or carries no valid phase.
    static std::optional<update_marker> parse(std::string_view text);
    static std::optional<update_marker> from_json(const nlohmann::json& j);
};

// Durable record of the last reverted update, written by the rollback and read
// by the auto poll loop so it does not immediately retry a version that just
// failed. It outlives update.state (which is cleared once the report is sent).
struct reverted_info {
    std::string version;   // target version that failed; do not auto-retry it
    std::string restored;  // version reverted to (the one now running)
    std::string reason;    // "deadline" | "auth_failed" | "crash_loop"
    int64_t at = 0;        // epoch seconds of the rollback

    nlohmann::json to_json() const;
    std::string dump() const;

    static std::optional<reverted_info> parse(std::string_view text);
    static std::optional<reverted_info> from_json(const nlohmann::json& j);
};

} // namespace thinr::update
