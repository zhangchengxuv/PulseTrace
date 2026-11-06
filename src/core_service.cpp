#include "pulsetrace/core_service.hpp"
#include "pulsetrace/protocol.hpp"
#include "pulsetrace/version.hpp"
#include "pulsetrace/localization.hpp"
#include "pulsetrace/pending_store.hpp"
#include "pulsetrace/campus.hpp"
#include <fstream>
#include <iomanip>
#include <array>
#include <sstream>
#include <stdexcept>
#include <sqlite3.h>
namespace pulsetrace {
namespace {
std::string path_utf8(const std::filesystem::path &path) {
    const auto text=path.u8string();return {text.begin(),text.end()};
}
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
    if(!c.at("campus_enabled").is_boolean())throw std::invalid_argument("Expected boolean campus setting");
    normalize_campus_portal_url(c.at("campus_portal_url").get<std::string>());
    if(!c.at("campus_allowed_origins").is_array()||c.at("campus_allowed_origins").size()>8)throw std::invalid_argument("Invalid campus allowed origins");
    for(const auto &origin:c.at("campus_allowed_origins")){
        if(!origin.is_string())throw std::invalid_argument("Campus allowed origin must be text");
        campus_origin(origin.get<std::string>());
    }
    if(!c.at("campus_refresh_minutes").is_number_integer()||c.at("campus_refresh_minutes")<5||c.at("campus_refresh_minutes")>1440)
        throw std::invalid_argument("Campus refresh interval must be 5..1440 minutes");
    if(!c.at("campus_unit_price").is_number()||!std::isfinite(c.at("campus_unit_price").get<double>())||
       c.at("campus_unit_price").get<double>()<0||c.at("campus_unit_price").get<double>()>1000)
        throw std::invalid_argument("Invalid campus unit price");
    if(c.value("campus_price_unit",std::string("GiB"))!="GB"&&c.value("campus_price_unit",std::string("GiB"))!="GiB")
        throw std::invalid_argument("Invalid campus price unit");
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
CoreService::CoreService(std::filesystem::path data) : data_(std::filesystem::absolute(std::move(data)).lexically_normal()) {
    config_ = {{"theme", "system"},       {"retention_days", 30},  {"flush_seconds", 30},
               {"network_interface", ""}, {"close_to_tray", true}, {"capture_on_launch", false},
               {"config_version",1},{"update_channel","stable"},{"update_source","auto"},{"check_updates_on_start",false},
               {"taskbar_font","Segoe UI Variable Text"},{"taskbar_font_size",10},{"taskbar_unit","auto"},{"taskbar_precision",1},{"taskbar_upload_color","#3498db"},{"taskbar_download_color","#8bc4ea"},{"taskbar_offset_x",0},{"taskbar_offset_y",0},{"taskbar_mode","embedded"},{"taskbar_monitor",0},{"taskbar_background","transparent"},{"language","system"},
               {"ranking_windows_enabled",false},{"ranking_mihomo_enabled",false},{"ranking_domains_enabled",false},{"ranking_retention_days",30},{"mihomo_controller",""},{"usage_enabled",true},{"usage_idle_seconds",120},{"usage_exclusions",Json::array()},{"usage_icons",true},{"usage_retention_days",30}};
    config_["campus_enabled"]=false;config_["campus_portal_url"]="";config_["campus_allowed_origins"]=Json::array();config_["campus_refresh_minutes"]=5;config_["campus_unit_price"]=0.0;config_["campus_price_unit"]="GiB";
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
    std::string campus_rule_error;
    try{campus_rules_=load_campus_rules(data_);}catch(const std::exception &e){
        campus_rule_error=e.what();campus_rules_={{"format",1},{"portals",Json::array()}};
    }
    if(migrate){const auto directory=data_/"backups";std::filesystem::create_directories(directory);std::filesystem::copy_file(data_/"config.json",directory/("config.pre-v0-"+std::to_string(utc_now_ms())+".json"));save_config();}
    session_ = std::to_string(utc_now_ms()) + "-" + std::to_string(GetCurrentProcessId());
    rankings_=std::make_unique<TrafficRankings>(data_);
    usage_=std::make_unique<AppUsageMonitor>();
    state_ = {{"input_ready", false},      {"input_error", ""},
              {"storage_error", ""},       {"network", Json::object()},
              {"session", session_},       {"last_flush_ms", 0},
              {"mouse_clicks_session", 0}, {"keyboard_presses_session", 0},
              {"version", app_version},{"config_version",1},{"config_migration",migrate?"v0 -> v1; backup retained":"current"}};
    if(!campus_rule_error.empty())state_["campus_rule_error"]=campus_rule_error;
}
CoreService::~CoreService() { stop(); }
void CoreService::start() {
    if(input_thread_.joinable()||worker_thread_.joinable())throw std::logic_error("Core already started");
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
void CoreService::retry_final_flush(){
 Json batch;{std::lock_guard lock(mutex_);batch=unsaved_batch_;}if(batch.empty())return;
 try{
  PendingStore journal(data_/"pending");try{journal.stage(batch);}catch(const std::exception &e){std::lock_guard lock(mutex_);state_["journal_error"]=e.what();}
  DatabaseManager db(data_/"monitor.db");db.save_batch(batch);
  try{journal.acknowledge(batch.at("session"));}catch(const std::exception &e){std::lock_guard lock(mutex_);state_["journal_error"]=e.what();}
  {std::lock_guard lock(mutex_);unsaved_batch_=Json::object();unsaved_rows_=Json::array();unsaved_events_=Json::array();unsaved_usage_=Json::array();unsaved_rankings_=Json::array();state_["storage_error"]="";state_["pending_retry_rows"]=0;state_["last_flush_ms"]=utc_now_ms();}
  try{MonthlyTrafficStore monthly(data_/"network-months");monthly.recover(db);std::lock_guard lock(mutex_);state_["archive_error"]="";state_["archive_flush_ms"]=utc_now_ms();}
  catch(const std::exception &e){std::lock_guard lock(mutex_);state_["archive_error"]=e.what();}
 }catch(const std::exception &e){std::lock_guard lock(mutex_);state_["storage_error"]=e.what();}
}
bool CoreService::flush_now(std::chrono::milliseconds timeout){const auto target=++flush_requested_;wake_.notify_all();std::unique_lock lock(mutex_);return wake_.wait_for(lock,timeout,[&]{return flush_completed_>=target||worker_done_;})&&flush_completed_>=target&&state_.value("storage_error","").empty();}
bool CoreService::prepare_stop(){
 quiescing_.store(true);std::int64_t unset=0;capture_window_.stop.compare_exchange_strong(unset,capture_clock());
 if(auto h=input_window_.load()){DWORD_PTR ignored{};SendMessageTimeoutW(h,WM_APP+18,0,0,SMTO_ABORTIFHUNG,1500,&ignored);}
 Json config;{std::lock_guard lock(mutex_);config=config_;}usage_->configure(config,false);
 if(!rankings_->quiesce(config))return false;
 return flush_now();
}
void CoreService::cancel_stop()noexcept{const bool was_quiescing=quiescing_.exchange(false);if(was_quiescing&&enabled_.load()&&!suspended_.load()){capture_window_.start.store(capture_clock());capture_window_.stop.store(0);}wake_.notify_all();}
void CoreService::on_power_suspended(bool suspended)noexcept{
 if(suspended_.exchange(suspended)!=suspended)network_rebaseline_.store(true);keyboard_.reset_state();last_coverage_ms_=0;
 if(suspended){std::int64_t unset=0;capture_window_.stop.compare_exchange_strong(unset,capture_clock());}
 else if(enabled_.load()&&!quiescing_.load()){capture_window_.start.store(capture_clock());capture_window_.stop.store(0);}
 on_flush_requested();
}
void CoreService::on_ready(HWND window) noexcept {
    input_window_.store(window);
    {
        std::lock_guard lock(mutex_);
        state_["input_ready"] = true;
    }
    {std::lock_guard lock(mutex_);if(state_["input_error"].get<std::string>().starts_with("Raw Input registration unavailable"))state_["input_error"]="";}
    if (stopping_.load())
        PostMessageW(window, WM_CLOSE, 0, 0);
}
void CoreService::on_session_status(bool registered,unsigned long error)noexcept{try{std::lock_guard lock(mutex_);state_["session_notifications_ready"]=registered;state_["session_notification_error_code"]=error;}catch(...){}}
void CoreService::on_registration_error(unsigned long error)noexcept{try{std::lock_guard lock(mutex_);state_["input_ready"]=false;state_["input_error"]="Raw Input registration unavailable; Win32="+std::to_string(error);}catch(...){}}
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
            state_["input_warning"] = "Raw Input read errors: " + std::to_string(errors);
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
            if (c.horizontal_events)
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
        // Preserve unhanded buckets even if the next snapshot follows a long suspend
        // and has already expired them from the monitor's rolling memory window.
        for(const auto &r:rows)input_pending_[Json::array({r["minute"],r["module"],r["metric"]}).dump()]=r;
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
    result["suspended"] = suspended_.load();result["quiescing"]=quiescing_.load();result["collection_active"]=capture_enabled();
    result["config"] = config_;
    return result;
}
Json CoreService::campus_portal_context() const {
    std::lock_guard lock(mutex_);
    const auto url=config_.value("campus_portal_url",std::string());
    std::vector<std::string> origins;
    const auto primary=campus_origin(url);if(!primary.empty())origins.push_back(primary);
    for(const auto &value:config_.value("campus_allowed_origins",Json::array())){
        try{const auto origin=campus_origin(value.get<std::string>());if(std::find(origins.begin(),origins.end(),origin)==origins.end())origins.push_back(origin);}catch(...){}
    }
    return {{"enabled",config_.value("campus_enabled",false)},
            {"portal_url",url},{"origin",campus_origin(url)},{"allowed_origins",origins},
            {"refresh_minutes",config_.value("campus_refresh_minutes",5)},
            {"rule",campus_rule_for(campus_rules_,campus_origin(url))}};
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
void CoreService::save_campus_snapshot(Json snapshot, std::function<void(Json)> reply){
    std::lock_guard lock(mutex_);
    if(worker_done_)throw std::runtime_error("Storage worker stopped: "+state_.value("storage_error",""));
    if(requests_.size()>=32)throw std::runtime_error("Request queue busy");
    requests_.emplace_back(Json{{"type","save_campus_snapshot"},{"request_id",0u},{"params",std::move(snapshot)}},std::move(reply));
    wake_.notify_one();
}
void CoreService::on_reset() noexcept {
    try {
        keyboard_ = KeyboardMonitor();
        coverage_.clear();
        last_coverage_ms_ = 0;
        std::lock_guard lock(mutex_);
        input_rows_ = Json::array();
        input_pending_.clear();
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
Json CoreService::execute(const Json &request, DatabaseManager *connection) {
    const auto database=[&]() -> DatabaseManager& {if(!connection)throw std::runtime_error("SQLite database unavailable; recovery is pending");return *connection;};
    const auto type = request.at("type").get<std::string>();
    if(type=="retry_persistence"){on_flush_requested();return {{"retry_requested",true}};}
    const auto p = request.value("params", Json::object());
    if (type == "ping")
        return {{"pong", true}};
    if (type == "get_status")
        return status();
    if(type=="get_campus_status"){
        Json config,rules;std::string rule_error;{std::lock_guard lock(mutex_);config=config_;rules=campus_rules_;rule_error=state_.value("campus_rule_error","");}
        const auto url=config.value("campus_portal_url",std::string()),origin=campus_origin(url);
        Json latest=nullptr;
        if(connection&&!origin.empty())latest=connection->campus_latest(origin);
        return {{"enabled",config.value("campus_enabled",false)},
                {"configured",!url.empty()},{"portal_url",url},{"origin",origin},
                {"refresh_minutes",config.value("campus_refresh_minutes",5)},
                {"unit_price",config.value("campus_unit_price",0.0)},
                {"rule",campus_rule_for(rules,origin)},{"latest",latest},
                {"quality_basis","portal-confirmed mapping; values are not inferred from interface totals"},
                {"rule_error",rule_error}};
    }
    if(type=="save_campus_mapping"){
        Json fields=p.at("fields");normalize_campus_mapping(fields);validate_campus_mapping(fields);
        std::string url;Json config;{std::lock_guard lock(mutex_);url=config_.value("campus_portal_url",std::string());config=config_;}
        const auto origin=campus_origin(url);if(origin.empty())throw std::invalid_argument("Configure and save a campus portal URL first");
        Json rules;{std::lock_guard lock(mutex_);rules=campus_rules_;}
        Json rule=nullptr;Json portals=Json::array();
        for(const auto &old:rules["portals"]){
            if(old.value("origin","")==origin)rule=old;else portals.push_back(old);
        }
        if(rule.is_null())rule={{"origin",origin},{"portal_url",url},{"format",1}};
        rule["origin"]=origin;rule["portal_url"]=url;rule["fields"]=fields;rule["updated_ms"]=utc_now_ms();rule["adapter"]="generic_dom";
        rule["allowed_origins"]=config.value("campus_allowed_origins",Json::array());
        rule["refresh_minutes"]=config.value("campus_refresh_minutes",5);
        rule["unit_price"]=config.value("campus_unit_price",0.0);
        rule["price_unit"]=config.value("campus_price_unit",std::string("GiB"));
        rule["last_status"]="confirmed";
        portals.push_back(rule);rules["portals"]=std::move(portals);save_campus_rules(data_,rules);
        {std::lock_guard lock(mutex_);campus_rules_=rules;state_["campus_rule_error"]="";state_["campus_rule_updated_ms"]=utc_now_ms();}
        return {{"saved",true},{"rule",rule}};
    }
    if(type=="delete_campus_rule"){
        std::string url;{std::lock_guard lock(mutex_);url=config_.value("campus_portal_url",std::string());}
        const auto origin=campus_origin(url);Json rules;{std::lock_guard lock(mutex_);rules=campus_rules_;}
        Json portals=Json::array();for(const auto &old:rules["portals"])if(old.value("origin","")!=origin)portals.push_back(old);
        rules["portals"]=std::move(portals);save_campus_rules(data_,rules);
        {std::lock_guard lock(mutex_);campus_rules_=rules;state_["campus_rule_updated_ms"]=utc_now_ms();}
        return {{"deleted",true}};
    }
    if(type=="save_campus_snapshot"){
        std::string configured;{std::lock_guard lock(mutex_);configured=config_.value("campus_portal_url",std::string());}
        const auto url=normalize_campus_portal_url(p.at("portal_url").get<std::string>());
        const auto origin=campus_origin(url),configured_origin=campus_origin(configured);
        if(origin.empty()||configured_origin.empty()||origin!=configured_origin)throw std::invalid_argument("Snapshot origin is not the configured campus portal");
        const std::array<const char*,10> campus_values={"balance","free_quota_bytes","free_remaining_bytes","used_free_bytes",
            "spendable_balance","paid_used_bytes","used_bytes","remaining_bytes","quota_bytes","fee"};
        for(const auto *key:campus_values){
            if(p.contains(key)&&!p[key].is_null()){
                if(!p[key].is_number()||!std::isfinite(p[key].get<double>())||p[key].get<double>()<0)throw std::invalid_argument("Invalid campus snapshot value");
            }
        }
        const auto quality=p.value("quality",std::string());
        if(quality!="confirmed"&&quality!="auto_recognized"&&quality!="low_confidence"&&quality!="unknown")
            throw std::invalid_argument("Invalid campus snapshot quality");
        const auto confidence=p.value("confidence",0.0);
        if(!std::isfinite(confidence)||confidence<0||confidence>1)throw std::invalid_argument("Invalid campus confidence");
        const auto cycle=p.value("cycle_text",std::string());if(cycle.size()>128)throw std::invalid_argument("Billing cycle is too long");
        Json mapped=Json::array();
        if(p.contains("mapped_fields")){
            if(!p["mapped_fields"].is_array()||p["mapped_fields"].size()>11)throw std::invalid_argument("Invalid mapped field summary");
            for(const auto &field:p["mapped_fields"])if(field.is_string()&&field.get<std::string>().size()<=32)mapped.push_back(field);
        }
        Json web_values=Json::object();for(const auto *key:campus_values)if(p.contains(key)&&!p[key].is_null())web_values[key]=p[key];
        Json snapshot={{"observed_ms",utc_now_ms()},{"portal_origin",origin},{"portal_url",url},
                       {"balance",p.value("balance",Json(nullptr))},{"used_bytes",p.value("used_bytes",Json(nullptr))},
                       {"remaining_bytes",p.value("remaining_bytes",Json(nullptr))},{"quota_bytes",p.value("quota_bytes",Json(nullptr))},
                       {"fee",p.value("fee",Json(nullptr))},{"cycle_text",cycle},{"quality",quality},{"confidence",confidence},
                       {"details",{{"mapped_fields",mapped},{"adapter","generic_dom"},{"web_values",web_values}}}};
        database().save_campus_snapshot(snapshot);
        try{
            Json rules;{std::lock_guard lock(mutex_);rules=campus_rules_;}
            bool found=false;for(auto &rule:rules["portals"])if(rule.value("origin","")==origin){
                rule["last_status"]=quality;rule["last_sync_ms"]=snapshot["observed_ms"];found=true;break;
            }
            if(found){save_campus_rules(data_,rules);std::lock_guard lock(mutex_);campus_rules_=rules;state_["campus_rule_updated_ms"]=utc_now_ms();}
        }catch(const std::exception &e){std::lock_guard lock(mutex_);state_["campus_rule_error"]=e.what();}
        return {{"saved",true},{"snapshot",snapshot}};
    }
    if(type=="get_history_bounds")return database().history_bounds();
    if(type=="get_app_usage"||type=="export_app_usage"){
        const auto from=p.at("from").get<std::int64_t>(),to=p.at("to").get<std::int64_t>();
        const auto live=usage_->rows();auto floor=utc_now_ms()/60000-119;
        for(const auto &r:live)floor=std::min(floor,r.at("minute").get<std::int64_t>());
        const int bucket=type=="export_app_usage"?1:p.value("bucket",1);
        auto values=database().usage(from,to,session_,floor,bucket);
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
        if(path.empty())path=database().usage_icon_path(key);
        return {{"icon",config_.value("usage_icons",true)?application_icon(path):""}};
    }
    if(type=="get_ranking_status")return rankings_->status();
    if(type=="get_ranking_icon"){
        const std::string source=p.at("source"),key=p.at("key");const auto cache=source+"/"+key;
        if(!ranking_icons_.contains(cache)){
            if(ranking_icons_.size()>=128)ranking_icons_.clear();std::string path=database().ranking_icon_path(source,key);
            for(const auto &r:rankings_->rows())if(r["source"]==source&&r["kind"]=="app"&&r["key"]==key){path=r["path"];break;}
            ranking_icons_[cache]=application_icon(path);
        }return {{"icon",ranking_icons_[cache]}};
    }
    if(type=="set_mihomo_secret"){rankings_->set_secret(p.at("secret"));return {{"saved",true}};}
    if(type=="clear_ranking_domains"){
        if(enabled_.load())throw std::runtime_error("ranking_pause_required");
        database().clear_rankings(true);rankings_->clear(true);
        Json retained=Json::array();for(const auto &r:unsaved_rankings_)if(r["kind"]!="domain")retained.push_back(r);unsaved_rankings_=std::move(retained);
        return {{"cleared",true}};
    }
    if(type=="get_rankings"){
        const auto from=p.at("from").get<std::int64_t>(),to=p.at("to").get<std::int64_t>();const std::string source=p.at("source"),kind=p.at("kind");const int family=p.at("family");
        const auto live=rankings_->rows();auto floor=utc_now_ms()/60000-119;
        for(const auto &r:live)floor=std::min(floor,r.at("minute").get<std::int64_t>());
        auto values=database().rankings(from,to,source,kind,session_,floor);
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
    if (type == "get_network_calendar") {auto result=(monthly_?monthly_.get():throw std::runtime_error("Monthly archive unavailable"))->daily(p.value("month",""));result["last_flush_ms"]=status().value("archive_flush_ms",std::int64_t(0));return result;}
    if (type == "get_monthly_traffic") {
        auto result = (monthly_?monthly_.get():throw std::runtime_error("Monthly archive unavailable"))->query(p.value("month", ""));
        result["last_flush_ms"] = status().value("archive_flush_ms",std::int64_t(0));
        return result;
    }
    if (type == "set_monitoring") {
        const bool active = p.at("enabled").get<bool>();
        if(active&&(quiescing_.load()||stopping_.load()))throw std::runtime_error("Exit is pending; cancel it before resuming monitoring");
        {
            std::lock_guard lock(mutex_);
            if (active && !state_["input_error"].get<std::string>().empty())
                throw std::runtime_error("Resolve input registration error before enabling capture");
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
        auto historical = database().query(from, to, bucket, session_, floor);
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
        auto events = database().network_events(from, to);
        for (const auto &e : network_events_) {
            auto ms = e["observed_ms"].get<std::int64_t>();
            if (ms >= from * 60000 && ms < to * 60000)
                events.push_back(e);
        }
        auto protocol = database().protocol_samples(from, to, bucket, session_, floor);
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
        database().backup(folder/"monitor.db");
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
                              connection);
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
        PendingStore pending(data_/"pending");std::vector<std::string> sessions{session_};for(const auto &file:pending.files())sessions.push_back(file.stem().string());
        database().clear(sessions);
        if(monthly_)monthly_->clear();
        for(const auto &session:sessions)try{pending.acknowledge(session);}catch(...){}
        session_=std::to_string(utc_now_ms())+"-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(reset_generation_.load());persistence_revision_=0;
        {std::lock_guard lock(mutex_);state_["session"]=session_;unsaved_batch_=Json::object();}
        rankings_->clear(false);usage_->clear();unsaved_usage_=Json::array();
        {std::lock_guard lock(mutex_);unsaved_rows_=Json::array();unsaved_events_=Json::array();unsaved_rankings_=Json::array();state_["pending_retry_rows"]=0;}
        clear_requested_ = true;
        return {{"cleared", true}};
    }
    throw std::invalid_argument("Unsupported message type");
}
void CoreService::worker() {
    PendingStore journal(data_/"pending");std::unique_ptr<DatabaseManager> db;
    NetworkMonitor network(&capture_window_);
    std::map<std::string,std::string> saved;
    std::map<std::string,Json> retry_rows;
    Json saved_usage=Json::array(),saved_rankings=Json::array();
    auto last_flush=std::chrono::steady_clock::now()-std::chrono::hours(1),next_attempt=last_flush,last_network=last_flush;
    auto last_prune=std::chrono::steady_clock::now();
    unsigned failures=0;bool archive_recover=true,retry_blocked=false;
    const auto update_network=[&](const Json &config){
        try{
            if(network_rebaseline_.exchange(false))network.reset_baseline();
            auto sample=network.sample(utc_now_ms()/60000,enabled_.load()&&!suspended_.load()&&!quiescing_.load(),config.value("network_interface",""));
            network_rows_=std::move(sample.rows);
            for(const auto &event:sample.events)network_events_.push_back(event);
            std::lock_guard lock(mutex_);state_["network"]=std::move(sample.status);state_["network_error"]="";
        }catch(const std::exception &e){std::lock_guard lock(mutex_);state_["network_error"]=e.what();}
        last_network=std::chrono::steady_clock::now();
    };
    const auto key_of=[](const Json &r){Json key=Json::array({r.at("minute"),r.at("module"),r.at("metric")});if(r.contains("ip_record")){key.push_back(r["ip_record"]["source"]);key.push_back(r["ip_record"]["basis"]);}return key.dump();};
    const auto record_error=[&](const std::exception &error){
        Json detail={{"path",path_utf8(data_/"monitor.db")},{"message",error.what()}};
        if(const auto *sqlite=dynamic_cast<const SQLiteError*>(&error))detail=sqlite->details();
        detail["time_ms"]=utc_now_ms();
        try{const auto logs=data_/"logs";std::filesystem::create_directories(logs);const auto file=logs/"storage.log";
            if(std::filesystem::exists(file)&&std::filesystem::file_size(file)>1024*1024){std::error_code ec;std::filesystem::remove(logs/"storage.log.1",ec);std::filesystem::rename(file,logs/"storage.log.1",ec);}
            std::ofstream out(file,std::ios::app);out<<detail.dump()<<'\n';
        }catch(...){}
        std::lock_guard lock(mutex_);state_["storage_error"]=error.what();state_["storage_diagnostic"]=detail;state_["storage_state"]="retrying";
    };
    try {
        for(;;){
            std::deque<std::pair<Json,std::function<void(Json)>>> requests;Json cfg,rows;bool done;
            {std::unique_lock lock(mutex_);wake_.wait_for(lock,std::chrono::seconds(1),[&]{return !requests_.empty()||flush_requested_.load()>flush_completed_||(stopping_.load()&&input_done_);});requests.swap(requests_);cfg=config_;rows=Json::array();for(const auto &[key,r]:input_pending_)rows.push_back(r);input_pending_.clear();done=stopping_.load()&&input_done_;}
            const bool collecting=enabled_.load()&&!session_locked_.load()&&!suspended_.load()&&!quiescing_.load();
            rankings_->configure(cfg,collecting);usage_->configure(cfg,collecting);
            if(done){rankings_->stop();usage_->stop();}
            auto now=std::chrono::steady_clock::now();
            if(done||now-last_network>=std::chrono::seconds(1)||flush_requested_.load()>flush_completed_){
                update_network(cfg);
            }
            for(const auto &r:network_rows_)rows.push_back(r);
            for(const auto &r:rows){const auto key=key_of(r);const auto fingerprint=r.dump();if(!saved.contains(key)||saved[key]!=fingerprint)retry_rows[key]=r;}
            const auto requested=flush_requested_.load();
            const bool forced=requested>flush_completed_;
            if(done||forced||(!retry_blocked&&now>=next_attempt&&(!db||now-last_flush>=std::chrono::seconds(cfg.value("flush_seconds",30))))){
                Json changed=Json::array();for(const auto &[key,r]:retry_rows)changed.push_back(r);
                auto usage=usage_->rows(),rankings=rankings_->rows();
                Json batch={{"format",1},{"session",session_},{"revision",++persistence_revision_},{"created_ms",utc_now_ms()},{"rows",changed},{"events",network_events_},{"usage",usage==saved_usage?Json::array():usage},{"rankings",rankings==saved_rankings?Json::array():rankings}};
                const bool has_data=!changed.empty()||!network_events_.empty()||!batch["usage"].empty()||!batch["rankings"].empty();
                std::string journal_error;
                if(has_data)try{journal.stage(batch);}catch(const std::exception &e){journal_error=e.what();}
                try{
                    if(!db){db=std::make_unique<DatabaseManager>(data_/"monitor.db");archive_recover=true;
                        {std::lock_guard lock(mutex_);state_["database_schema"]=DatabaseManager::supported_schema;state_["database_migration"]=db->migration_info();state_["database_path"]=path_utf8(data_/"monitor.db");}
                    }
                    std::vector<std::filesystem::path> pending_files;
                    try{pending_files=journal.files();}catch(const std::exception &e){journal_error=e.what();}
                    {std::lock_guard lock(mutex_);state_["recovery_error"]="";}
                    for(const auto &file:pending_files){
                        if(file.stem().string()==session_)continue;
                        Json recovered;
                        try{recovered=journal.read(file);}catch(const std::exception &e){std::lock_guard lock(mutex_);state_["recovery_error"]=e.what();continue;}
                        if(db->save_batch(recovered))archive_recover=true;
                        try{journal.acknowledge(recovered.at("session"));}catch(const std::exception &e){journal_error=e.what();}
                    }
                    if(has_data)db->save_batch(batch);
                    for(const auto &[key,r]:retry_rows)saved[key]=r.dump();
                    retry_rows.clear();network_events_=Json::array();
                    usage_->persisted_before(utc_now_ms()/60000-119);rankings_->persisted_before(utc_now_ms()/60000-119);
                    saved_usage=Json::array();saved_rankings=Json::array();
                    for(const auto &r:usage)if(r["minute"].get<std::int64_t>()>=utc_now_ms()/60000-119)saved_usage.push_back(r);
                    for(const auto &r:rankings)if(r["minute"].get<std::int64_t>()>=utc_now_ms()/60000-119)saved_rankings.push_back(r);
                    if(has_data)try{journal.acknowledge(session_);}catch(const std::exception &e){journal_error=e.what();}
                    failures=0;retry_blocked=false;last_flush=now;next_attempt=now;
                    {std::lock_guard lock(mutex_);state_["last_flush_ms"]=utc_now_ms();state_["storage_error"]="";state_["storage_diagnostic"]=nullptr;state_["storage_state"]="healthy";state_["journal_error"]=journal_error;state_["pending_retry_rows"]=0;unsaved_batch_=Json::object();unsaved_rows_=Json::array();unsaved_events_=Json::array();unsaved_usage_=Json::array();unsaved_rankings_=Json::array();}
                    // Archive failures never turn a committed primary transaction into lost input.
                    try{if(!monthly_)monthly_=std::make_unique<MonthlyTrafficStore>(data_/"network-months");if(archive_recover){monthly_->recover(*db);archive_recover=false;}else if(has_data)monthly_->save(session_,changed);
                        std::lock_guard lock(mutex_);state_["archive_error"]="";state_["archive_flush_ms"]=utc_now_ms();
                    }catch(const std::exception &e){archive_recover=true;std::lock_guard lock(mutex_);state_["archive_error"]=e.what();}
                    if(!done&&!archive_recover&&now-last_prune>std::chrono::hours(1)){
                        try{db->prune(utc_now_ms()/60000-cfg.value("retention_days",30)*1440);db->prune_usage(utc_now_ms()/60000-cfg.value("usage_retention_days",30)*1440);db->prune_rankings(utc_now_ms()/60000-cfg.value("ranking_retention_days",30)*1440);last_prune=now;std::lock_guard lock(mutex_);state_["retention_error"]="";}
                        catch(const std::exception &e){std::lock_guard lock(mutex_);state_["retention_error"]=e.what();}
                    }
                    const auto cutoff=utc_now_ms()/60000-120;
                    for(auto it=saved.begin();it!=saved.end();)if(Json::parse(it->first)[0].get<std::int64_t>()<cutoff)it=saved.erase(it);else ++it;
                }catch(const std::exception &e){
                    record_error(e);++failures;
                    const auto *sqlite=dynamic_cast<const SQLiteError*>(&e);
                    const int code=sqlite?sqlite->details()["code"].get<int>():-1;
                    const bool environment=sqlite&&(sqlite->transient()||code==SQLITE_READONLY||code==SQLITE_FULL);retry_blocked=!environment;
                    const bool retry=sqlite&&sqlite->transient()&&failures<=5;
                    next_attempt=now+std::chrono::seconds(retry?std::min(30u,1u<<std::min(failures,5u)):60u);
                    // Reopen failed connections after backoff; this also restores WAL/I/O state.
                    db.reset();
                    std::lock_guard lock(mutex_);state_["pending_retry_rows"]=changed.size()+batch["usage"].size()+batch["rankings"].size();state_["journal_error"]=journal_error;state_["storage_state"]=retry?"retrying":"blocked";
                    unsaved_batch_=batch;unsaved_rows_=changed;unsaved_events_=network_events_;unsaved_usage_=usage;unsaved_rankings_=rankings;
                }
                {std::lock_guard lock(mutex_);flush_completed_=requested;}wake_.notify_all();
            }
            for(auto &[request,reply]:requests){Json response={{"type","response"},{"request_id",request.value("request_id",Json(nullptr))},{"ok",true},{"error",nullptr}};
                try{
                    const bool transition=request["type"]=="set_monitoring";
                    // Snapshot the paused baseline before enabling; after pausing, drain
                    // already accepted ETW buffers before acknowledging the UI request.
                    if(transition&&request["params"].value("enabled",false)&&!enabled_.load())update_network(status()["config"]);
                    response["data"]=execute(request,db.get());
                    if(transition){update_network(status()["config"]);response["data"]=status();}
                }
                catch(const std::exception &e){
                    const auto kind=request["type"].get<std::string>();
                    const bool sqlite=dynamic_cast<const SQLiteError*>(&e)!=nullptr;
                    const bool save=kind.starts_with("save_")||kind=="set_config"||kind=="clear_data"||kind.starts_with("clear_");
                    response["ok"]=false;
                    response["error"]={{"code",sqlite&&save?"DATABASE_SAVE_FAILED":(kind.starts_with("get_")||sqlite)?"DATABASE_QUERY_FAILED":"REQUEST_FAILED"},{"message",e.what()}};
                }
                try{reply(std::move(response));}catch(...){}
            }
            if(clear_requested_){network=NetworkMonitor(&capture_window_);network_rows_=Json::array();network_events_=Json::array();saved.clear();retry_rows.clear();saved_usage=Json::array();saved_rankings=Json::array();clear_requested_=false;}
            if(done)break;
        }
    }catch(const std::exception &e){record_error(e);}
    {std::lock_guard lock(mutex_);worker_done_=true;state_["worker_stopped"]=true;for(auto &[request,reply]:requests_)try{reply({{"type","response"},{"request_id",request.value("request_id",Json(nullptr))},{"ok",false},{"error",{{"code","DATABASE_UNAVAILABLE"},{"message",state_.value("storage_error","")}}}});}catch(...){}requests_.clear();}
    wake_.notify_all();
}
} // namespace pulsetrace
