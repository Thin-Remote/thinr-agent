#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

#include "thinr/utils/atomic_file.hpp"
#include "thinr/update/update_marker.hpp"
#include "thinr/update/watchdog_core.hpp"

using namespace thinr;

namespace {

std::string read_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

int file_mode(const std::string& path) {
    struct stat st{};
    if (::stat(path.c_str(), &st) != 0) return -1;
    return st.st_mode & 0777;
}

struct temp_dir {
    std::filesystem::path path;
    temp_dir() {
        path = std::filesystem::temp_directory_path() /
               ("thinr-wd-test-" + std::to_string(::getpid()) + "-" +
                std::to_string(reinterpret_cast<uintptr_t>(this)));
        std::filesystem::create_directories(path);
    }
    ~temp_dir() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
    std::string file(const char* name) const { return (path / name).string(); }
};

} // namespace

TEST_CASE("write_atomic writes content and leaves no temp residue", "[atomic_file]") {
    temp_dir dir;
    const std::string target = dir.file("state.json");

    REQUIRE(utils::write_atomic(target, "hello", 0600));
    REQUIRE(read_file(target) == "hello");
    REQUIRE(file_mode(target) == 0600);

    // Overwrites in place, old content fully replaced.
    REQUIRE(utils::write_atomic(target, "world!!", 0600));
    REQUIRE(read_file(target) == "world!!");

    // No .tmp.* files left behind in the directory.
    int leftovers = 0;
    for (const auto& e : std::filesystem::directory_iterator(dir.path)) {
        if (e.path().filename().string().find(".tmp.") != std::string::npos) ++leftovers;
    }
    REQUIRE(leftovers == 0);
}

TEST_CASE("update_marker round-trips through JSON", "[marker]") {
    update::update_marker m;
    m.phase = update::update_phase::pending;
    m.from = "v1.7.5";
    m.to = "v1.8.0";
    m.prev_binary = "/usr/local/bin/thinr-agent.prev";
    m.started_at = 1762000000;
    m.attempts = 2;
    m.deadline_s = 90;
    m.report_attempts = 0;

    auto parsed = update::update_marker::parse(m.dump());
    REQUIRE(parsed.has_value());
    REQUIRE(parsed->phase == update::update_phase::pending);
    REQUIRE(parsed->from == "v1.7.5");
    REQUIRE(parsed->to == "v1.8.0");
    REQUIRE(parsed->prev_binary == "/usr/local/bin/thinr-agent.prev");
    REQUIRE(parsed->started_at == 1762000000);
    REQUIRE(parsed->attempts == 2);
    REQUIRE(parsed->deadline_s == 90);
}

TEST_CASE("update_marker rejects corrupt or invalid input", "[marker]") {
    REQUIRE_FALSE(update::update_marker::parse("not json").has_value());
    REQUIRE_FALSE(update::update_marker::parse("{}").has_value());               // no phase
    REQUIRE_FALSE(update::update_marker::parse(R"({"phase":"bogus"})").has_value());
    REQUIRE(update::update_marker::parse(R"({"phase":"pending"})").has_value()); // defaults fill in
}

TEST_CASE("reverted_info requires a version", "[marker]") {
    update::reverted_info r;
    r.version = "v1.8.0";
    r.restored = "v1.7.5";
    r.reason = "deadline";
    r.at = 1762000000;

    auto parsed = update::reverted_info::parse(r.dump());
    REQUIRE(parsed.has_value());
    REQUIRE(parsed->version == "v1.8.0");
    REQUIRE(parsed->restored == "v1.7.5");
    REQUIRE(parsed->reason == "deadline");

    REQUIRE_FALSE(update::reverted_info::parse(R"({"restored":"v1.7.5"})").has_value());
    REQUIRE_FALSE(update::reverted_info::parse("garbage").has_value());
}

TEST_CASE("decide_boot: no marker is a clean start", "[watchdog]") {
    REQUIRE(update::decide_boot(std::nullopt, "v1.8.0").action == update::boot_action::clean_start);

    update::update_marker none;
    none.phase = update::update_phase::none;
    REQUIRE(update::decide_boot(none, "v1.8.0").action == update::boot_action::clean_start);
}

TEST_CASE("decide_boot: rolled_back owes a report", "[watchdog]") {
    update::update_marker m;
    m.phase = update::update_phase::rolled_back;
    m.from = "v1.7.5";
    m.to = "v1.8.0";
    REQUIRE(update::decide_boot(m, "v1.7.5").action == update::boot_action::report_rolled_back);
}

TEST_CASE("decide_boot: pending new binary arms within budget", "[watchdog]") {
    update::update_marker m;
    m.phase = update::update_phase::pending;
    m.from = "v1.7.5";
    m.to = "v1.8.0";
    m.attempts = 0;

    auto d = update::decide_boot(m, "v1.8.0");
    REQUIRE(d.action == update::boot_action::arm);
    REQUIRE(d.marker.attempts == 1);  // this boot counted

    m.attempts = 2;
    auto d3 = update::decide_boot(m, "v1.8.0");
    REQUIRE(d3.action == update::boot_action::arm);
    REQUIRE(d3.marker.attempts == 3);
}

TEST_CASE("decide_boot: crash loop reverts on the fourth boot", "[watchdog]") {
    update::update_marker m;
    m.phase = update::update_phase::pending;
    m.from = "v1.7.5";
    m.to = "v1.8.0";
    m.attempts = update::WATCHDOG_MAX_ATTEMPTS;  // already used the budget

    auto d = update::decide_boot(m, "v1.8.0");
    REQUIRE(d.action == update::boot_action::rollback);
}

TEST_CASE("decide_boot: never reverts a binary that is already the good one", "[watchdog]") {
    update::update_marker m;
    m.phase = update::update_phase::pending;
    m.from = "v1.7.5";
    m.to = "v1.8.0";
    m.attempts = 0;

    // Swap never happened: still running `from`.
    REQUIRE(update::decide_boot(m, "v1.7.5").action == update::boot_action::clear_no_revert);
    // Running something unrelated to the marker.
    REQUIRE(update::decide_boot(m, "v1.6.0").action == update::boot_action::clear_no_revert);
}

TEST_CASE("decide_runtime maps events to actions", "[watchdog]") {
    REQUIRE(update::decide_runtime(update::runtime_event::streams_ready) == update::runtime_action::confirm);
    REQUIRE(update::decide_runtime(update::runtime_event::auth_failed) == update::runtime_action::rollback);
    REQUIRE(update::decide_runtime(update::runtime_event::deadline) == update::runtime_action::rollback);
    REQUIRE(update::decide_runtime(update::runtime_event::auth_failed_io) == update::runtime_action::ignore);
    REQUIRE(update::decide_runtime(update::runtime_event::state_other) == update::runtime_action::ignore);
}

TEST_CASE("skip_reverted_version: a different version is never blocked", "[watchdog]") {
    update::reverted_info rev;
    rev.version = "v1.8.0";
    rev.reason = "crash_loop";
    rev.at = 1000;
    // Candidate is a different version: always retriable regardless of reason.
    REQUIRE_FALSE(update::skip_reverted_version(rev, "v1.8.1", /*now=*/2000, /*cooldown=*/86400));
}

TEST_CASE("skip_reverted_version: a bad binary is skipped for good", "[watchdog]") {
    const int64_t cooldown = 86400;
    const int64_t long_after = 1000 + 10 * cooldown;

    update::reverted_info crash;
    crash.version = "v1.8.0";
    crash.reason = "crash_loop";
    crash.at = 1000;
    REQUIRE(update::skip_reverted_version(crash, "v1.8.0", long_after, cooldown));

    update::reverted_info auth;
    auth.version = "v1.8.0";
    auth.reason = "auth_failed";
    auth.at = 1000;
    REQUIRE(update::skip_reverted_version(auth, "v1.8.0", long_after, cooldown));
}

TEST_CASE("skip_reverted_version: a deadline miss is retried after the cooldown", "[watchdog]") {
    const int64_t cooldown = 86400;
    update::reverted_info rev;
    rev.version = "v1.8.0";
    rev.reason = "deadline";
    rev.at = 1000;

    // Still within the cooldown: skip.
    REQUIRE(update::skip_reverted_version(rev, "v1.8.0", 1000 + cooldown - 1, cooldown));
    // Cooldown elapsed: retry.
    REQUIRE_FALSE(update::skip_reverted_version(rev, "v1.8.0", 1000 + cooldown + 1, cooldown));
}
