#include "pulsetrace/monthly_traffic.hpp"
#include "pulsetrace/probe_report.hpp"
#include "pulsetrace/protocol.hpp"
#include <fstream>
#include <iostream>
using namespace pulsetrace;
namespace {
int checks=0;
void check(bool ok,const char *message){++checks;if(!ok)throw std::runtime_error(message);}
std::int64_t local_minute(int y,int m,int d,int h=0,int min=0){DYNAMIC_TIME_ZONE_INFORMATION zone{};GetDynamicTimeZoneInformation(&zone);SYSTEMTIME local{},utc{};local.wYear=y;local.wMonth=m;local.wDay=d;local.wHour=h;local.wMinute=min;
    if(!TzSpecificLocalTimeToSystemTimeEx(&zone,&local,&utc))throw std::runtime_error("Test calendar conversion failed");FILETIME ft{};SystemTimeToFileTime(&utc,&ft);ULARGE_INTEGER n{};n.LowPart=ft.dwLowDateTime;n.HighPart=ft.dwHighDateTime;return static_cast<std::int64_t>(n.QuadPart/10000000ULL-11644473600ULL)/60;}
Json network(std::int64_t m,std::string metric,int value){return {{"minute",m},{"module","network"},{"metric",metric},{"value",value}};}
Json ip(std::int64_t m){Json r={{"minute",m},{"interface_id","19"},{"source","test_fixture"},{"basis","ip_lengths"},{"quality","incomplete_observation"},{"ipv4_up",44},{"ipv4_down",88},{"ipv6_up",64},{"ipv6_down",128},{"accepted",5},{"rejected",2},{"unsupported",1},{"events_lost",1},{"buffers_lost",0}};
    return {{"minute",m},{"module","network_ip"},{"metric","19/quality_state"},{"value",2},{"ip_record",r}};}
}
int main(){try{
 const auto dir=executable_data_directory()/"tests"/("monthly-"+std::to_string(utc_now_ms()));std::filesystem::create_directories(dir);
 MonthlyTrafficStore store(dir/"months");const auto nov=local_minute(2026,11,30,23,59),dec=local_minute(2026,12,1);
 check(store.month_at(nov)=="2026-11"&&store.month_at(dec)=="2026-12","natural month local boundary");
 check(store.month_at(local_minute(2024,2,29,23,59))=="2024-02"&&store.month_at(local_minute(2024,3,1))=="2024-03","leap-year month boundary");
 check(store.month_at(local_minute(2027,1,1))=="2027-01","year rollover");
 check(!store.query("2026-11")["totals"]["has_totals"].get<bool>()&&!std::filesystem::exists(dir/"months/2026-11.sqlite3"),"empty query creates no fake month data");
 Json rows=Json::array({network(nov,"interface_upload_bytes",100),network(nov,"interface_download_bytes",200),network(nov,"bytes/19/upload",100),network(dec,"interface_upload_bytes",300),ip(nov),{{"minute",nov},{"module","keyboard"},{"metric","30"},{"value",5}}});
 store.save("a",rows);store.save("a",rows);auto t=store.query("2026-11")["totals"];
 check(t["upload_bytes"]==100&&t["download_bytes"]==200,"idempotent retry; per-interface bytes not double-counted");
 check(t["ipv4_up"]==44&&t["ipv4_down"]==88&&t["ipv6_up"]==64&&t["ipv6_down"]==128,"four protocol directions archived once");
 check(t["quality"]=="incomplete_observation"&&t["rejected"]==2&&!t["reference_validated"].get<bool>(),"quality not lost");
 check(store.query("2026-12")["totals"]["upload_bytes"]==300,"new month isolated");
 store.save("a",Json::array({network(nov,"interface_upload_bytes",150)}));store.save("b",Json::array({network(nov,"interface_upload_bytes",20)}));
 check(store.query("2026-11")["totals"]["upload_bytes"]==170,"absolute counter update and multiple sessions");
 check(store.query("all")["totals"]["upload_bytes"]==470,"all months cumulative total");
 DatabaseManager source(dir/"source.db");source.save("a",Json::array({network(nov,"interface_upload_bytes",150),ip(nov)}));
 store.recover(source);check(store.query("all")["totals"]["upload_bytes"]==470,"startup recovery idempotent");
 source.prune(dec+1);check(source.query(nov,dec+1,60).empty()&&store.query("2026-11")["totals"]["upload_bytes"]==170,"detail retention cannot remove monthly collection");
 source.save("recovery",Json::array({network(dec,"interface_download_bytes",400),ip(dec)}));
 {MonthlyTrafficStore reopened(dir/"months");reopened.recover(source);check(reopened.query("2026-12")["totals"]["download_bytes"]==400,"main DB committed before archive: restart recovers");}
 std::ifstream in(dir/"months/2026-11.json");Json json;in>>json;in.close();check(json["upload_bytes"]==170&&json["quality"]=="incomplete_observation"&&json["windows_timezone"].is_string(),"readable JSON agrees with committed SQLite");
 {DatabaseManager archive(dir/"months/2026-11.sqlite3");auto all=archive.query(nov,nov+1,1);check(std::none_of(all.begin(),all.end(),[](auto &r){return r["module"]=="keyboard";}),"monthly archive contains no keyboard data");}
 bool invalid=false;try{store.query("../bad");}catch(...){invalid=true;}check(invalid,"path traversal rejected");
 invalid=false;try{parse_message(R"({"type":"get_monthly_traffic","request_id":1,"params":{"month":"2026-13"}})");}catch(...){invalid=true;}check(invalid,"bridge rejects malformed month");
 check(parse_message(R"({"type":"get_monthly_traffic","request_id":1,"params":{"month":"all"}})")["type"]=="get_monthly_traffic","all-month bridge validated");
 std::ofstream(dir/"months/keep.txt")<<"not an archive";store.clear();check(store.query("all")["months"].empty()&&std::filesystem::exists(dir/"months/keep.txt")&&std::filesystem::exists(dir/"months/calendar.json"),"explicit clear removes only own month files; calendar retained");
 std::cout<<checks<<" monthly archive checks passed\n";return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
