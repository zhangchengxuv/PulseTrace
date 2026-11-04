#include "pulsetrace/core_service.hpp"
#include "pulsetrace/protocol.hpp"
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
namespace pulsetrace {
namespace {
void validate_config(const Json &c) {
    const auto theme = c.at("theme").get<std::string>();
    if (theme != "light" && theme != "dark" && theme != "system")
        throw std::invalid_argument("Invalid theme");
    const auto &days = c.at("retention_days");
    const auto &seconds = c.at("flush_seconds");
    if (!days.is_number_integer() || days < 1 || days > 365 || !seconds.is_number_integer() || seconds < 5 ||
        seconds > 300)
        throw std::invalid_argument("Settings outside supported range");
    const auto interface = c.at("network_interface").get<std::string>();
    if (interface.size() > 32 ||
        !std::all_of(interface.begin(), interface.end(), [](char c) { return c >= '0' && c <= '9'; }))
        throw std::invalid_argument("Invalid interface identifier");
}
Json row(std::int64_t minute, const char *module, std::string metric, std::uint64_t value) {
    return {{"minute", minute}, {"module", module}, {"metric", std::move(metric)}, {"value", value}};
}
} // namespace
CoreService::CoreService(std::filesystem::path data) : data_(std::move(data)) {
    config_ = {{"theme", "system"},       {"retention_days", 30},  {"flush_seconds", 30},
               {"network_interface", ""}, {"close_to_tray", true}, {"capture_on_launch", false}};
    std::ifstream in(data_ / "config.json");
    if (in) {
        Json c;
        in >> c;
        for (auto it = config_.begin(); it != config_.end(); ++it)
            if (c.contains(it.key()) && (c[it.key()].type() == it.value().type() ||
                                         (c[it.key()].is_number_integer() && it.value().is_number_integer())))
                it.value() = c[it.key()];
    }
    validate_config(config_);
    session_ = std::to_string(utc_now_ms()) + "-" + std::to_string(GetCurrentProcessId());
    state_ = {{"input_ready", false},      {"input_error", ""},
              {"storage_error", ""},       {"network", Json::object()},
              {"session", session_},       {"last_flush_ms", 0},
              {"mouse_clicks_session", 0}, {"keyboard_presses_session", 0},
              {"version", "0.2.0-rc1"}};
}
CoreService::~CoreService() { stop(); }
void CoreService::start() {
    enabled_.store(config_.value("capture_on_launch", false));
    worker_thread_ = std::thread([this] { worker(); });
    input_thread_ = std::thread([this] {
        try {
            RawInputHost host({0, 1440, true, this});
            host.run();
        } catch (const std::exception &e) {
            enabled_.store(false);
            std::lock_guard lock(mutex_);
            state_["input_error"] = e.what();
        }
        input_window_.store(nullptr);
        {
            std::lock_guard lock(mutex_);
            input_done_ = true;
            state_["input_ready"] = false;
        }
        wake_.notify_all();
    });
}
void CoreService::stop() {
    if (!input_thread_.joinable() && !worker_thread_.joinable()) {
        retry_final_flush();
        return;
    }
    stopping_.store(true);
    enabled_.store(false);
    if (auto window = input_window_.load())
        PostMessageW(window, WM_CLOSE, 0, 0);
    if (input_thread_.joinable())
        input_thread_.join();
    else {
        std::lock_guard lock(mutex_);
        input_done_ = true;
    }
    wake_.notify_all();
    if (worker_thread_.joinable())
        worker_thread_.join();
}
void CoreService::retry_final_flush() {
    Json rows, events;
    {
        std::lock_guard lock(mutex_);
        rows = unsaved_rows_;
        events = unsaved_events_;
    }
    if (rows.empty() && events.empty())
        return;
    try {
        DatabaseManager db(data_ / "monitor.db");
        db.save(session_, rows, events);
        std::lock_guard lock(mutex_);
        unsaved_rows_ = Json::array();
        unsaved_events_ = Json::array();
        state_["storage_error"] = "";
        state_["pending_retry_rows"] = 0;
        state_["last_flush_ms"] = utc_now_ms();
    } catch (const std::exception &e) {
        std::lock_guard lock(mutex_);
        state_["storage_error"] = e.what();
    }
}
void CoreService::on_ready(HWND window) noexcept {
    input_window_.store(window);
    {
        std::lock_guard lock(mutex_);
        state_["input_ready"] = true;
    }
    if (stopping_.load())
        PostMessageW(window, WM_CLOSE, 0, 0);
}
void CoreService::on_keyboard(std::uintptr_t d, USHORT scan, USHORT flags, USHORT vk,
                              std::int64_t minute) noexcept {
    if (keyboard_generation_ != reset_generation_.load()) {
        keyboard_.reset_state();
        keyboard_generation_ = reset_generation_.load();
    }
    keyboard_.consume(d, scan, flags, vk, minute);
}
void CoreService::on_input_metrics(std::uint64_t mouse, std::uint64_t keyboard,
                                   std::uint64_t errors) noexcept {
    try {
        std::lock_guard lock(mutex_);
        state_["mouse_packets"] = mouse;
        state_["keyboard_packets"] = keyboard;
        state_["raw_read_errors"] = errors;
        if (errors) {
            enabled_.store(false);
            state_["input_error"] = "Raw Input read errors: " + std::to_string(errors);
        }
    } catch (...) {
        enabled_.store(false);
    }
}
void CoreService::on_tick(const MouseMonitor &mouse) noexcept {
    try {
        Json rows = Json::array();
        const auto minute = utc_now_ms() / 60000;
        const auto now = utc_now_ms();
        if (capture_enabled() && last_coverage_ms_ > 0 && now > last_coverage_ms_ &&
            now - last_coverage_ms_ < 5000) {
            auto begin = last_coverage_ms_;
            while (begin < now) {
                auto end = std::min(now, (begin / 60000 + 1) * 60000);
                coverage_[begin / 60000] += static_cast<std::uint64_t>(end - begin);
                begin = end;
            }
        }
        last_coverage_ms_ = now;
        for (auto it = coverage_.begin(); it != coverage_.end();)
            if (it->first < minute - 119)
                it = coverage_.erase(it);
            else {
                rows.push_back(row(it->first, "coverage", "observed_ms", it->second));
                ++it;
            }
        for (const auto &m : mouse.query(std::max<std::int64_t>(0, minute - 119), minute + 1)) {
            for (std::size_t i = 0; i < source_count; ++i)
                if (m.sources[i].total_clicks())
                    rows.push_back(row(m.utc_minute, "mouse",
                                       std::string("source/") + source_names[i] + "/clicks",
                                       m.sources[i].total_clicks()));
            const auto c = m.total();
            for (std::size_t b = 0; b < 5; ++b) {
                if (c.clicks[b])
                    rows.push_back(row(m.utc_minute, "mouse", button_names[b], c.clicks[b]));
                if (c.temporal_double_clicks[b])
                    rows.push_back(row(m.utc_minute, "mouse", std::string("double/") + button_names[b],
                                       c.temporal_double_clicks[b]));
            }
            for (const auto &[name, value] : std::initializer_list<std::pair<const char *, std::uint64_t>>{
                     {"wheel_up_events", c.wheel_up_events},
                     {"wheel_down_events", c.wheel_down_events},
                     {"wheel_up_delta", c.wheel_up_delta},
                     {"wheel_down_delta", c.wheel_down_delta},
                     {"horizontal_events", c.horizontal_events}})
                if (value)
                    rows.push_back(row(m.utc_minute, "mouse", name, value));
            if (c.horizontal_delta)
                rows.push_back({{"minute", m.utc_minute},
                                {"module", "mouse"},
                                {"metric", "horizontal_delta"},
                                {"value", c.horizontal_delta}});
        }
        for (const auto &k : keyboard_.minutes())
            if (k.minute >= minute - 119)
                for (std::size_t i = 0; i < key_slots; ++i) {
                    if (k.presses[i])
                        rows.push_back(row(k.minute, "keyboard", std::to_string(i), k.presses[i]));
                    if (k.repeats[i])
                        rows.push_back(row(k.minute, "repeat", std::to_string(i), k.repeats[i]));
                }
        MouseCounts lifetime;
        for (const auto &s : mouse.lifetime())
            lifetime += s;
        std::lock_guard lock(mutex_);
        input_rows_ = std::move(rows);
        state_["mouse_clicks_session"] = lifetime.total_clicks();
        state_["keyboard_presses_session"] = keyboard_.total();
        state_["keyboard_state_capacity_events"] = keyboard_.state_capacity_events();
        state_["heartbeat_ms"] = utc_now_ms();
        state_["mouse_diagnostics"] = {{"duplicate_downs", mouse.diagnostics().duplicate_downs},
                                       {"state_capacity_events", mouse.diagnostics().state_capacity_events}};
    } catch (const std::exception &e) {
        enabled_.store(false);
        std::lock_guard lock(mutex_);
        state_["input_error"] = e.what();
    }
}
Json CoreService::status() const {
    std::lock_guard lock(mutex_);
    Json result = state_;
    result["monitoring"] = enabled_.load();
    result["session_locked"] = session_locked_.load();
    result["config"] = config_;
    return result;
}
void CoreService::request(Json request, std::function<void(Json)> reply) {
    validate_request(request);
    if (request["type"] == "get_status") {
        reply({{"type", "response"},
               {"request_id", request["request_id"]},
               {"ok", true},
               {"data", status()},
               {"error", nullptr}});
        return;
    }
    std::lock_guard lock(mutex_);
    if (worker_done_)
        throw std::runtime_error("Storage worker stopped: " + state_.value("storage_error", ""));
    if (requests_.size() >= 32)
        throw std::runtime_error("Request queue busy");
    requests_.emplace_back(std::move(request), std::move(reply));
    wake_.notify_one();
}
void CoreService::on_reset() noexcept {
    try {
        keyboard_ = KeyboardMonitor();
        coverage_.clear();
        last_coverage_ms_ = 0;
        std::lock_guard lock(mutex_);
        input_rows_ = Json::array();
        state_["mouse_clicks_session"] = 0;
        state_["keyboard_presses_session"] = 0;
    } catch (...) {
        enabled_.store(false);
    }
}
void CoreService::save_config() {
    const auto temp = data_ / "config.json.tmp";
    {
        std::ofstream out(temp);
        out << config_.dump(2);
        out.close();
        if (!out)
            throw std::runtime_error("Cannot save configuration");
    }
    if (!MoveFileExW(temp.c_str(), (data_ / "config.json").c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error("Cannot replace configuration");
}
Json CoreService::execute(const Json &request, DatabaseManager &db) {
    const auto type = request.at("type").get<std::string>();
    const auto p = request.value("params", Json::object());
    if (type == "ping")
        return {{"pong", true}};
    if (type == "get_status")
        return status();
    if (type == "set_monitoring") {
        const bool active = p.at("enabled").get<bool>();
        {
            std::lock_guard lock(mutex_);
            if (active && (!state_["input_error"].get<std::string>().empty() ||
                           !state_["storage_error"].get<std::string>().empty()))
                throw std::runtime_error("Resolve core/storage error before enabling capture");
        }
        ++reset_generation_;
        if (auto h = input_window_.load())
            PostMessageW(h, WM_APP + 16, 0, 0);
        enabled_.store(active);
        return status();
    }
    if (type == "get_statistics") {
        const auto from = p.at("from").get<std::int64_t>(), to = p.at("to").get<std::int64_t>(),
                   floor = utc_now_ms() / 60000 - 119;
        const int bucket = p.value("bucket", 1);
        auto historical = db.query(from, to, bucket, session_, floor);
        Json live;
        {
            std::lock_guard lock(mutex_);
            live = input_rows_;
        }
        for (const auto &r : network_rows_)
            live.push_back(r);
        std::map<std::tuple<std::int64_t, std::string, std::string>, std::int64_t> values;
        for (const auto &r : historical)
            values[{r["minute"], r["module"], r["metric"]}] += r["value"].get<std::int64_t>();
        for (const auto &r : live) {
            auto minute = r["minute"].get<std::int64_t>();
            if (minute >= floor && minute >= from && minute < to)
                values[{minute / bucket * bucket, r["module"], r["metric"]}] +=
                    r["value"].get<std::int64_t>();
        }
        Json result = Json::array();
        for (const auto &[key, value] : values)
            result.push_back({{"minute", std::get<0>(key)},
                              {"module", std::get<1>(key)},
                              {"metric", std::get<2>(key)},
                              {"value", value}});
        auto events = db.network_events(from, to);
        for (const auto &e : network_events_) {
            auto ms = e["observed_ms"].get<std::int64_t>();
            if (ms >= from * 60000 && ms < to * 60000)
                events.push_back(e);
        }
        return {{"rows", result},     {"network_events", events},
                {"from", from},       {"to", to},
                {"time_zone", "UTC"}, {"freshness", "实时聚合快照 + SQLite 历史；快照约 1 秒更新"}};
    }
    if (type == "set_config") {
        std::lock_guard lock(mutex_);
        Json next = config_;
        for (auto it = p.begin(); it != p.end(); ++it) {
            if (!next.contains(it.key()) ||
                (it.value().type() != next[it.key()].type() &&
                 !(it.value().is_number_integer() && next[it.key()].is_number_integer())))
                throw std::invalid_argument("Unknown setting or wrong value type");
            next[it.key()] = it.value();
        }
        validate_config(next);
        auto previous = config_;
        config_ = next;
        try {
            save_config();
        } catch (...) {
            config_ = previous;
            throw;
        }
        return config_;
    }
    if (type == "export_csv") {
        auto name = "pulsetrace-export-" + std::to_string(utc_now_ms()) + ".csv";
        auto data = execute({{"type", "get_statistics"},
                             {"params", {{"from", p.at("from")}, {"to", p.at("to")}, {"bucket", 1}}}},
                            db)["rows"];
        std::ofstream out(data_ / name, std::ios::binary);
        out << "utc_minute,module,metric,value\r\n";
        for (const auto &r : data)
            out << r["minute"] << ',' << r["module"].get<std::string>() << ','
                << r["metric"].get<std::string>() << ',' << r["value"] << "\r\n";
        out.close();
        if (!out)
            throw std::runtime_error("Export write failed");
        return {{"file", name}};
    }
    if (type == "clear_data") {
        if (p.value("confirmation", "") != "DELETE")
            throw std::invalid_argument("Explicit DELETE confirmation required");
        if (enabled_.load())
            throw std::runtime_error("Pause monitoring before clearing data");
        if (auto h = input_window_.load()) {
            DWORD_PTR result{};
            if (!SendMessageTimeoutW(h, WM_APP + 17, 0, 0, SMTO_ABORTIFHUNG, 3000, &result) || !result)
                throw std::runtime_error("Input reset did not complete; database preserved");
        }
        db.clear();
        clear_requested_ = true;
        return {{"cleared", true}};
    }
    throw std::invalid_argument("Unsupported message type");
}
void CoreService::worker() {
    try {
        DatabaseManager db(data_ / "monitor.db");
        NetworkMonitor network;
        std::map<std::string, std::int64_t> saved;
        auto last_flush = std::chrono::steady_clock::now() - std::chrono::seconds(300);
        auto last_prune = last_flush;
        auto last_network = last_flush;
        auto last_attempt = last_flush;
        int retry_seconds = 1;
        Json net_rows = Json::array();
        std::map<std::string, Json> retry_rows;
        for (;;) {
            std::deque<std::pair<Json, std::function<void(Json)>>> pending;
            Json cfg, rows;
            bool done{};
            {
                std::unique_lock lock(mutex_);
                wake_.wait_for(lock, std::chrono::seconds(1),
                               [this] { return !requests_.empty() || (stopping_.load() && input_done_); });
                pending.swap(requests_);
                cfg = config_;
                rows = input_rows_;
                done = stopping_.load() && input_done_;
            }
            if (std::chrono::steady_clock::now() - last_network >= std::chrono::seconds(1))
                try {
                    auto sample = network.sample(utc_now_ms() / 60000, enabled_.load(),
                                                 cfg.value("network_interface", ""));
                    net_rows = std::move(sample.rows);
                    network_rows_ = net_rows;
                    for (const auto &e : sample.events)
                        network_events_.push_back(e);
                    last_network = std::chrono::steady_clock::now();
                    std::lock_guard lock(mutex_);
                    state_["network"] = std::move(sample.status);
                    state_["network_error"] = "";
                } catch (const std::exception &e) {
                    std::lock_guard lock(mutex_);
                    state_["network_error"] = e.what();
                }
            const auto now = std::chrono::steady_clock::now();
            if (done || (now - last_flush >= std::chrono::seconds(cfg.value("flush_seconds", 30)) &&
                         now - last_attempt >= std::chrono::seconds(retry_seconds))) {
                last_attempt = now;
                for (const auto &r : net_rows)
                    rows.push_back(r);
                Json changed = Json::array();
                std::map<std::string, std::int64_t> updates;
                for (const auto &r : rows) {
                    std::string key = std::to_string(r["minute"].get<std::int64_t>()) + "/" +
                                      r["module"].get<std::string>() + "/" + r["metric"].get<std::string>();
                    auto value = r["value"].get<std::int64_t>();
                    if (!saved.contains(key) || saved[key] != value) {
                        changed.push_back(r);
                        updates[key] = value;
                    }
                }
                for (const auto &r : changed) {
                    const auto key = std::to_string(r["minute"].get<std::int64_t>()) + "/" +
                                     r["module"].get<std::string>() + "/" + r["metric"].get<std::string>();
                    retry_rows[key] = r;
                }
                changed = Json::array();
                for (const auto &[key, r] : retry_rows) {
                    changed.push_back(r);
                    updates[key] = r["value"].get<std::int64_t>();
                }
                try {
                    if (!changed.empty() || !network_events_.empty())
                        db.save(session_, changed, network_events_);
                    for (const auto &[k, v] : updates)
                        saved[k] = v;
                    retry_rows.clear();
                    network_events_ = Json::array();
                    retry_seconds = 1;
                    const auto cutoff = utc_now_ms() / 60000 - 120;
                    for (auto it = saved.begin(); it != saved.end();)
                        if (std::stoll(it->first.substr(0, it->first.find('/'))) < cutoff)
                            it = saved.erase(it);
                        else
                            ++it;
                    if (now - last_prune > std::chrono::hours(1)) {
                        db.prune(utc_now_ms() / 60000 - cfg.value("retention_days", 30) * 1440);
                        last_prune = now;
                    }
                    last_flush = now;
                    std::lock_guard lock(mutex_);
                    state_["last_flush_ms"] = utc_now_ms();
                    state_["storage_error"] = "";
                    unsaved_rows_ = Json::array();
                    unsaved_events_ = Json::array();
                    state_["pending_retry_rows"] = 0;
                } catch (const std::exception &e) {
                    enabled_.store(false);
                    retry_seconds = std::min(60, retry_seconds * 2);
                    std::lock_guard lock(mutex_);
                    state_["storage_error"] = e.what();
                    state_["pending_retry_rows"] = retry_rows.size();
                    unsaved_rows_ = changed;
                    unsaved_events_ = network_events_;
                }
            }
            for (auto &[request, reply] : pending) {
                Json response = {{"type", "response"},
                                 {"request_id", request.value("request_id", Json(nullptr))},
                                 {"ok", true},
                                 {"error", nullptr}};
                try {
                    response["data"] = execute(request, db);
                } catch (const std::exception &e) {
                    response["ok"] = false;
                    response["error"] = {{"code", "REQUEST_FAILED"}, {"message", e.what()}};
                }
                try {
                    reply(std::move(response));
                } catch (...) {
                }
            }
            if (clear_requested_) {
                network = NetworkMonitor();
                net_rows = Json::array();
                network_rows_ = Json::array();
                network_events_ = Json::array();
                saved.clear();
                retry_rows.clear();
                clear_requested_ = false;
            }
            if (done)
                break;
        }
    } catch (const std::exception &e) {
        enabled_.store(false);
        std::lock_guard lock(mutex_);
        state_["storage_error"] = e.what();
        for (auto &[request, reply] : requests_)
            try {
                reply({{"type", "response"},
                       {"request_id", request.value("request_id", Json(nullptr))},
                       {"ok", false},
                       {"error", {{"code", "DATABASE_UNAVAILABLE"}, {"message", e.what()}}}});
            } catch (...) {
            }
        requests_.clear();
    }
    {
        std::lock_guard lock(mutex_);
        worker_done_ = true;
    }
}
} // namespace pulsetrace
