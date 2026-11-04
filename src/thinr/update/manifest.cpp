#include "manifest.hpp"

#include <cmath>

#include <openssl/evp.h>

namespace thinr::update {

namespace {

bool is_finite_number(const nlohmann::json& j, const char* key) {
    return j.contains(key) && j[key].is_number() && std::isfinite(j[key].get<double>());
}

} // namespace

manifest manifest::parse(std::string_view text) {
    manifest m;
    auto j = nlohmann::json::parse(text, nullptr, false);
    if (j.is_discarded() || !j.is_object()) return m;

    if (j.contains("version") && j["version"].is_string()) {
        m.version = j["version"].get<std::string>();
    }
    m.valid = !m.version.empty();

    if (j.contains("checksums") && j["checksums"].is_object()) {
        for (const auto& [arch, value] : j["checksums"].items()) {
            if (value.is_string()) m.checksums[arch] = value.get<std::string>();
        }
    }

    // Staging block: all three present and valid, or none.
    const bool has_pub = j.contains("published_at");
    const bool has_soak = j.contains("soak_h");
    const bool has_ramp = j.contains("ramp_h");
    const int present = (has_pub ? 1 : 0) + (has_soak ? 1 : 0) + (has_ramp ? 1 : 0);

    if (present == 3) {
        const bool pub_ok = j["published_at"].is_number() &&
                            j["published_at"].get<double>() > 0;
        const bool soak_ok = is_finite_number(j, "soak_h") &&
                             j["soak_h"].get<double>() >= 0 && j["soak_h"].get<double>() <= 8760;
        const bool ramp_ok = is_finite_number(j, "ramp_h") &&
                             j["ramp_h"].get<double>() >= 0 && j["ramp_h"].get<double>() <= 8760;
        if (pub_ok && soak_ok && ramp_ok) {
            m.has_staging = true;
            m.published_at = j["published_at"].get<int64_t>();
            m.soak_h = j["soak_h"].get<double>();
            m.ramp_h = j["ramp_h"].get<double>();
        } else {
            m.staging_invalid = true;
        }
    } else if (present != 0) {
        m.staging_invalid = true;  // partial block
    }

    if (j.contains("frozen_at") && j["frozen_at"].is_number() &&
        j["frozen_at"].get<double>() > 0) {
        m.has_frozen = true;
        m.frozen_at = j["frozen_at"].get<int64_t>();
    }

    return m;
}

std::optional<std::string> manifest::checksum_for(const std::string& arch) const {
    auto it = checksums.find(arch);
    if (it == checksums.end()) return std::nullopt;
    return it->second;
}

bool is_eligible(eligibility e) {
    return e == eligibility::eligible || e == eligibility::legacy_eligible;
}

double device_fraction(const std::string& device_id, const std::string& version) {
    const std::string input = device_id + ":" + version;
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int len = 0;
    if (EVP_Digest(input.data(), input.size(), digest, &len, EVP_sha256(), nullptr) != 1 || len < 8) {
        return 0.0;
    }
    uint64_t be = 0;
    for (int i = 0; i < 8; ++i) {
        be = (be << 8) | static_cast<uint64_t>(digest[i]);
    }
    long double f = static_cast<long double>(be) / 18446744073709551616.0L;  // 2^64
    if (f >= 1.0L) f = std::nextafter(1.0L, 0.0L);
    return static_cast<double>(f);
}

eligibility evaluate_eligibility(const manifest& m, const std::string& device_id, int64_t now) {
    if (!m.valid) return eligibility::invalid;
    if (m.staging_invalid) return eligibility::invalid;
    if (!m.has_staging) return eligibility::legacy_eligible;

    if (now < m.published_at) return eligibility::clock;

    const long double soak_end = static_cast<long double>(m.published_at) +
                                 static_cast<long double>(m.soak_h) * 3600.0L;
    if (static_cast<long double>(now) < soak_end) return eligibility::soak;

    if (m.ramp_h <= 0) return eligibility::eligible;

    long double ramp_ref = static_cast<long double>(now);
    if (m.has_frozen) {
        ramp_ref = std::min(ramp_ref, static_cast<long double>(m.frozen_at));
    }
    const long double ramp_elapsed_h = (ramp_ref - soak_end) / 3600.0L;
    const long double f = static_cast<long double>(device_fraction(device_id, m.version));

    if (ramp_elapsed_h / static_cast<long double>(m.ramp_h) > f) {
        return eligibility::eligible;
    }
    return eligibility::ramp;
}

} // namespace thinr::update
