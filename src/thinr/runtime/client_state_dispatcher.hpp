#pragma once

#include <thinger/iotmp/client.hpp>

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace thinr::runtime {

// The IOTMP client exposes a single state-callback slot. Several parts of the
// agent need to watch the connection state (the update watchdog, the poller, ...),
// so this dispatcher is the one and only caller of set_state_callback and fans
// each change out to any number of observers. Observers run on the thread that
// emitted the change (the io thread, or the thread calling stop()); they must
// not block. An observer that throws does not affect the others.
class client_state_dispatcher {
public:
    using state = thinger::iotmp::client_state;
    using observer = std::function<void(state, const std::string&)>;

    // RAII handle that removes its observer on destruction. Non-copyable,
    // movable. Keep it alive for as long as the observer must receive events.
    class subscription {
    public:
        subscription() = default;
        subscription(client_state_dispatcher* owner, uint64_t id) : owner_(owner), id_(id) {}

        subscription(const subscription&) = delete;
        subscription& operator=(const subscription&) = delete;

        subscription(subscription&& other) noexcept : owner_(other.owner_), id_(other.id_) {
            other.owner_ = nullptr;
        }
        subscription& operator=(subscription&& other) noexcept {
            if (this != &other) {
                reset();
                owner_ = other.owner_;
                id_ = other.id_;
                other.owner_ = nullptr;
            }
            return *this;
        }

        ~subscription() { reset(); }

        void reset() {
            if (owner_) {
                owner_->unsubscribe(id_);
                owner_ = nullptr;
            }
        }

    private:
        client_state_dispatcher* owner_ = nullptr;
        uint64_t id_ = 0;
    };

    explicit client_state_dispatcher(thinger::iotmp::client& client) : client_(client) {
        client_.set_state_callback([this](state s, const std::string& reason) {
            dispatch(s, reason);
        });
    }

    ~client_state_dispatcher() {
        // Detach before we die so a late callback never lands on a destroyed
        // dispatcher. Safe once the client's io threads are stopped.
        client_.set_state_callback({});
    }

    client_state_dispatcher(const client_state_dispatcher&) = delete;
    client_state_dispatcher& operator=(const client_state_dispatcher&) = delete;

    [[nodiscard]] subscription subscribe(observer obs) {
        std::lock_guard<std::mutex> lock(mutex_);
        uint64_t id = next_id_++;
        observers_.push_back({id, std::move(obs)});
        return subscription(this, id);
    }

    // Last state seen. Also compensates for the client having no state getter.
    state last_state() const { return last_.load(std::memory_order_relaxed); }

private:
    struct entry {
        uint64_t id;
        observer obs;
    };

    void unsubscribe(uint64_t id) {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto it = observers_.begin(); it != observers_.end(); ++it) {
            if (it->id == id) {
                observers_.erase(it);
                return;
            }
        }
    }

    void dispatch(state s, const std::string& reason) {
        last_.store(s, std::memory_order_relaxed);
        std::vector<observer> snapshot;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            snapshot.reserve(observers_.size());
            for (auto& e : observers_) snapshot.push_back(e.obs);
        }
        for (auto& obs : snapshot) {
            try {
                obs(s, reason);
            } catch (...) {
                // An observer's failure must not break the others.
            }
        }
    }

    thinger::iotmp::client& client_;
    mutable std::mutex mutex_;
    std::vector<entry> observers_;
    uint64_t next_id_ = 1;
    std::atomic<state> last_{state::DISCONNECTED};
};

} // namespace thinr::runtime
