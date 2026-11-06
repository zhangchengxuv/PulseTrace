#include "pulsetrace/core_service.hpp"
#include "pulsetrace/protocol.hpp"
#include "pulsetrace/version.hpp"
#include "pulsetrace/localization.hpp"
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
namespace pulsetrace {
namespace {
void validate_config(const Json &c) {
    for(const char *key:{"usage_enabled","usage_icons"})if(!c.at(key).is_boolean())throw std::invalid_argument("Expected boolean usage setting");
    for(const auto &[key,lo,hi]:{std::tuple{"usage_idle_seconds",15,3600},std::tuple{"usage_retention_days",1,365}})if(!c.at(key).is_number_integer()||c.at(key)<lo||c.at(key)>hi)throw std::invalid_argument("Invalid usage setting");
    if(!c.at("usage_exclusions").is_array()||c.at("usage_exclusions").size()>128)throw std::invalid_argument("Invalid usage exclusions");
    for(const auto &v:c.at("usage_exclusions"))if(!v.is_string()||v.get<std::string>().empty()||v.get<std::string>().size()>2048)throw std::invalid_argument("Invalid usage exclusion");
    for(const char *key:{"ranking_windows_enabled","ranking_mihomo_enabled","ranking_domains_enabled"})if(!c.at(key).is_boolean())throw std::invalid_argument("Expected boolean ranking setting");
    if(!c.at("ranking_retention_days").is_number_integer()||c.at("ranking_retention_days")<1||c.at("ranking_retention_days")>365)throw std::invalid_argument("Invalid ranking retention");
    validate_controller(c.at("mihomo_controller").get<std::string>());
    const auto language=c.at("language").get<std::string>();if(language!="system"&&language!="zh-CN"&&language!="en-US")throw std::invalid_argument("Invalid interface language");
    if(!c.at("config_version").is_number_integer()||c.at("config_version")!=1)throw std::invalid_argument("Unsupported configuration version; original file preserved");
    const auto channel=c.at("update_channel").get<std::string>();if(channel!="stable"&&channel!="preview")throw std::invalid_argument("Invalid update channel");
    const auto source=c.at("update_source").get<std::string>();if(source!="auto"&&source!="github"&&source!="gitee")throw std::invalid_argument("Invalid update source");
    for(const auto &[key,lo,hi]:{std::tuple{"taskbar_font_size",8,12},std::tuple{"taskbar_precision",0,3},std::tuple{"taskbar_offset_x",-200,200},std::tuple{"taskbar_offset_y",-200,200}})if(!c.at(key).is_number_integer()||c.at(key)<lo||c.at(key)>hi)throw std::invalid_argument("Invalid taskbar style number");
    const auto font=c.at("taskbar_font").get<std::string>();if(font!="Segoe UI Variable Text"&&font!="Segoe UI"&&font!="Microsoft YaHei UI"&&font!="Consolas")throw std::invalid_argument("Unsupported taskbar font");
    const auto unit=c.at("taskbar_unit").get<std::string>();if(unit!="auto"&&unit!="bytes"&&unit!="kib"&&unit!="mib")throw std::invalid_argument("Invalid taskbar unit");
    for(const char *key:{"taskbar_upload_color","taskbar_download_color"}){const auto value=c.at(key).get<std::string>();if(value.size()!=7||value[0]!='#'||value.substr(1).find_first_not_of("0123456789abcdefABCDEF")!=std::string::npos)throw std::invalid_argument("Invalid taskbar color");}
    const auto mode=c.at("taskbar_mode").get<std::string>();if(mode!="embedded"&&mode!="floating"&&mode!="tray")throw std::invalid_argument("Invalid taskbar mode");
    const auto background=c.at("taskbar_background").get<std::string>();if(background!="transparent"&&background!="subtle")throw std::invalid_argument("Invalid taskbar background");
    if(!c.at("taskbar_monitor").is_number_integer()||c.at("taskbar_monitor")<0||c.at("taskbar_monitor")>32)throw std::invalid_argument("Invalid taskbar monitor");
    for(const char *key:{"close_to_tray","capture_on_launch","check_updates_on_start"})if(!c.at(key).is_boolean())throw std::invalid_argument("Expected boolean setting");
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
               {"network_interface", ""}, {"close_to_tray", true}, {"capture_on_launch", false},
               {"config_version",1},{"update_channel","stable"},{"update_source","auto"},{"check_updates_on_start",false},
               {"taskbar_font","Segoe UI Variable Text"},{"taskbar_font_size",10},{"taskbar_unit","auto"},{"taskbar_precision",1},{"taskbar_upload_color","#3498db"},{"taskbar_download_color","#8bc4ea"},{"taskbar_offset_x",0},{"taskbar_offset_y",0},{"taskbar_mode","embedded"},{"taskbar_monitor",0},{"taskbar_background","transparent"},{"language","system"},
               {"ranking_windows_enabled",false},{"ranking_mihomo_enabled",false},{"ranking_domains_enabled",false},{"ranking_retention_days",30},{"mihomo_controller",""},{"usage_enabled",true},{"usage_idle_seconds",120},{"usage_exclusions",Json::array()},{"usage_icons",true},{"usage_retention_days",30}};
    std::ifstream in(data_ / "config.json");
    bool migrate=false;
    if (in) {
        Json c;
        in >> c;
        if(!c.is_object())throw std::invalid_argument("Configuration must be an object");
        if(c.contains("config_version")&&(!c["config_version"].is_number_integer()||c["config_version"]>1||c["config_version"]<0))throw std::invalid_argument("Configuration from a newer version; original preserved");
        migrate=c.value("config_version",0)==0;c["config_version"]=1;
        for(auto it=config_.begin();it!=config_.end();++it)if(!c.contains(it.key()))c[it.key()]=it.value();
        config_=std::move(c);
    }
    if(in.is_open())in.close();
    validate_config(config_);
    if(migrate){const auto directory=data_/"backups";std::filesystem::create_directories(directory);std::filesystem::copy_file(data_/"config.json",directory/("config.pre-v0-"+std::to_string(utc_now_ms())+".json"));save_config();}
    session_ = std::to_string(utc_now_ms()) + "-" + std::to_string(GetCurrentProcessId());
    rankings_=std::make_unique<TrafficRankings>(data_);
    usage_=std::make_unique<AppUsageMonitor>();
    state_ = {{"input_ready", false},      {"input_error", ""},
              {"storage_error", ""},       {"network", Json::object()},
              {"session", session_},       {"last_flush_ms", 0},
              {"mouse_clicks_session", 0}, {"keyboard_presses_session", 0},
              {"version", app_version},{"config_version",1},{"config_migration",migrate?"v0 -> v1; backup retained":"current"}};
}
CoreService::~CoreService() { stop(); }
void CoreService::start() {
    enabled_.store(config_.value("capture_on_launch", false));
    capture_window_.start.store(capture_clock());
    capture_window_.stop.store(enabled_.load() ? 0 : capture_clock());
    worker_thread_ = std::thread([this] { worker(); });
    input_thread_ = std::thread([this] {
        try {
            RawInputHost host({0, 1440, true, this});
            host.run();
        } catch (const std::exception &e) {
            disable_capture();
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
    disable_capture();
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
    if (rows.empty() && events.empty() && unsaved_rankings_.empty() && unsaved_usage_.empty())
        return;
    try {
        DatabaseManager db(data_ / "monitor.db");
        db.save(session_, rows, events);
        db.save_rankings(session_,unsaved_rankings_);
        db.save_usage(session_,unsaved_usage_);unsaved_usage_=Json::array();
        unsaved_rankings_=Json::array();
        MonthlyTrafficStore monthly(data_ / "network-months");
        monthly.recover(db);
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
            disable_capture();
            state_["input_error"] = "Raw Input read errors: " + std::to_string(errors);
        }
    } catch (...) {
        disable_capture();
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
        disable_capture();
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
        disable_capture();
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
    if(type=="get_history_bounds")return db.history_bounds();
    if(type=="get_app_usage"||type=="export_app_usage"){
        const auto from=p.at("from").get<std::int64_t>(),to=p.at("to").get<std::int64_t>();
        const auto live=usage_->rows();auto floor=utc_now_ms()/60000-119;
        for(const auto &r:live)floor=std::min(floor,r.at("minute").get<std::int64_t>());
        const int bucket=type=="export_app_usage"?1:p.value("bucket",1);
        auto values=db.usage(from,to,session_,floor,bucket);
        for(const auto &r:live)if(r["minute"]>=from&&r["minute"]<to)values.push_back(r);
        if(type!="export_app_usage"){
            std::map<std::pair<std::int64_t,std::string>,Json> grouped;
            for(auto r:values){r["minute"]=r["minute"].get<std::int64_t>()/bucket*bucket;const auto key=std::make_pair(r["minute"].get<std::int64_t>(),r["key"].get<std::string>());
                if(!grouped.contains(key))grouped[key]=r;else for(const char *f:{"foreground_ms","active_ms"})grouped[key][f]=grouped[key][f].get<std::uint64_t>()+r[f].get<std::uint64_t>();}
            values=Json::array();for(const auto &[key,r]:grouped)values.push_back(r);
        }
        if(type=="export_app_usage"){
            const auto name="pulsetrace-app-usage-"+std::to_string(utc_now_ms())+".csv";std::ofstream out(data_/name,std::ios::binary);
            const auto field=[](std::string text){if(!text.empty()&&std::string("=+-@\t\r").find(text.front())!=std::string::npos)text="'"+text;std::string result="\"";for(char c:text){if(c=='"')result+='"';result+=c;}return result+'"';};
            out<<"utc_minute,application,name,foreground_ms,active_ms\r\n";for(const auto &r:values)out<<r["minute"]<<','<<field(r["key"])<<','<<field(r["name"])<<','<<r["foreground_ms"]<<','<<r["active_ms"]<<"\r\n";
            out.close();if(!out)throw std::runtime_error("Usage export failed");return {{"file",name}};
        }return {{"rows",values},{"status",usage_->status()},{"precision","minute"}};
    }
    if(type=="get_usage_icon"){
        const auto key=p.at("key").get<std::string>();std::string path;for(const auto &r:usage_->rows())if(r["key"]==key){path=r["path"];break;}
        if(path.empty())path=db.usage_icon_path(key);
        return {{"icon",config_.value("usage_icons",true)?application_icon(path):""}};
    }
    if(type=="get_ranking_status")return rankings_->status();
    if(type=="get_ranking_icon"){
        const std::string source=p.at("source"),key=p.at("key");const auto cache=source+"/"+key;
        if(!ranking_icons_.contains(cache)){
            if(ranking_icons_.size()>=128)ranking_icons_.clear();std::string path=db.ranking_icon_path(source,key);
            for(const auto &r:rankings_->rows())if(r["source"]==source&&r["kind"]=="app"&&r["key"]==key){path=r["path"];break;}
            ranking_icons_[cache]=application_icon(path);
        }return {{"icon",ranking_icons_[cache]}};
    }
    if(type=="set_mihomo_secret"){rankings_->set_secret(p.at("secret"));return {{"saved",true}};}
    if(type=="clear_ranking_domains"){
        if(enabled_.load())throw std::runtime_error("ranking_pause_required");
        db.clear_rankings(true);rankings_->clear(true);
        Json retained=Json::array();for(const auto &r:unsaved_rankings_)if(r["kind"]!="domain")retained.push_back(r);unsaved_rankings_=std::move(retained);
        return {{"cleared",true}};
    }
    if(type=="get_rankings"){
        const auto from=p.at("from").get<std::int64_t>(),to=p.at("to").get<std::int64_t>();const std::string source=p.at("source"),kind=p.at("kind");const int family=p.at("family");
        const auto live=rankings_->rows();auto floor=utc_now_ms()/60000-119;
        for(const auto &r:live)floor=std::min(floor,r.at("minute").get<std::int64_t>());
        auto values=db.rankings(from,to,source,kind,session_,floor);
        for(const auto &r:live)if(r["source"]==source&&r["kind"]==kind&&r["minute"]>=from&&r["minute"]<to)values.push_back(r);
        std::map<std::string,Json> grouped;unsigned flags=0;
        for(const auto &r:values){if(family&&r["family"]!=family)continue;const std::string key=r["key"];auto &a=grouped[key];if(a.is_null()){a=r;a.erase("minute");a["upload"]=0ULL;a["download"]=0ULL;a["quality"]=0u;a["ipv4_up"]=0ULL;a["ipv4_down"]=0ULL;a["ipv6_up"]=0ULL;a["ipv6_down"]=0ULL;a["unknown_family_bytes"]=0ULL;a["up_bps"]=nullptr;a["down_bps"]=nullptr;}
            for(const char *field:{"upload","download"})a[field]=a[field].get<std::uint64_t>()+r[field].get<std::uint64_t>();
            const std::string prefix=r["family"]==4?"ipv4_":r["family"]==6?"ipv6_":"";
            if(prefix.empty())a["unknown_family_bytes"]=a["unknown_family_bytes"].get<std::uint64_t>()+r["upload"].get<std::uint64_t>()+r["download"].get<std::uint64_t>();else{a[prefix+"up"]=a[prefix+"up"].get<std::uint64_t>()+r["upload"].get<std::uint64_t>();a[prefix+"down"]=a[prefix+"down"].get<std::uint64_t>()+r["download"].get<std::uint64_t>();}
            a["quality"]=a["quality"].get<unsigned>()|r["quality"].get<unsigned>();flags|=r["quality"].get<unsigned>();
        }
        for(const auto &rate:rankings_->rates())if(rate["source"]==source&&rate["kind"]==kind&&(!family||rate["family"]==family)&&grouped.contains(rate["key"].get<std::string>())){auto &a=grouped.at(rate["key"]);for(const char *field:{"up_bps","down_bps"})a[field]=(a[field].is_null()?0:a[field].get<double>())+rate[field].get<double>();}
        Json result=Json::array();for(auto &[key,r]:grouped){r["family"]=family;r["total"]=r["upload"].get<std::uint64_t>()+r["download"].get<std::uint64_t>();result.push_back(std::move(r));}
        return {{"rows",result},{"status",rankings_->status()},{"source",source},{"basis",source=="mihomo"?"connection_payload_counters":"etw_reported_size"},{"quality_flags",flags},{"reference_validated",false},{"sources_additive",false},{"time_basis",source=="mihomo"?"sample_end_minute":"event_minute"}};
    }
    if (type == "get_network_calendar") {auto result=monthly_->daily(p.value("month",""));result["last_flush_ms"]=status()["last_flush_ms"];return result;}
    if (type == "get_monthly_traffic") {
        auto result = monthly_->query(p.value("month", ""));
        result["last_flush_ms"] = status()["last_flush_ms"];
        return result;
    }
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
        if (active) {
            capture_window_.start.store(capture_clock());
            capture_window_.stop.store(0);
        } else
            capture_window_.stop.store(capture_clock());
        enabled_.store(active);
        rankings_->configure(config_,active&&!session_locked_.load());
        usage_->configure(config_,active&&!session_locked_.load());
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
        auto protocol = db.protocol_samples(from, to, bucket, session_, floor);
        // Capture sources and packet-length bases are distinct observations, never additive.
        using ProtocolKey = std::tuple<std::int64_t, std::string, std::string, std::string>;
        const auto protocol_key = [](const Json &record) -> ProtocolKey {
            return {record.at("minute"), record.at("interface_id"), record.at("source"), record.at("basis")};
        };
        std::map<ProtocolKey, Json> live_protocol;
        for (const auto &r : network_rows_)
            if (r.contains("ip_record")) {
                const auto &p = r["ip_record"];
                auto m = p["minute"].get<std::int64_t>();
                if (m >= floor && m >= from && m < to)
                    live_protocol[protocol_key(p)] = p;
            }
        std::map<ProtocolKey, Json> grouped;
        const auto merge = [&](Json p) {
            p["minute"] = p["minute"].get<std::int64_t>() / bucket * bucket;
            const auto key = protocol_key(p);
            if (!grouped.contains(key)) {
                grouped[key] = std::move(p);
                return;
            }
            auto &old = grouped[key];
            for (const auto *k :
                 {"ipv4_up", "ipv4_down", "ipv6_up", "ipv6_down", "accepted", "rejected", "unsupported"})
                old[k] = old[k].get<std::uint64_t>() + p[k].get<std::uint64_t>();
            for (const auto *k : {"events_lost", "buffers_lost"})
                old[k] = std::max(old[k].get<std::uint64_t>(), p[k].get<std::uint64_t>());
            if (p["quality"] == "incomplete_observation")
                old["quality"] = "incomplete_observation";
        };
        for (const auto &p : protocol)
            merge(p);
        for (const auto &[key, p] : live_protocol)
            merge(p);
        protocol = Json::array();
        for (const auto &[key, p] : grouped)
            protocol.push_back(p);
        return {{"rows", result},
                {"network_events", events},
                {"protocol_samples", protocol},
                {"legacy_protocol_split", "unknown"},
                {"from", from},
                {"to", to},
                {"time_zone", "UTC"},
                {"freshness", "实时聚合快照 + SQLite 历史；快照约 1 秒更新"}};
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
        rankings_->configure(config_,enabled_.load()&&!session_locked_.load());
        usage_->configure(config_,enabled_.load()&&!session_locked_.load());
        return config_;
    }
    if(type=="backup_database"){
        const auto folder=data_/"backups"/("manual-"+std::to_string(utc_now_ms()));std::filesystem::create_directories(folder);
        db.backup(folder/"monitor.db");
        if(std::filesystem::exists(data_/"network-months"))for(const auto &item:std::filesystem::directory_iterator(data_/"network-months")){
            if(!item.is_regular_file())continue;const auto ext=item.path().extension();
            std::filesystem::create_directories(folder/"network-months");
            if(ext==".sqlite3")DatabaseManager::backup_file(item.path(),folder/"network-months"/item.path().filename());
            else if(ext==".json")std::filesystem::copy_file(item.path(),folder/"network-months"/item.path().filename());
        }
        {std::ofstream cfg(folder/"config.json");cfg<<config_.dump(2);if(!cfg)throw std::runtime_error("Cannot backup configuration");}
        const auto name=folder.u8string();return {{"directory",std::string(name.begin(),name.end())},{"basis","committed SQLite snapshots"}};
    }
    if (type == "export_csv") {
        auto name = "pulsetrace-export-" + std::to_string(utc_now_ms()) + ".csv";
        auto report = execute({{"type", "get_statistics"},
                               {"params", {{"from", p.at("from")}, {"to", p.at("to")}, {"bucket", 1}}}},
                              db);
        const auto &data = report["rows"];
        std::ofstream out(data_ / name, std::ios::binary);
        out << "utc_minute,module,metric,value\r\n";
        for (const auto &r : data)
            out << r["minute"] << ',' << r["module"].get<std::string>() << ','
                << r["metric"].get<std::string>() << ',' << r["value"] << "\r\n";
        out.close();
        if (!out)
            throw std::runtime_error("Export write failed");
        const auto protocol_name = "pulsetrace-ip-observation-" + std::to_string(utc_now_ms()) + ".csv";
        std::ofstream ip(data_ / protocol_name, std::ios::binary);
        ip << "utc_minute,interface_id,ipv4_up_observed,ipv4_down_observed,ipv6_up_observed,ipv6_down_"
              "observed,source,basis,quality,accepted,rejected,events_lost,buffers_lost,reference_"
              "validated\r\n";
        for (const auto &r : report["protocol_samples"]) {
            ip << r["minute"] << ',' << std::quoted(r["interface_id"].get<std::string>(), '"', '"');
            for (const auto *k : {"ipv4_up", "ipv4_down", "ipv6_up", "ipv6_down"})
                ip << ',' << r[k];
            for (const auto *k : {"source", "basis", "quality"})
                ip << ',' << std::quoted(r[k].get<std::string>(), '"', '"');
            for (const auto *k : {"accepted", "rejected", "events_lost", "buffers_lost"})
                ip << ',' << r[k];
            ip << ",false\r\n";
        }
        ip.close();
        if (!ip)
            throw std::runtime_error("Protocol quality export write failed");
        return {{"file", name}, {"protocol_file", protocol_name}};
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
        monthly_->clear();
        db.clear();
        rankings_->clear(false);usage_->clear();unsaved_usage_=Json::array();
        {std::lock_guard lock(mutex_);unsaved_rows_=Json::array();unsaved_events_=Json::array();unsaved_rankings_=Json::array();state_["pending_retry_rows"]=0;}
        clear_requested_ = true;
        return {{"cleared", true}};
    }
    throw std::invalid_argument("Unsupported message type");
}
void CoreService::worker() {
    try {
        DatabaseManager db(data_ / "monitor.db");
        {std::lock_guard lock(mutex_);state_["database_schema"]=DatabaseManager::supported_schema;state_["database_migration"]=db.migration_info();}
        monthly_ = std::make_unique<MonthlyTrafficStore>(data_ / "network-months");
        monthly_->recover(db);
        NetworkMonitor network(&capture_window_);
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
            rankings_->configure(cfg,enabled_.load()&&!session_locked_.load());
            usage_->configure(cfg,enabled_.load()&&!session_locked_.load());
            if(done){rankings_->stop();usage_->stop();}
            if (done || std::chrono::steady_clock::now() - last_network >= std::chrono::seconds(1))
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
                    monthly_->save(session_, changed);
                    db.save_usage(session_,usage_->rows());usage_->persisted_before(utc_now_ms()/60000-119);
                    db.save_rankings(session_,rankings_->rows());
                    rankings_->persisted_before(utc_now_ms()/60000-119);
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
                        db.prune_usage(utc_now_ms()/60000-cfg.value("usage_retention_days",30)*1440);
                        db.prune_rankings(utc_now_ms()/60000-cfg.value("ranking_retention_days",30)*1440);
                        last_prune = now;
                    }
                    last_flush = now;
                    std::lock_guard lock(mutex_);
                    state_["last_flush_ms"] = utc_now_ms();
                    state_["storage_error"] = "";
                    unsaved_rows_ = Json::array();
                    unsaved_events_ = Json::array();
                    unsaved_usage_=Json::array();
                    unsaved_rankings_=Json::array();
                    state_["pending_retry_rows"] = 0;
                } catch (const std::exception &e) {
                    disable_capture();
                    rankings_->configure(cfg,false);usage_->configure(cfg,false);
                    retry_seconds = std::min(60, retry_seconds * 2);
                    std::lock_guard lock(mutex_);
                    state_["storage_error"] = e.what();
                    state_["pending_retry_rows"] = retry_rows.size();
                    unsaved_rows_ = changed;
                    unsaved_events_ = network_events_;
                    unsaved_usage_=usage_->rows();
                    unsaved_rankings_=rankings_->rows();
                }
            }
            for (auto &[request, reply] : pending) {
                Json response = {{"type", "response"},
                                 {"request_id", request.value("request_id", Json(nullptr))},
                                 {"ok", true},
                                 {"error", nullptr}};
                try {
                    response["data"] = execute(request, db);
                    if (request["type"] == "set_monitoring") {
                        auto sample = network.sample(utc_now_ms() / 60000, enabled_.load(),
                                                     config_.value("network_interface", ""));
                        net_rows = std::move(sample.rows);
                        network_rows_ = net_rows;
                        for (const auto &e : sample.events)
                            network_events_.push_back(e);
                        std::lock_guard lock(mutex_);
                        state_["network"] = std::move(sample.status);
                    }
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
                network = NetworkMonitor(&capture_window_);
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
        disable_capture();
        rankings_->stop();usage_->stop();
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
