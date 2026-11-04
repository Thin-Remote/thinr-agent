#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

namespace thinr::update {

// Parsed channel manifest (get.thinremote.io/<channel>.json).
//
//   { "version": "v1.8.0",
//     "published_at": 1762000000, "soak_h": 72, "ramp_h": 96,
//     "frozen_at": 1762500000,                      (optional)
//     "checksums": { "<arch>": "sha256:<hex>" } }
//
// The staging block (published_at + soak_h + ramp_h) is optional as a whole:
// either all three are present and valid, or there is no staging (legacy
// manifests of main/develop, eligible immediately). A partial or invalid block
// is treated as invalid (not eligible), never inherited.
struct manifest {
    std::string version;
    std::map<std::string, std::string> checksums;

    bool valid = false;           // has a non-empty version

    bool has_staging = false;     // all three staging fields present and valid
    bool staging_invalid = false; // staging fields present but malformed/partial
    int64_t published_at = 0;
    double soak_h = 0;
    double ramp_h = 0;

    bool has_frozen = false;
    int64_t frozen_at = 0;

    static manifest parse(std::string_view text);
    std::optional<std::string> checksum_for(const std::string& arch) const;
};

enum class eligibility {
    eligible,         // staged rollout reached this device
    legacy_eligible,  // no staging block: eligible immediately
    soak,             // still in the soak period
    ramp,             // in the ramp but this device's turn has not come
    invalid,          // malformed staging block
    clock,            // device clock earlier than published_at
};

bool is_eligible(eligibility e);

// Device's fixed fraction in [0,1): uint64 big-endian of the first 8 bytes of
// SHA-256("<device_id>:<version>") divided by 2^64, clamped below 1.
double device_fraction(const std::string& device_id, const std::string& version);

// Eligibility of this device for this manifest at epoch-second `now`.
eligibility evaluate_eligibility(const manifest& m, const std::string& device_id, int64_t now);

} // namespace thinr::update
