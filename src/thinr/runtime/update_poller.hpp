#pragma once

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

#include "client_state_dispatcher.hpp"
#include "../update/update_settings.hpp"

namespace thinr::runtime {

// Automatic update poll loop. When the local policy (combined with any remote
// override) is `auto`, it periodically fetches the channel manifest and, if a
// different version is published and this device is eligible under the staged
// rollout, asks the updater to apply it through the watchdog path.
//
// It holds no reference to the IOTMP client: connection state comes from the
// state dispatcher, and the manifest fetch / apply use the updater's own HTTP
// path. The periodic work runs on a dedicated, cancellable thread (the client's
// worker io_context is not publicly reachable).
class update_poller {
public:
    update_poller(client_state_dispatcher& dispatcher,
                  std::string device_id,
                  std::string config_path,
                  std::string state_dir,
                  std::string running_version);
    ~update_poller();

    update_poller(const update_poller&) = delete;
    update_poller& operator=(const update_poller&) = delete;

    // Remote policy override from the `config` property (nullopt clears it).
    void set_remote_policy(std::optional<thinr::update::update_policy> policy);

    void stop();

private:
    enum class cycle_result { normal, deferred, restarting };

    void on_state(thinger::iotmp::client_state state, const std::string& reason);
    void poll_loop();
    cycle_result run_cycle();
    std::optional<thinr::update::update_policy> remote_override() const;

    std::string device_id_;
    std::string config_path_;
    std::string state_dir_;
    std::string running_version_;

    std::atomic<bool> connected_{false};
    std::atomic<int> remote_policy_{-1};  // -1 none, 0 auto, 1 managed
    int last_interval_h_ = thinr::update::CHECK_INTERVAL_DEFAULT_H;  // poll thread only

    std::mutex mutex_;
    std::condition_variable cv_;
    bool stopping_ = false;
    std::thread thread_;

    client_state_dispatcher::subscription sub_;
};

} // namespace thinr::runtime
