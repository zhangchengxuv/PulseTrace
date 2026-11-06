#include "pulsetrace/protocol.hpp"
#include "pulsetrace/campus.hpp"
#include <set>
#include <stdexcept>
#include <vector>
#include <cmath>
namespace pulsetrace {
namespace {
bool valid_envelope(const Json &r){
    return r.is_object()&&r.size()==3&&r.contains("type")&&r["type"].is_string()&&
        r.contains("request_id")&&r["request_id"].is_number_integer()&&r["request_id"].get<double>()>=0&&
        r["request_id"].get<double>()<=9007199254740991.0&&r.contains("params")&&r["params"].is_object();
}
}
std::wstring quote_windows_argument(const std::wstring &argument) {
    std::wstring result = L"\"";
    std::size_t slashes = 0;
    for (const auto c : argument) {
        if (c == L'\\') {
            ++slashes;
            continue;
        }
        result.append(slashes * (c == L'"' ? 2 : 1), L'\\');
        if (c == L'"')
            result += L'\\';
        result += c;
        slashes = 0;
    }
    result.append(slashes * 2, L'\\');
    result += L'"';
    return result;
}
Json parse_message(const std::string &text,Json *request_id) {
    if(request_id)*request_id=nullptr;
    if (text.size() > 65536)
        throw std::invalid_argument("Message exceeds 64 KiB");
    std::vector<std::set<std::string>> objects;
    auto json = Json::parse(text, [&](int depth, Json::parse_event_t event, Json &value) {
        if (depth > 16)
            throw std::invalid_argument("JSON nesting too deep");
        if (event == Json::parse_event_t::object_start)
            objects.emplace_back();
        if (event == Json::parse_event_t::key && !objects.back().insert(value.get<std::string>()).second)
            throw std::invalid_argument("Duplicate JSON field");
        if (event == Json::parse_event_t::object_end)
            objects.pop_back();
        return true;
    });
    // Correlate parameter-validation errors only after bounded, duplicate-free
    // JSON parsing and a valid envelope. Malformed/ambiguous IDs stay unknown.
    if(request_id&&valid_envelope(json))*request_id=json["request_id"];
    validate_request(json);
    return json;
}
void validate_request(const Json &r) {
    if (!valid_envelope(r))
        throw std::invalid_argument("Invalid request envelope");
    const auto type = r["type"].get<std::string>();
    const auto &p = r["params"];
    std::set<std::string> allowed;
    if (type == "retry_persistence" || type == "ping" || type == "get_status" || type == "get_history_bounds" || type == "exit" || type == "hide" || type == "open_monthly_folder" || type=="backup_database" || type=="open_data_folder" || type=="get_update_status" || type=="check_updates" || type=="download_update" || type=="cancel_update" || type=="open_update_folder" || type=="open_release_page" || type=="install_update" || type=="get_campus_status" || type=="open_campus_portal" || type=="campus_rescan" || type=="delete_campus_rule") {
    } else if(type=="campus_pick_field"){
        allowed={"field"};const std::set<std::string> fields(campus_field_names.begin(),campus_field_names.end());
        if(!p.contains("field")||!p["field"].is_string()||!fields.contains(p["field"].get<std::string>()))throw std::invalid_argument("Invalid campus field");
    } else if(type=="save_campus_mapping"){
        allowed={"fields"};if(!p.contains("fields")||!p["fields"].is_object()||p["fields"].size()>11)throw std::invalid_argument("Invalid campus fields");
        const std::set<std::string> fields(campus_field_names.begin(),campus_field_names.end());
        const std::set<std::string> keys={"mode","selector","kind","unit","confidence","value","formula","cycle","reset_on_cycle"};
        for(auto it=p["fields"].begin();it!=p["fields"].end();++it){
            if(!fields.contains(it.key())||!it->is_object()||it->size()>keys.size())throw std::invalid_argument("Invalid campus mapping");
            for(auto value=it->begin();value!=it->end();++value){
                if(!keys.contains(value.key()))throw std::invalid_argument("Invalid campus mapping key");
                if(value.key()=="mode"&&(!value->is_string()||(*value!="auto"&&*value!="selector"&&*value!="manual"&&*value!="formula"&&*value!="disabled")))throw std::invalid_argument("Invalid campus mode");
                if(value.key()=="selector"&&(!value->is_string()||value->get<std::string>().empty()||value->get<std::string>().size()>2048))throw std::invalid_argument("Invalid campus selector");
                if(value.key()=="kind"&&(!value->is_string()||(*value!="bytes"&&*value!="currency"&&*value!="text")))throw std::invalid_argument("Invalid campus kind");
                if(value.key()=="unit"&&(!value->is_string()||value->get<std::string>().size()>16))throw std::invalid_argument("Invalid campus unit");
                if(value.key()=="confidence"&&(!value->is_number()||value->get<double>()<0||value->get<double>()>1))throw std::invalid_argument("Invalid campus confidence");
                if(value.key()=="value"&&!((value->is_number()&&value->get<double>()>=0)||(value->is_string()&&!value->get<std::string>().empty()&&value->get<std::string>().size()<=128)))throw std::invalid_argument("Invalid manual campus value");
                if(value.key()=="formula"&&(!value->is_string()||value->get<std::string>().empty()||value->get<std::string>().size()>512))throw std::invalid_argument("Invalid campus formula");
                if(value.key()=="cycle"&&(!value->is_string()||value->get<std::string>().size()>128))throw std::invalid_argument("Invalid campus cycle");
                if(value.key()=="reset_on_cycle"&&!value->is_boolean())throw std::invalid_argument("Invalid campus cycle flag");
            }
        }
    } else if (type == "selftest_failure") {
        allowed={"message"};if(!p.contains("message")||!p["message"].is_string()||p["message"].get<std::string>().size()>512)throw std::invalid_argument("Invalid selftest failure");
    } else if (type == "selftest_snapshot") {
        allowed={"phase","metrics"};
        const auto phase=p.value("phase",std::string());
        if(phase!="top"&&phase!="scrolled"&&phase!="opaque"&&phase!="calendar-en"&&phase!="calendar-zh"&&phase!="navigation")throw std::invalid_argument("Invalid selftest phase");
        if(p.contains("metrics")){
            const std::set<std::string> keys={"frames","mean_ms","p95_ms","fps","scroll_px","toolbar_top","blur_enabled","opacity_fallback","navigation_count","cached_count","empty_frames","dom_mean_ms","dom_p95_ms"};
            if(!p["metrics"].is_object()||p["metrics"].size()>keys.size())throw std::invalid_argument("Invalid selftest metrics");
            for(auto it=p["metrics"].begin();it!=p["metrics"].end();++it)
                if(!keys.contains(it.key())||!it->is_number()||!std::isfinite(it->get<double>())||it->get<double>()< -10000||it->get<double>()>1e9)
                    throw std::invalid_argument("Invalid selftest metric");
        }
    } else if (type == "get_monthly_traffic" || type == "get_network_calendar") {
        allowed = {"month"};
        if (p.contains("month")) {
            if (!p["month"].is_string()) throw std::invalid_argument("month must be YYYY-MM or all");
            const auto m = p["month"].get<std::string>();
            if(type=="get_network_calendar"&&m=="all")throw std::invalid_argument("Daily calendar requires one month");
            if (!m.empty() && m != "all" && (m.size()!=7 || m[4]!='-' ||
                m.substr(0,4).find_first_not_of("0123456789")!=std::string::npos ||
                m.substr(5).find_first_not_of("0123456789")!=std::string::npos ||
                m.substr(0,4)<"1970" || m.substr(5)<"01" || m.substr(5)>"12"))
                throw std::invalid_argument("Invalid calendar month");
        }
    } else if(type=="get_rankings"){
        allowed={"from","to","source","kind","family"};
        if(!p.contains("from")||!p.contains("to")||!p["from"].is_number_integer()||!p["to"].is_number_integer()||p["from"]<0||p["from"].get<double>()>9007199254740991.0||p["to"].get<double>()>9007199254740991.0||p["to"]<=p["from"]||p["to"].get<std::int64_t>()>4223371680LL||(p["from"]!=0&&p["to"].get<std::int64_t>()-p["from"].get<std::int64_t>()>527040))throw std::invalid_argument("Invalid ranking range");
        if((p.value("source","")!="windows_etw"&&p.value("source","")!="mihomo")||(p.value("kind","")!="app"&&p.value("kind","")!="domain")||!p.contains("family")||!p["family"].is_number_integer()||(p["family"]!=0&&p["family"]!=4&&p["family"]!=6))throw std::invalid_argument("Invalid ranking source or filter");
    } else if(type=="get_ranking_icon"){
        allowed={"source","key"};if((p.value("source","")!="windows_etw"&&p.value("source","")!="mihomo")||!p.contains("key")||!p["key"].is_string()||p["key"].get<std::string>().size()>2304)throw std::invalid_argument("Invalid application icon identity");
    } else if(type=="set_mihomo_secret"){
        allowed={"secret"};if(!p.contains("secret")||!p["secret"].is_string()||p["secret"].get<std::string>().size()>1024)throw std::invalid_argument("Invalid controller credential");
    } else if(type=="get_ranking_status"){
    } else if(type=="clear_ranking_domains"){
        allowed={"confirmation"};if(p.value("confirmation","")!="DELETE")throw std::invalid_argument("Confirmation required");
    } else if (type == "set_monitoring" || type == "set_autostart") {
        allowed = {"enabled"};
        if (!p.contains("enabled") || !p["enabled"].is_boolean())
            throw std::invalid_argument("enabled must be boolean");
    } else if (type == "get_statistics" || type == "export_csv" || type=="get_app_usage" || type=="export_app_usage" || type=="get_usage_icon") {
        allowed = {"from", "to", "bucket"};
        if(type=="get_usage_icon"){allowed.insert("key");if(!p.contains("key")||!p["key"].is_string()||p["key"].get<std::string>().size()>2048)throw std::invalid_argument("Invalid usage icon key");}
        if (!p.contains("from") || !p.contains("to") || !p["from"].is_number_integer() ||
            !p["to"].is_number_integer())
            throw std::invalid_argument("Range needs integer UTC minutes");
        auto from = p["from"].get<std::int64_t>(), to = p["to"].get<std::int64_t>();
        if (from < 0 || to <= from || to > 4223371680LL || (from != 0 && to - from > 527040))
            throw std::invalid_argument("Custom range must be 1 minute..366 days; from=0 selects all saved data");
        if (p.contains("bucket") && (!p["bucket"].is_number_integer() ||
                                     (p["bucket"] != 1 && p["bucket"] != 60 && p["bucket"] != 1440)))
            throw std::invalid_argument("Invalid aggregation bucket");
    } else if (type == "set_config")
        allowed = {"theme",         "retention_days",   "flush_seconds", "network_interface",
                   "close_to_tray", "capture_on_launch","update_channel","update_source","check_updates_on_start","taskbar_font","taskbar_font_size","taskbar_unit","taskbar_precision","taskbar_upload_color","taskbar_download_color","taskbar_offset_x","taskbar_offset_y","taskbar_mode","taskbar_monitor","taskbar_background","language","ranking_windows_enabled","ranking_mihomo_enabled","ranking_domains_enabled","ranking_retention_days","mihomo_controller","usage_enabled","usage_idle_seconds","usage_exclusions","usage_icons","usage_retention_days","campus_enabled","campus_portal_url","campus_allowed_origins","campus_refresh_minutes","campus_unit_price","campus_price_unit"};
    else if (type == "clear_data")
        allowed = {"confirmation"};
    else
        throw std::invalid_argument("Unknown request type");
    for (auto it = p.begin(); it != p.end(); ++it)
        if (!allowed.contains(it.key()))
            throw std::invalid_argument("Unexpected request parameter");
}
} // namespace pulsetrace
