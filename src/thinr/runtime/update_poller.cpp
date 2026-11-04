#include "update_poller.hpp"

#include "../extensions/updater.hpp"
#include "../update/manifest.hpp"
#include "../update/poll_schedule.hpp"
#include "../update/watchdog_core.hpp"
#include "../config/config_manager.hpp"

#include <spdlog/spdlog.h>

#include <chrono>
#include <ctime>
#include <random>

namespace thinr::runtime {

namespace up = thinr::update;

update_poller::update_poller(client_state_dispatcher& dispatcher,
                             std::string device_id,
                             std::string config_path,
                             std::string state_dir,
                             std::string running_version)
    : device_id_(std::move(device_id)),
      config_path_(std::move(config_path)),
      state_dir_(std::move(state_dir)),
      running_version_(std::move(running_version)) {

    sub_ = dispatcher.subscribe([this](thinger::iotmp::client_state s, const std::string& r) {
        on_state(s, r);
    });
    thread_ = std::thread([this] { poll_loop(); });
}

update_poller::~update_poller() {
    stop();
}

void update_poller::stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
    }
    cv_.notify_all();
    if (thread_.joinable()) thread_.join();
    sub_.reset();
}

void update_poller::set_remote_policy(std::optional<up::update_policy> policy) {
    int v = -1;
    if (policy) v = (*policy == up::update_policy::managed) ? 1 : 0;
    remote_policy_.store(v, std::memory_order_relaxed);
}

std::optional<up::update_policy> update_poller::remote_override() const {
    switch (remote_policy_.load(std::memory_order_relaxed)) {
        case 0: return up::update_policy::automatic;
        case 1: return up::update_policy::managed;
        default: return std::nullopt;
    }
}

void update_poller::on_state(thinger::iotmp::client_state state, const std::string&) {
    connected_.store(state == thinger::iotmp::client_state::STREAMS_READY,
                     std::memory_order_relaxed);
}

void update_poller::poll_loop() {
    std::mt19937_64 rng(static_cast<uint64_t>(std::random_device{}()) ^
                        static_cast<uint64_t>(::time(nullptr)));
    auto uniform = [&rng] { return std::uniform_real_distribution<double>(0.0, 1.0)(rng); };

    auto next = std::chrono::steady_clock::now() +
                std::chrono::milliseconds(static_cast<int64_t>(up::first_poll_delay_s(uniform()) * 1000));

    while (true) {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait_until(lock, next, [this] { return stopping_; });
        if (stopping_) return;
        lock.unlock();

        double delay_s;
        switch (run_cycle()) {
            case cycle_result::restarting:
                return;  // a SIGTERM is on its way; stop polling
            case cycle_result::deferred:
                delay_s = up::DEFERRED_RETRY_DELAY_S;
                break;
            case cycle_result::normal:
            default:
                delay_s = up::normal_poll_delay_s(last_interval_h_, uniform());
                break;
        }
        next = std::chrono::steady_clock::now() +
               std::chrono::milliseconds(static_cast<int64_t>(delay_s * 1000));
    }
}

update_poller::cycle_result update_poller::run_cycle() {
    namespace ext = thinr::extensions;

    config::config_manager cm = config_path_.empty()
        ? config::config_manager()
        : config::config_manager(config_path_);

    auto local = up::update_settings::from_json(cm.read_update_section());
    last_interval_h_ = local.check_interval_h;

    const auto policy = up::effective_policy(local, remote_override());
    if (policy == up::update_policy::managed) {
        spdlog::debug("Auto-update skipped: policy is managed");
        return cycle_result::normal;
    }

    if (!connected_.load(std::memory_order_relaxed)) {
        spdlog::debug("Auto-update skipped: not connected");
        return cycle_result::normal;
    }

    auto man = ext::updater::fetch_manifest(local.channel);
    if (!man || !man->valid) {
        spdlog::warn("Auto-update: no valid manifest on channel {}", local.channel);
        return cycle_result::normal;
    }
    if (man->version == running_version_) {
        spdlog::debug("Auto-update: up to date ({})", running_version_);
        return cycle_result::normal;
    }

    if (auto rev = ext::updater::last_reverted(state_dir_);
        rev && up::skip_reverted_version(*rev, man->version,
                                         static_cast<int64_t>(::time(nullptr)),
                                         up::REVERTED_RETRY_COOLDOWN_S)) {
        spdlog::info("Auto-update: skipping {}, recently reverted ({})",
                     man->version, rev->reason);
        return cycle_result::normal;
    }

    const auto elig = up::evaluate_eligibility(*man, device_id_, static_cast<int64_t>(::time(nullptr)));
    if (!up::is_eligible(elig)) {
        spdlog::debug("Auto-update: {} not yet eligible (stage={})", man->version, static_cast<int>(elig));
        return cycle_result::normal;
    }

    ext::updater::apply_request req;
    req.version = man->version;
    req.channel = local.channel;
    req.url = ext::updater::binary_url(local.channel);
    req.checksum = man->checksum_for(AGENT_BINARY_SUFFIX).value_or("");
    req.state_dir = state_dir_;
    req.origin = ext::updater::update_origin::auto_poll;
    req.rollback_deadline_s = local.rollback_deadline_s;

    spdlog::info("Auto-update: applying {} -> {}", running_version_, man->version);
    auto r = ext::updater::apply_with_watchdog(req);
    switch (r.st) {
        case ext::updater::apply_result::status::updated:
            ext::updater::restart_into_new_binary();
            return cycle_result::restarting;
        case ext::updater::apply_result::status::deferred:
            spdlog::info("Auto-update deferred: {}", r.message);
            return cycle_result::deferred;
        case ext::updater::apply_result::status::error:
        default:
            spdlog::warn("Auto-update failed: {}", r.message);
            return cycle_result::normal;
    }
}

} // namespace thinr::runtime
