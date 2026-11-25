#include "pulsetrace/campus.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <optional>
#include <set>
#include <stdexcept>
#include <windows.h>
namespace pulsetrace {
double campus_unit_factor(const std::string &unit,int radix){
    if(radix!=1000&&radix!=1024)throw std::invalid_argument("Invalid campus traffic radix");
    const std::array<std::string,5> school={"B","KB","MB","GB","TB"};
    const std::array<std::string,5> iec={"B","KiB","MiB","GiB","TiB"};
    for(size_t i=0;i<school.size();++i){if(unit==school[i])return std::pow(radix,i);if(unit==iec[i])return std::pow(1024,i);}
    throw std::invalid_argument("Invalid campus source unit");
}
Json campus_measurement(const Json &snapshot){
    if(snapshot.contains("measurement")&&!snapshot["measurement"].empty())return snapshot["measurement"];
    // v7 stored standard decimal-normalized values, but only recent v7 builds
    // retained the original unit. Recover those observations without rewriting them.
    Json raw=Json::object();const auto details=snapshot.value("details",Json::object());
    const auto units=details.value("web_units",Json::object()),values=details.value("web_values",Json::object());
    for(const auto &[field,unit]:units.items()){
        if(!unit.is_string())continue;const auto value=values.value(field,snapshot.value(field,Json(nullptr)));
        if(!value.is_number())continue;
        try{raw[field]={{"value",value.get<double>()/campus_unit_factor(unit.get<std::string>(),1000)},{"unit",unit}};}catch(...){}
    }
    return {{"raw_readings",raw},{"capture_radix",1000},{"status",raw.empty()?"needs_confirmation":"recovered_source_units"},{"basis","portal_convention"}};
}
namespace {
std::string lower(std::string value){
    std::transform(value.begin(),value.end(),value.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
    return value;
}
bool http_url(const std::string &value){
    const auto prefix=lower(value.substr(0,8));
    return prefix.starts_with("http://")||prefix.starts_with("https://");
}
std::filesystem::path rule_path(const std::filesystem::path &data){return data/"campus"/"rules.json";}
}
std::string normalize_campus_portal_url(const std::string &value){
    if(value.empty())return {};
    if(value.size()>2048||!http_url(value)||value.find('@')!=std::string::npos||
       value.find('?')!=std::string::npos||value.find('#')!=std::string::npos)
        throw std::invalid_argument("Portal URL must be an http(s) page URL without credentials, query or fragment");
    if(value.find_first_of(" \t\r\n\\")!=std::string::npos)
        throw std::invalid_argument("Portal URL contains invalid characters");
    const auto authority=value.find("://")+3,slash=value.find('/',authority);
    const auto host_end=slash==std::string::npos?value.size():slash;
    if(authority>=value.size()||host_end==authority)
        throw std::invalid_argument("Portal URL needs a host");
    return value;
}
std::string campus_origin(const std::string &url){
    if(url.empty())return {};
    const auto clean=normalize_campus_portal_url(url);
    const auto start=clean.find("://")+3,end=clean.find('/',start);
    return lower(clean.substr(0,end==std::string::npos?clean.size():end));
}
Json load_campus_rules(const std::filesystem::path &data){
    Json rules={{"format",1},{"portals",Json::array()}};
    const auto file=rule_path(data);
    if(!std::filesystem::exists(file))return rules;
    std::ifstream input(file);input>>rules;
    if(!rules.is_object()||rules.value("format",0)!=1||!rules.contains("portals")||!rules["portals"].is_array()||rules["portals"].size()>256)
        throw std::runtime_error("Campus mapping rules are invalid; original file preserved");
    for(auto &rule:rules["portals"]){
        if(!rule.is_object()||!rule.contains("origin")||!rule["origin"].is_string()||
           !rule.contains("fields")||!rule["fields"].is_object())
            throw std::runtime_error("Campus mapping rule is invalid; original file preserved");
        normalize_campus_mapping(rule["fields"]);
        validate_campus_mapping(rule["fields"]);
    }
    return rules;
}
void save_campus_rules(const std::filesystem::path &data,const Json &rules){
    if(!rules.is_object()||rules.value("format",0)!=1||!rules.contains("portals")||!rules["portals"].is_array())
        throw std::invalid_argument("Invalid campus rule document");
    const auto file=rule_path(data),directory=file.parent_path();std::filesystem::create_directories(directory);
    const auto temp=file.string()+".tmp";std::ofstream output(temp,std::ios::binary|std::ios::trunc);
    output<<rules.dump(2);output.close();
    if(!output)throw std::runtime_error("Cannot write campus mapping rules");
    const auto temporary=std::filesystem::path(temp).wstring(),destination=file.wstring();
    if(!MoveFileExW(temporary.c_str(),destination.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error("Cannot publish campus mapping rules");
}
Json campus_rule_for(const Json &rules,const std::string &origin){
    if(origin.empty()||!rules.contains("portals"))return nullptr;
    for(const auto &rule:rules["portals"])if(lower(rule.value("origin",""))==lower(origin))return rule;
    return nullptr;
}
void validate_campus_mapping(const Json &fields){
    if(!fields.is_object())throw std::invalid_argument("Campus mapping must be an object");
    const std::set<std::string> allowed(campus_field_names.begin(),campus_field_names.end());
    for(auto it=fields.begin();it!=fields.end();++it){
        if(!allowed.contains(it.key())||!it->is_object())throw std::invalid_argument("Unknown campus field mapping");
        const auto &m=*it;
        if(m.contains("traffic_radix")&&(!m["traffic_radix"].is_number_integer()||(m["traffic_radix"]!=1000&&m["traffic_radix"]!=1024)))throw std::invalid_argument("Invalid campus traffic radix");
        if(m.contains("accounting_confirmed")&&!m["accounting_confirmed"].is_boolean())throw std::invalid_argument("Invalid campus accounting confirmation");
        for(const char *key:{"cycle","confirmed_month"})if(m.contains(key)&&(!m[key].is_string()||m[key].get<std::string>().size()>128))throw std::invalid_argument("Invalid campus accounting period");
        const auto mode=m.value("mode","auto");
        if(m.contains("scope")&&(it.key()!="used_bytes"||m["scope"]!="ipv4"))throw std::invalid_argument("Invalid campus traffic scope");
        if(m.contains("period")||m.contains("start_date")){
            if(it.key()!="billing_cycle"||mode!="manual"||m.value("kind","")!="text"||
               !m.contains("period")||(m["period"]!="monthly"&&m["period"]!="yearly")||
               !m.contains("start_date")||!m["start_date"].is_string())throw std::invalid_argument("Invalid campus billing period");
            const auto date=m["start_date"].get<std::string>();
            if(date.size()!=10||date[4]!='-'||date[7]!='-'||
               !std::all_of(date.begin(),date.end(),[](char c){return (c>='0'&&c<='9')||c=='-';})||
               date.substr(0,4).find('-')!=std::string::npos||date.substr(5,2).find('-')!=std::string::npos||date.substr(8,2).find('-')!=std::string::npos)
                throw std::invalid_argument("Invalid campus billing start date");
            const std::chrono::year_month_day day{std::chrono::year{std::stoi(date.substr(0,4))},std::chrono::month{static_cast<unsigned>(std::stoi(date.substr(5,2)))},std::chrono::day{static_cast<unsigned>(std::stoi(date.substr(8,2)))}};
            if(!day.ok()||int(day.year())<100)throw std::invalid_argument("Invalid campus billing start date");
        }
        if(mode!="auto"&&mode!="selector"&&mode!="manual"&&mode!="formula"&&mode!="disabled")
            throw std::invalid_argument("Invalid campus mapping mode");
        if(mode=="auto"||mode=="selector"){
            if(!m.contains("selector")||!m["selector"].is_string()||m["selector"].get<std::string>().empty()||m["selector"].get<std::string>().size()>2048)
                throw std::invalid_argument("Invalid campus selector");
            if(!m.contains("kind")||(m["kind"]!="bytes"&&m["kind"]!="currency"&&m["kind"]!="text"))
                throw std::invalid_argument("Invalid campus field kind");
            if(!m.contains("unit")||!m["unit"].is_string()||m["unit"].get<std::string>().size()>16)
                throw std::invalid_argument("Invalid campus unit");
            if(!m.contains("confidence")||!m["confidence"].is_number()||m["confidence"].get<double>()<0||m["confidence"].get<double>()>1)
                throw std::invalid_argument("Invalid campus confidence");
        }else if(mode=="manual"){
            if(!m.contains("unit")||!m["unit"].is_string()||m["unit"].get<std::string>().size()>16)
                throw std::invalid_argument("Invalid campus unit");
            if(m.value("kind","bytes")=="text"){
                if(!m.contains("value")||!m["value"].is_string()||m["value"].get<std::string>().empty()||m["value"].get<std::string>().size()>128)
                    throw std::invalid_argument("Invalid manual campus text");
            }else if(!m.contains("value")||!m["value"].is_number()||!std::isfinite(m["value"].get<double>())||m["value"].get<double>()<0)
                throw std::invalid_argument("Invalid manual campus value");
            if(m.contains("cycle")&&(!m["cycle"].is_string()||m["cycle"].get<std::string>().size()>128))
                throw std::invalid_argument("Invalid campus cycle");
            if(m.contains("reset_on_cycle")&&!m["reset_on_cycle"].is_boolean())throw std::invalid_argument("Invalid campus cycle reset flag");
        }else if(mode=="formula"){
            if(!m.contains("formula")||!m["formula"].is_string()||m["formula"].get<std::string>().empty()||m["formula"].get<std::string>().size()>512)
                throw std::invalid_argument("Invalid campus formula");
            if(!m.contains("kind")||(m["kind"]!="bytes"&&m["kind"]!="currency"))
                throw std::invalid_argument("Invalid campus formula kind");
        }
    }
}
void normalize_campus_mapping(Json &fields){
    for(auto &[field,mapping]:fields.items()){
        (void)field;
        if(mapping.is_object()&&!mapping.contains("mode"))mapping["mode"]=mapping.contains("selector")?"auto":"disabled";
    }
}

void validate_campus_alert_settings(const Json &settings) {
    if (!settings.is_object() || settings.size() != 4)
        throw std::invalid_argument("Invalid campus alert settings");
    for (const auto *key : {"free_enabled", "balance_enabled"})
        if (!settings.contains(key) || !settings[key].is_boolean())
            throw std::invalid_argument("Invalid campus alert switch");
    for (const auto *key : {"free_gb", "balance_cny"})
        if (!settings.contains(key) || !settings[key].is_number() ||
            !std::isfinite(settings[key].get<double>()) || settings[key] < 0 || settings[key] > 1000000)
            throw std::invalid_argument("Invalid campus alert threshold");
}

namespace {
using namespace std::chrono;
year_month_day campus_local_day(std::int64_t ms) {
    ULARGE_INTEGER ticks{};
    ticks.QuadPart = (ms + 11644473600000LL) * 10000ULL;
    const FILETIME time{ticks.LowPart, ticks.HighPart};
    SYSTEMTIME utc{}, local{};
    if (!FileTimeToSystemTime(&time, &utc) || !SystemTimeToTzSpecificLocalTimeEx(nullptr, &utc, &local))
        throw std::runtime_error("Cannot determine campus alert date");
    return {year{local.wYear}, month{local.wMonth}, day{local.wDay}};
}
std::string campus_day_key(year_month_day date) {
    char text[16]{};
    std::snprintf(text, sizeof(text), "%04d-%02u-%02u",
                  int(date.year()), unsigned(date.month()), unsigned(date.day()));
    return text;
}
std::string campus_period_key(const Json &billing, year_month_day today) {
    const auto period = billing.value("period", "");
    if (period != "monthly" && period != "yearly")
        return {};
    // Billing mappings are validated when loaded or saved.
    const auto start = billing.at("start_date").get<std::string>();
    const year_month_day begin{year{std::stoi(start.substr(0, 4))},
                               month{static_cast<unsigned>(std::stoi(start.substr(5, 2)))},
                               day{static_cast<unsigned>(std::stoi(start.substr(8, 2)))}};
    if (today < begin)
        return {};
    const auto boundary = [&](year_month ym) {
        const auto last_day = year_month_day_last{ym / last}.day();
        return ym / std::min(begin.day(), last_day);
    };
    auto ym = today.year() / (period == "monthly" ? today.month() : begin.month());
    if (today < boundary(ym))
        ym -= months{period == "monthly" ? 1 : 12};
    return campus_day_key(boundary(ym)) + "/" + period;
}
std::optional<double> campus_number(const Json &value, double multiplier = 1) {
    if (!value.is_number())
        return std::nullopt;
    const auto number = value.get<double>() * multiplier;
    return std::isfinite(number) && number >= 0 ? std::optional{number} : std::nullopt;
}
}

Json CampusThresholds::flags() const {
    return {{"low_free", low_free}, {"low_balance", low_balance}};
}

CampusThresholds campus_thresholds(const Json &config, const Json &rule, const Json &snapshot,
                                  std::int64_t now_ms) {
    CampusThresholds thresholds;
    if (!config.value("campus_enabled", false) || !rule.is_object() || !snapshot.is_object() ||
        snapshot.value("quality", "") != "confirmed" ||
        snapshot.value("observed_ms", std::int64_t{}) < rule.value("updated_ms", std::int64_t{}))
        return thresholds;
    const auto settings = config.value("campus_alerts", Json::object());
    const bool free_enabled = settings.value("free_enabled", false);
    const bool balance_enabled = settings.value("balance_enabled", false);
    if (!free_enabled && !balance_enabled)
        return thresholds;
    const auto &fields = rule.at("fields");
    const auto billing = fields.value("billing_cycle", Json::object());
    const auto today = campus_local_day(now_ms);
    const auto observed = campus_local_day(snapshot.at("observed_ms"));
    const auto current_month = campus_day_key(today).substr(0, 7);
    const auto portal_cycle = snapshot.value("cycle_text", "");
    const auto cycle = billing.contains("period") ? campus_period_key(billing, today) :
        billing.value("mode", "") == "manual" ? billing.value("value", "") : portal_cycle;
    const bool same_month = today.year() == observed.year() && today.month() == observed.month();
    const bool valid_cycle = !cycle.empty() &&
        (billing.contains("period") ? cycle == campus_period_key(billing, observed) :
         same_month && (portal_cycle.empty() || cycle == portal_cycle) &&
         (cycle == current_month || (cycle == portal_cycle && today == observed &&
                                    (cycle.size() != 7 || cycle[4] != '-'))));
    if (valid_cycle && (billing.contains("period") || cycle == current_month))
        thresholds.cycle = cycle;
    const int radix = config.value("campus_traffic_radix", 1024);
    const auto raw = campus_measurement(snapshot).value("raw_readings", Json::object());
    const auto traffic = [&](const Json &number, const std::string &unit) -> std::optional<double> {
        try { return campus_number(number, campus_unit_factor(unit, radix)); }
        catch (const std::invalid_argument &) { return std::nullopt; }
    };
    const auto value = [&](const char *name, bool bytes) -> std::optional<double> {
        const auto mapping = fields.value(name, Json::object());
        const auto mode = mapping.value("mode", "disabled");
        if (mode == "manual") {
            if (mapping.value("reset_on_cycle", false) && (!valid_cycle || mapping.value("cycle", "") != cycle))
                return std::nullopt;
            const auto number = mapping.value("value", Json(nullptr));
            return bytes ? traffic(number, mapping.value("unit", "B")) : campus_number(number);
        }
        if (mode != "auto" && mode != "selector")
            return std::nullopt;
        if (!bytes)
            return campus_number(snapshot.value(name, Json(nullptr)));
        if (!raw.contains(name))
            return std::nullopt;
        const auto &reading = raw.at(name);
        return traffic(reading.at("value"), reading.at("unit"));
    };
    if (balance_enabled) {
        const auto balance = value("balance", false);
        thresholds.low_balance = balance && *balance < settings.at("balance_cny").get<double>();
    }
    if (free_enabled) {
        auto free = value("free_remaining_bytes", true);
        if (!free && valid_cycle) {
            const auto quota = value("free_quota_bytes", true);
            auto used = value("used_free_bytes", true);
            const auto mapping = fields.value("used_free_bytes", Json::object());
            auto formula = mapping.value("formula", "");
            std::erase_if(formula, [](unsigned char c) { return std::isspace(c) != 0; });
            if (!used && mapping.value("mode", "") == "formula" && formula == "min(used_bytes,free_quota_bytes)" &&
                mapping.value("accounting_confirmed", false) && mapping.value("cycle", "") == cycle &&
                (billing.contains("period") || mapping.value("confirmed_month", "") == current_month))
                used = value("used_bytes", true);
            if (quota && used)
                free = std::max(0.0, *quota - *used);
        }
        thresholds.low_free = free && *free < settings.at("free_gb").get<double>() * campus_unit_factor("GB", radix);
    }
    return thresholds;
}

Json campus_due_alerts(const CampusThresholds &thresholds, Json &receipts, std::int64_t now_ms) {
    if (!receipts.is_object())
        receipts = Json::object();
    Json due = Json::object();
    constexpr std::int64_t day_ms = 86400000;
    for (const auto &[key, low] : {std::pair{"low_free", thresholds.low_free},
                                   std::pair{"low_balance", thresholds.low_balance}}) {
        if (!low)
            continue;
        const auto previous = receipts.value(key, Json::object());
        const auto cycle = previous.value("cycle", "");
        const auto last = previous.value("notified_ms", std::int64_t{});
        const bool repeated = !cycle.empty() && !thresholds.cycle.empty()
            ? cycle == thresholds.cycle : last > 0 && now_ms - last < day_ms;
        if (!repeated) {
            receipts[key] = {{"cycle", thresholds.cycle}, {"notified_ms", now_ms}};
            due[key] = true;
        }
    }
    return due;
}
}
