#pragma once

#include <optional>
#include <string>

#include <nlohmann/json.hpp>

namespace thinr::update {

enum class update_policy {
    automatic,  // "auto": poll and self-update from the channel
    managed,    // no self-polling; only orchestrated updates
};

// Where the local policy came from. Only `default` can be overridden to auto by
// the cloud; `local_explicit` (set by the user via CLI / installer flag) is
// never flipped back to auto remotely. `remote` is an in-memory state only and
// is never persisted.
enum class policy_source {
    defaulted,
    local_explicit,
    remote,
};

std::string to_string(update_policy p);
std::string to_string(policy_source s);
std::optional<update_policy> policy_from_string(std::string_view text);
policy_source source_from_string(std::string_view text);

// Bounds for the numeric fields (a broken value is clamped, never disables the
// updater or the watchdog).
inline constexpr int CHECK_INTERVAL_MIN_H = 1;
inline constexpr int CHECK_INTERVAL_MAX_H = 168;
inline constexpr int CHECK_INTERVAL_DEFAULT_H = 6;
inline constexpr int ROLLBACK_DEADLINE_MIN_S = 30;
inline constexpr int ROLLBACK_DEADLINE_MAX_S = 600;
inline constexpr int ROLLBACK_DEADLINE_DEFAULT_S = 90;

// The `update` section of the local config.
struct update_settings {
    update_policy policy = update_policy::automatic;
    policy_source source = policy_source::defaulted;
    std::string channel = "latest";
    int check_interval_h = CHECK_INTERVAL_DEFAULT_H;
    int rollback_deadline_s = ROLLBACK_DEADLINE_DEFAULT_S;

    nlohmann::json to_json() const;

    // Tolerant parse: a missing or malformed section yields defaults, each
    // invalid field falls back to its default, numerics are clamped to range.
    // Never throws.
    static update_settings from_json(const nlohmann::json& j);
};

// Returns true when `channel` is a safe channel name.
bool valid_channel(const std::string& channel);

// Effective policy from the local settings and an optional remote override.
// The cloud can always make a device more conservative (managed); it can set
// auto only when the local policy is not an explicit managed opt-out.
update_policy effective_policy(const update_settings& local,
                               std::optional<update_policy> remote);

} // namespace thinr::update
