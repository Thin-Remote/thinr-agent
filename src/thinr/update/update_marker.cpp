#include "update_marker.hpp"

namespace thinr::update {

std::string to_string(update_phase phase) {
    switch (phase) {
        case update_phase::pending:     return "pending";
        case update_phase::rolled_back: return "rolled_back";
        case update_phase::none:        return "none";
    }
    return "none";
}

std::optional<update_phase> phase_from_string(std::string_view text) {
    if (text == "pending")     return update_phase::pending;
    if (text == "rolled_back") return update_phase::rolled_back;
    if (text == "none")        return update_phase::none;
    return std::nullopt;
}

nlohmann::json update_marker::to_json() const {
    return nlohmann::json{
        {"phase", to_string(phase)},
        {"from", from},
        {"to", to},
        {"prev_binary", prev_binary},
        {"started_at", started_at},
        {"attempts", attempts},
        {"deadline_s", deadline_s},
        {"report_attempts", report_attempts},
    };
}

std::string update_marker::dump() const {
    return to_json().dump();
}

std::optional<update_marker> update_marker::from_json(const nlohmann::json& j) {
    if (!j.is_object()) return std::nullopt;

    auto phase_it = j.find("phase");
    if (phase_it == j.end() || !phase_it->is_string()) return std::nullopt;
    auto phase = phase_from_string(phase_it->get<std::string>());
    if (!phase) return std::nullopt;

    update_marker m;
    m.phase = *phase;
    m.from = j.value("from", std::string{});
    m.to = j.value("to", std::string{});
    m.prev_binary = j.value("prev_binary", std::string{});
    m.started_at = j.value("started_at", static_cast<int64_t>(0));
    m.attempts = j.value("attempts", 0);
    m.deadline_s = j.value("deadline_s", 90);
    m.report_attempts = j.value("report_attempts", 0);
    return m;
}

std::optional<update_marker> update_marker::parse(std::string_view text) {
    auto j = nlohmann::json::parse(text, nullptr, false);
    if (j.is_discarded()) return std::nullopt;
    return from_json(j);
}

nlohmann::json reverted_info::to_json() const {
    return nlohmann::json{
        {"version", version},
        {"restored", restored},
        {"reason", reason},
        {"at", at},
    };
}

std::string reverted_info::dump() const {
    return to_json().dump();
}

std::optional<reverted_info> reverted_info::from_json(const nlohmann::json& j) {
    if (!j.is_object()) return std::nullopt;

    auto ver_it = j.find("version");
    if (ver_it == j.end() || !ver_it->is_string() || ver_it->get<std::string>().empty()) {
        return std::nullopt;
    }

    reverted_info r;
    r.version = ver_it->get<std::string>();
    r.restored = j.value("restored", std::string{});
    r.reason = j.value("reason", std::string{});
    r.at = j.value("at", static_cast<int64_t>(0));
    return r;
}

std::optional<reverted_info> reverted_info::parse(std::string_view text) {
    auto j = nlohmann::json::parse(text, nullptr, false);
    if (j.is_discarded()) return std::nullopt;
    return from_json(j);
}

} // namespace thinr::update
