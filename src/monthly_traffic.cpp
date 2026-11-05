#include "pulsetrace/monthly_traffic.hpp"
#include "pulsetrace/raw_input_host.hpp"
#include <fstream>
#include <iomanip>
#include <sstream>
#include <memory>
namespace pulsetrace {
namespace {
std::string utf8(const std::filesystem::path &p){const auto s=p.u8string();return {s.begin(),s.end()};}
bool valid_month(const std::string &s){return s.size()==7&&s[4]=='-'&&s.substr(0,4)>="1970"&&s.substr(0,4)<="9999"&&s.substr(5)>="01"&&s.substr(5)<="12"&&s.substr(0,4).find_first_not_of("0123456789")==std::string::npos&&s.substr(5).find_first_not_of("0123456789")==std::string::npos;}
void atomic_json(const std::filesystem::path &path,const Json &value){
    auto temp=path;temp+=L".tmp";
    std::ofstream out(temp,std::ios::binary);out<<value.dump(2)<<'\n';out.close();
    if(!out||!MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("Cannot publish monthly traffic file: "+utf8(path));
}
}
MonthlyTrafficStore::MonthlyTrafficStore(std::filesystem::path directory):directory_(std::filesystem::absolute(std::move(directory))){
    std::filesystem::create_directories(directory_);
    if(GetDynamicTimeZoneInformation(&zone_)==TIME_ZONE_ID_INVALID)throw std::runtime_error("Cannot determine calendar timezone");
    const auto settings=directory_/"calendar.json";
    std::ifstream in(settings);Json cfg;
    if(in){in>>cfg;zone_key_=cfg.at("windows_timezone").get<std::string>();bool found=false;
        for(DWORD i=0;;++i){DYNAMIC_TIME_ZONE_INFORMATION candidate{};const auto rc=EnumDynamicTimeZoneInformation(i,&candidate);if(rc==ERROR_NO_MORE_ITEMS)break;if(rc!=ERROR_SUCCESS)throw std::runtime_error("Cannot enumerate calendar timezone");
            const auto key=std::filesystem::path(candidate.TimeZoneKeyName).u8string();if(std::string(key.begin(),key.end())==zone_key_){zone_=candidate;found=true;break;}}
        if(!found)throw std::runtime_error("Saved monthly calendar timezone is unavailable; archive preserved");
    }else{const auto key=std::filesystem::path(zone_.TimeZoneKeyName).u8string();zone_key_={key.begin(),key.end()};if(zone_key_.empty())throw std::runtime_error("No named Windows timezone for monthly archive");atomic_json(settings,{{"format_version",1},{"windows_timezone",zone_key_}});}
}
std::string MonthlyTrafficStore::month_at(std::int64_t minute) const {
    if(minute<0||minute>253402300799LL/60)throw std::invalid_argument("Invalid archive UTC minute");
    ULARGE_INTEGER time{};time.QuadPart=(static_cast<ULONGLONG>(minute)*60+11644473600ULL)*10000000ULL;
    FILETIME ft{time.LowPart,time.HighPart};SYSTEMTIME utc{},local{};
    if(!FileTimeToSystemTime(&ft,&utc)||!SystemTimeToTzSpecificLocalTimeEx(&zone_,&utc,&local))throw std::runtime_error("Cannot convert monthly calendar boundary");
    std::ostringstream key;key<<std::setfill('0')<<std::setw(4)<<local.wYear<<'-'<<std::setw(2)<<local.wMonth;return key.str();
}
void MonthlyTrafficStore::save(const std::string &session,const Json &rows){
    std::map<std::string,Json> batches;
    for(const auto &r:rows){const auto module=r.value("module","");const auto metric=r.value("metric","");
        if(!(r.contains("ip_record")||(module=="network"&&(metric=="interface_upload_bytes"||metric=="interface_download_bytes"||metric.starts_with("bytes/")))))continue;
        const auto key=month_at(r.at("minute").get<std::int64_t>());if(!batches.contains(key))batches[key]=Json::array();batches[key].push_back(r);
    }
    for(const auto &[key,batch]:batches){{DatabaseManager db(directory_/(key+".sqlite3"));db.save(session,batch);}publish(key);}
}
void MonthlyTrafficStore::recover(const DatabaseManager &source){source.visit_traffic([this](const auto &session,const auto &rows){save(session,rows);});
    for(const auto &entry:std::filesystem::directory_iterator(directory_))if(entry.path().extension()==".sqlite3"&&valid_month(entry.path().stem().string()))publish(entry.path().stem().string());
}
void MonthlyTrafficStore::publish(const std::string &month) const {
    DatabaseManager db(directory_/(month+".sqlite3"));auto value=db.traffic_summary();value["month"]=month;value["windows_timezone"]=zone_key_;value["format_version"]=1;
    value["updated_ms"]=utc_now_ms();value["database_file"]=utf8(directory_/(month+".sqlite3"));value["summary_file"]=utf8(directory_/(month+".json"));
    value["scope"]="Monitoring-enabled intervals on the selected interface; totals are interface octets, family values are incomplete/unverified IP observations; not campus billing.";
    atomic_json(directory_/(month+".json"),value);
}
Json MonthlyTrafficStore::query(const std::string &requested) const {
    const auto month=requested.empty()?month_at(utc_now_ms()/60000):requested;
    if(month!="all"&&!valid_month(month))throw std::invalid_argument("Invalid calendar month");
    std::set<std::string> months;
    for(const auto &entry:std::filesystem::directory_iterator(directory_))if(entry.path().extension()==".sqlite3"&&valid_month(entry.path().stem().string()))months.insert(entry.path().stem().string());
    Json result={{"month",month},{"months",months},{"directory",utf8(directory_)},{"windows_timezone",zone_key_},{"persisted_only",true}};
    Json totals={{"upload_bytes",0},{"download_bytes",0},{"ipv4_up",0},{"ipv4_down",0},{"ipv6_up",0},{"ipv6_down",0},{"accepted",0},{"rejected",0},{"unsupported",0},{"events_lost",0},{"buffers_lost",0},{"has_totals",false},{"has_ip",false},{"quality","no_data"},{"reference_validated",false}};
    for(const auto &key:months){if(month!="all"&&key!=month)continue;
        if(!std::filesystem::exists(directory_/(key+".json")))publish(key);
        std::ifstream summary(directory_/(key+".json"));Json t;summary>>t;
        for(const char *k:{"upload_bytes","download_bytes","ipv4_up","ipv4_down","ipv6_up","ipv6_down","accepted","rejected","unsupported"})totals[k]=totals[k].get<std::int64_t>()+t[k].get<std::int64_t>();
        for(const char *k:{"events_lost","buffers_lost"})totals[k]=std::max(totals[k].get<std::int64_t>(),t[k].get<std::int64_t>());
        totals["has_totals"]=totals["has_totals"].get<bool>()||t["has_totals"].get<bool>();totals["has_ip"]=totals["has_ip"].get<bool>()||t["has_ip"].get<bool>();
        if(t["quality"]=="incomplete_observation")totals["quality"]="incomplete_observation";
    }
    if(totals["quality"]!="incomplete_observation"&&totals["has_ip"].get<bool>())totals["quality"]="observed_unverified_reference";
    result["totals"]=totals;result["database_file"]=month=="all"?utf8(directory_):utf8(directory_/(month+".sqlite3"));result["summary_file"]=month=="all"?utf8(directory_):utf8(directory_/(month+".json"));return result;
}
Json MonthlyTrafficStore::daily(const std::string &requested) const {
    const auto month=requested.empty()?month_at(utc_now_ms()/60000):requested;
    if(!valid_month(month))throw std::invalid_argument("Invalid calendar month");
    const int year=std::stoi(month.substr(0,4)),mon=std::stoi(month.substr(5));
    const int lengths[]={31,28,31,30,31,30,31,31,30,31,30,31};
    const int count=lengths[mon-1]+(mon==2&&year%4==0&&(year%100!=0||year%400==0));
    const auto boundary=[&](int day){SYSTEMTIME local{},utc{};local.wYear=year;local.wMonth=mon;local.wDay=day;
        if(day>count){local.wDay=1;if(++local.wMonth>12){local.wMonth=1;++local.wYear;}}
        FILETIME ft{};if(!TzSpecificLocalTimeToSystemTimeEx(&zone_,&local,&utc)||!SystemTimeToFileTime(&utc,&ft))throw std::runtime_error("Cannot convert daily archive boundary");
        ULARGE_INTEGER n{};n.LowPart=ft.dwLowDateTime;n.HighPart=ft.dwHighDateTime;return static_cast<std::int64_t>(n.QuadPart/10000000ULL-11644473600ULL)/60;};
    const auto file=directory_/(month+".sqlite3");std::unique_ptr<DatabaseManager> db;
    if(std::filesystem::exists(file))db=std::make_unique<DatabaseManager>(file);
    Json days=Json::array();
    for(int day=1;day<=count;day++){
        const auto from=boundary(day),to=boundary(day+1);Json data=db?db->traffic_summary(from,to):Json{{"has_totals",false},{"has_ip",false},{"quality","no_data"}};
        std::ostringstream date;date<<month<<'-'<<std::setfill('0')<<std::setw(2)<<day;
        data["date"]=date.str();data["from"]=from;data["to"]=to;data["expected_minutes"]=to-from;
        // Different packet-length bases cannot become a comparable family total.
        if(data.value("basis_count",0)>1){data["has_ip"]=false;data["quality"]="incomparable_bases";}
        days.push_back(std::move(data));
    }
    return {{"month",month},{"days",days},{"windows_timezone",zone_key_},{"persisted_only",true},{"sources_additive",false},{"reference_validated",false}};
}
void MonthlyTrafficStore::clear(){
    for(const auto &entry:std::filesystem::directory_iterator(directory_)){const auto p=entry.path();auto name=p.filename().string();
        if(name.size()>=7&&valid_month(name.substr(0,7))&&(name.substr(7)==".sqlite3"||name.substr(7)==".sqlite3-wal"||name.substr(7)==".sqlite3-shm"||name.substr(7)==".json"||name.substr(7)==".json.tmp"))std::filesystem::remove(p);
    }
}
} // namespace pulsetrace
