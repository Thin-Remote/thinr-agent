#include "update_settings.hpp"

#include <algorithm>

namespace thinr::update {

std::string to_string(update_policy p) {
    return p == update_policy::managed ? "managed" : "auto";
}

std::string to_string(policy_source s) {
    switch (s) {
        case policy_source::local_explicit: return "local_explicit";
        case policy_source::remote:         return "remote";
        case policy_source::defaulted:      return "default";
    }
    return "default";
}

std::optional<update_policy> policy_from_string(std::string_view text) {
    if (text == "auto")    return update_policy::automatic;
    if (text == "managed") return update_policy::managed;
    return std::nullopt;
}

policy_source source_from_string(std::string_view text) {
    if (text == "local_explicit") return policy_source::local_explicit;
    if (text == "remote")         return policy_source::remote;
    return policy_source::defaulted;
}

bool valid_channel(const std::string& channel) {
    if (channel.empty()) return false;
    if (channel.find("..") != std::string::npos) return false;
    for (char c : channel) {
        bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                  (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-';
        if (!ok) return false;
    }
    return true;
}

static int clamp_int(const nlohmann::json& j, const char* key, int def, int lo, int hi) {
    if (!j.contains(key) || !j[key].is_number_integer()) return def;
    int v = j[key].get<int>();
    return std::clamp(v, lo, hi);
}

nlohmann::json update_settings::to_json() const {
    return nlohmann::json{
        {"policy", to_string(policy)},
        {"policy_source", to_string(source)},
        {"channel", channel},
        {"check_interval_h", check_interval_h},
        {"rollback_deadline_s", rollback_deadline_s},
    };
}

update_settings update_settings::from_json(const nlohmann::json& j) {
    update_settings s;
    if (!j.is_object()) return s;

    if (j.contains("policy") && j["policy"].is_string()) {
        if (auto p = policy_from_string(j["policy"].get<std::string>())) s.policy = *p;
    }
    if (j.contains("policy_source") && j["policy_source"].is_string()) {
        s.source = source_from_string(j["policy_source"].get<std::string>());
    }
    if (j.contains("channel") && j["channel"].is_string()) {
        auto ch = j["channel"].get<std::string>();
        if (valid_channel(ch)) s.channel = ch;
    }
    s.check_interval_h = clamp_int(j, "check_interval_h", CHECK_INTERVAL_DEFAULT_H,
                                   CHECK_INTERVAL_MIN_H, CHECK_INTERVAL_MAX_H);
    s.rollback_deadline_s = clamp_int(j, "rollback_deadline_s", ROLLBACK_DEADLINE_DEFAULT_S,
                                      ROLLBACK_DEADLINE_MIN_S, ROLLBACK_DEADLINE_MAX_S);
    return s;
}

update_policy effective_policy(const update_settings& local,
                               std::optional<update_policy> remote) {
    if (!remote) return local.policy;
    if (*remote == update_policy::managed) return update_policy::managed;
    // remote == automatic: honour an explicit local opt-out.
    if (local.policy == update_policy::managed && local.source == policy_source::local_explicit) {
        return update_policy::managed;
    }
    return update_policy::automatic;
}

} // namespace thinr::update
