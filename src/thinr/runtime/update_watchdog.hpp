#pragma once

#include <thinger/iotmp/client.hpp>

#include <condition_variable>
#include <cstdint>
#include <exception>
#include <mutex>
#include <string>
#include <thread>

#include "client_state_dispatcher.hpp"
#include "../update/update_marker.hpp"

namespace thinr::runtime {

// Thrown by check_boot (through the agent constructor) when a crash-looping
// update has just been reverted. The process must exit so the init system
// restarts into the restored previous binary.
class reverted_restart_required : public std::exception {
public:
    const char* what() const noexcept override {
        return "update reverted after crash loop; restart required";
    }
};

// Per-device rollback watchdog. Split in two:
//  - check_boot(): a static, pre-client step run very early at startup. It reads
//    the marker, bounds crash loops and reverts when needed.
//  - the object itself: the runtime half. Once a swap is on trial it watches the
//    client state and a deadline, then confirms the new binary or reverts.
class update_watchdog {
public:
    enum class boot_outcome {
        proceed,        // start the agent normally
        reverted_exit,  // a crash loop was reverted; caller must exit to restart
    };

    // Early-boot check. `state_dir` holds update.state / update.reverted;
    // `running_version` is AGENT_VERSION of this process.
    static boot_outcome check_boot(const std::string& state_dir,
                                   const std::string& running_version);

    update_watchdog(thinger::iotmp::client& client,
                    client_state_dispatcher& dispatcher,
                    std::string state_dir,
                    std::string running_version);
    ~update_watchdog();

    update_watchdog(const update_watchdog&) = delete;
    update_watchdog& operator=(const update_watchdog&) = delete;

    // Cancel the deadline and detach. Call before the client stops.
    void stop();

private:
    void on_state(thinger::iotmp::client_state state, const std::string& reason);
    void deadline_loop();
    void confirm();
    void rollback(const std::string& reason);
    void report(const char* event, const std::string& from, const std::string& to,
                const std::string& reason);

    std::string marker_path() const;

    thinger::iotmp::client& client_;
    std::string state_dir_;
    std::string running_version_;

    thinr::update::update_marker marker_;  // the pending/rolled_back marker read at start
    bool armed_ = false;           // a swap is on trial, awaiting STREAMS_READY/deadline
    bool report_pending_ = false;  // a prior rollback must be reported after STREAMS_READY

    std::mutex mutex_;
    bool resolved_ = false;        // confirmed or rolled back already (once only)

    std::thread deadline_thread_;
    std::mutex deadline_mutex_;
    std::condition_variable deadline_cv_;
    bool deadline_stop_ = false;
    int deadline_s_ = 90;

    client_state_dispatcher::subscription sub_;
};

} // namespace thinr::runtime
