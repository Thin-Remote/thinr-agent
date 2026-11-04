#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <string>

#include "thinr/update/update_settings.hpp"
#include "thinr/update/manifest.hpp"
#include "thinr/update/poll_schedule.hpp"

using namespace thinr::update;
using Catch::Approx;

// ---- update_settings -------------------------------------------------------

TEST_CASE("update_settings defaults when absent or malformed", "[settings]") {
    auto s = update_settings::from_json(nlohmann::json::object());
    REQUIRE(s.policy == update_policy::automatic);
    REQUIRE(s.source == policy_source::defaulted);
    REQUIRE(s.channel == "latest");
    REQUIRE(s.check_interval_h == CHECK_INTERVAL_DEFAULT_H);
    REQUIRE(s.rollback_deadline_s == ROLLBACK_DEADLINE_DEFAULT_S);

    auto bad = update_settings::from_json(nlohmann::json::parse(R"({"policy":"nope","channel":"a/b/..","check_interval_h":0,"rollback_deadline_s":5})"));
    REQUIRE(bad.policy == update_policy::automatic);   // invalid -> auto
    REQUIRE(bad.channel == "latest");                  // invalid channel -> default
    REQUIRE(bad.check_interval_h == CHECK_INTERVAL_MIN_H);
    REQUIRE(bad.rollback_deadline_s == ROLLBACK_DEADLINE_MIN_S);
}

TEST_CASE("update_settings clamps numeric fields", "[settings]") {
    auto hi = update_settings::from_json(nlohmann::json::parse(R"({"check_interval_h":100000,"rollback_deadline_s":100000})"));
    REQUIRE(hi.check_interval_h == CHECK_INTERVAL_MAX_H);
    REQUIRE(hi.rollback_deadline_s == ROLLBACK_DEADLINE_MAX_S);
}

TEST_CASE("update_settings round-trips policy and source", "[settings]") {
    update_settings s;
    s.policy = update_policy::managed;
    s.source = policy_source::local_explicit;
    s.channel = "main";
    auto parsed = update_settings::from_json(s.to_json());
    REQUIRE(parsed.policy == update_policy::managed);
    REQUIRE(parsed.source == policy_source::local_explicit);
    REQUIRE(parsed.channel == "main");
}

TEST_CASE("effective_policy precedence", "[settings]") {
    update_settings def;  // auto / default
    REQUIRE(effective_policy(def, std::nullopt) == update_policy::automatic);
    REQUIRE(effective_policy(def, update_policy::managed) == update_policy::managed);
    REQUIRE(effective_policy(def, update_policy::automatic) == update_policy::automatic);

    update_settings opt_out;  // explicit managed opt-out
    opt_out.policy = update_policy::managed;
    opt_out.source = policy_source::local_explicit;
    REQUIRE(effective_policy(opt_out, update_policy::automatic) == update_policy::managed);  // cloud cannot force auto
    REQUIRE(effective_policy(opt_out, update_policy::managed) == update_policy::managed);
    REQUIRE(effective_policy(opt_out, std::nullopt) == update_policy::managed);

    update_settings managed_default;  // managed but only by default (not explicit)
    managed_default.policy = update_policy::managed;
    managed_default.source = policy_source::defaulted;
    REQUIRE(effective_policy(managed_default, update_policy::automatic) == update_policy::automatic);  // cloud can override a default

    update_settings auto_explicit;
    auto_explicit.policy = update_policy::automatic;
    auto_explicit.source = policy_source::local_explicit;
    REQUIRE(effective_policy(auto_explicit, update_policy::managed) == update_policy::managed);  // cloud can always be more conservative
}

// ---- manifest parse --------------------------------------------------------

TEST_CASE("manifest parses the full staged form", "[manifest]") {
    auto m = manifest::parse(R"({"version":"v1.8.0","published_at":1762000000,"soak_h":72,"ramp_h":96,"checksums":{"x86_64-linux-musl":"sha256:abc"}})");
    REQUIRE(m.valid);
    REQUIRE(m.version == "v1.8.0");
    REQUIRE(m.has_staging);
    REQUIRE_FALSE(m.staging_invalid);
    REQUIRE(m.published_at == 1762000000);
    REQUIRE(m.checksum_for("x86_64-linux-musl").value() == "sha256:abc");
    REQUIRE_FALSE(m.checksum_for("other").has_value());
}

TEST_CASE("manifest treats a legacy form as staging-less", "[manifest]") {
    auto m = manifest::parse(R"({"version":"v1.7.5","checksums":{"a":"sha256:x"}})");
    REQUIRE(m.valid);
    REQUIRE_FALSE(m.has_staging);
    REQUIRE_FALSE(m.staging_invalid);
}

TEST_CASE("manifest flags a partial or invalid staging block", "[manifest]") {
    auto partial = manifest::parse(R"({"version":"v1.8.0","published_at":1762000000})");
    REQUIRE(partial.staging_invalid);
    REQUIRE_FALSE(partial.has_staging);

    auto neg = manifest::parse(R"({"version":"v1.8.0","published_at":-5,"soak_h":1,"ramp_h":1})");
    REQUIRE(neg.staging_invalid);
}

TEST_CASE("device_fraction matches the published test vectors", "[manifest]") {
    REQUIRE(device_fraction("device-aaa", "v1.8.0") == Approx(0.6856489929).epsilon(1e-9));
    REQUIRE(device_fraction("device-bbb", "v1.8.0") == Approx(0.1306323212).epsilon(1e-9));
    REQUIRE(device_fraction("device-aaa", "v1.8.1") == Approx(0.1111382881).epsilon(1e-9));
    // Deterministic.
    REQUIRE(device_fraction("device-aaa", "v1.8.0") == device_fraction("device-aaa", "v1.8.0"));
    // Below 1.
    REQUIRE(device_fraction("device-aaa", "v1.8.0") < 1.0);
}

// ---- eligibility -----------------------------------------------------------

static manifest staged(int64_t published_at, double soak_h, double ramp_h,
                       const std::string& version = "v1.8.0") {
    manifest m;
    m.valid = true;
    m.version = version;
    m.has_staging = true;
    m.published_at = published_at;
    m.soak_h = soak_h;
    m.ramp_h = ramp_h;
    return m;
}

TEST_CASE("eligibility: soak, clock and ramp_h=0", "[eligibility]") {
    auto m = staged(1000, 1.0, 10.0);  // soak ends at 1000 + 3600

    REQUIRE(evaluate_eligibility(m, "d", 500) == eligibility::clock);        // clock before published_at
    REQUIRE(evaluate_eligibility(m, "d", 1000) == eligibility::soak);        // exactly published, in soak
    REQUIRE(evaluate_eligibility(m, "d", 1000 + 3599) == eligibility::soak); // still soaking
    REQUIRE(evaluate_eligibility(m, "d", 1000 + 3600) == eligibility::ramp); // ramp just started: 0 > f is false

    auto no_ramp = staged(1000, 1.0, 0.0);
    REQUIRE(evaluate_eligibility(no_ramp, "d", 1000 + 3600) == eligibility::eligible);  // ramp_h=0 -> all eligible after soak
}

TEST_CASE("eligibility: legacy and invalid", "[eligibility]") {
    manifest legacy;
    legacy.valid = true;
    legacy.version = "v1.7.5";
    REQUIRE(evaluate_eligibility(legacy, "d", 9999999999) == eligibility::legacy_eligible);

    manifest invalid;
    invalid.valid = true;
    invalid.version = "v1.8.0";
    invalid.staging_invalid = true;
    REQUIRE(evaluate_eligibility(invalid, "d", 9999999999) == eligibility::invalid);
}

TEST_CASE("eligibility: ramp fraction follows the rollout and nests", "[eligibility]") {
    // published_at=0, no soak, ramp spans 100h. At ramp_elapsed=25h the eligible
    // set is ~25% of devices; at 50h ~50%; and the 25% set is a subset of 50%.
    auto m = staged(0, 0.0, 100.0);
    const int64_t at25 = 25 * 3600;
    const int64_t at50 = 50 * 3600;

    int elig25 = 0, elig50 = 0, nested_ok = 0, in25 = 0;
    const int N = 2000;
    for (int i = 0; i < N; ++i) {
        std::string dev = "device-" + std::to_string(i);
        bool e25 = is_eligible(evaluate_eligibility(m, dev, at25));
        bool e50 = is_eligible(evaluate_eligibility(m, dev, at50));
        if (e25) { ++elig25; ++in25; if (e50) ++nested_ok; }
        if (e50) ++elig50;
    }
    double p25 = 100.0 * elig25 / N;
    double p50 = 100.0 * elig50 / N;
    REQUIRE(p25 == Approx(25.0).margin(3.0));
    REQUIRE(p50 == Approx(50.0).margin(3.0));
    REQUIRE(nested_ok == in25);  // everyone eligible at 25h is still eligible at 50h
}

TEST_CASE("eligibility: frozen_at caps the ramp exposure", "[eligibility]") {
    auto m = staged(0, 0.0, 100.0);
    m.has_frozen = true;
    m.frozen_at = 10 * 3600;  // freeze exposure at the 10% ramp level

    int elig_now = 0, elig_late = 0;
    const int N = 2000;
    for (int i = 0; i < N; ++i) {
        std::string dev = "device-" + std::to_string(i);
        if (is_eligible(evaluate_eligibility(m, dev, 50 * 3600))) ++elig_now;   // real time at 50h
        if (is_eligible(evaluate_eligibility(m, dev, 90 * 3600))) ++elig_late;  // even later
    }
    // Both held at the frozen 10% level regardless of how much later we look.
    REQUIRE(100.0 * elig_now / N == Approx(10.0).margin(3.0));
    REQUIRE(elig_now == elig_late);
}

// ---- poll schedule ---------------------------------------------------------

TEST_CASE("poll schedule bounds", "[schedule]") {
    REQUIRE(first_poll_delay_s(0.0) == Approx(0.0));
    REQUIRE(first_poll_delay_s(1.0) == Approx(1800.0));
    REQUIRE(normal_poll_delay_s(6, 0.0) == Approx(6 * 3600 * 0.9));
    REQUIRE(normal_poll_delay_s(6, 1.0) == Approx(6 * 3600 * 1.1));
    REQUIRE(DEFERRED_RETRY_DELAY_S == 600);
}
