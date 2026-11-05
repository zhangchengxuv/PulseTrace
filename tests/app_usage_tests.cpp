#include "pulsetrace/app_usage.hpp"
#include "pulsetrace/database.hpp"
#include "pulsetrace/core_service.hpp"
#include <windows.h>
#include <sqlite3.h>
#include <filesystem>
#include <iostream>
#include <future>
using namespace pulsetrace;
int checks=0;
void check(bool b,const char *message){++checks;if(!b)throw std::runtime_error(message);}
std::uint64_t total(const UsageTimeline &t,const char *field){std::uint64_t n=0;for(const auto &r:t.rows()){check(r["active_ms"]<=r["foreground_ms"],"active <= foreground");n+=r[field].get<std::uint64_t>();}return n;}
Json rpc(CoreService &core,const char *type,Json params=Json::object()){
 auto promise=std::make_shared<std::promise<Json>>();auto future=promise->get_future();core.request({{"type",type},{"request_id",1},{"params",params}},[promise](Json r){promise->set_value(std::move(r));});
 if(future.wait_for(std::chrono::seconds(8))!=std::future_status::ready)throw std::runtime_error("Usage core RPC timeout");auto result=future.get();if(!result.value("ok",false))throw std::runtime_error(result["error"]["message"].get<std::string>());return result["data"];
}
int main(){try{
 UsageTimeline t;UsageObservation s{1000,1000,59900,"a","App A","C:\\a.exe",true};t.observe(s,120);
 s.tick+=1000;s.utc_ms+=1000;t.observe(s,120);check(t.rows().size()==2&&total(t,"active_ms")==1000,"minute boundary split");
 s.identity="b";s.name="B";s.tick+=1000;s.utc_ms+=1000;t.observe(s,120);s.tick+=1000;s.utc_ms+=1000;t.observe(s,120);
 check(t.rows().back()["key"]=="b"&&total(t,"foreground_ms")==3000,"foreground switch settles previous app only");
 t.reset();auto before=total(t,"foreground_ms");s.tick+=500000;s.utc_ms+=500000;t.observe(s,120);check(total(t,"foreground_ms")==before,"pause baseline excludes background elapsed");
 t.clear();s={100000,0,100000,"a","A","",true};t.observe(s,120);for(int i=0;i<30;i++){s.tick+=1000;s.utc_ms+=1000;t.observe(s,120);}
 check(total(t,"foreground_ms")==30000&&total(t,"active_ms")==20000,"idle threshold clips activity at exact deadline");
 s.last_input=s.tick+500;s.tick+=1000;s.utc_ms+=1000;t.observe(s,120);check(total(t,"active_ms")==20500,"input resumes midway through observation");
 s.valid=false;s.tick+=1000;s.utc_ms+=1000;t.observe(s,120);before=total(t,"foreground_ms");s.tick+=1000;s.utc_ms+=1000;t.observe(s,120);check(total(t,"foreground_ms")==before,"locked/excluded/invalid session stops counting");
 s.valid=true;s.tick+=1000;s.utc_ms+=1000;t.observe(s,120);before=total(t,"foreground_ms");s.tick+=3600000;s.utc_ms+=3600000;t.observe(s,120);check(total(t,"foreground_ms")==before&&t.gaps()>0,"sleep gap not attributed");
 s.tick+=1000;s.utc_ms-=3600000;t.observe(s,120);check(total(t,"foreground_ms")==before,"wall-clock change not counted");
 t.clear();s={1000,1000,86399900,"same-exe","A","",true};t.observe(s,120);s.tick+=1000;s.utc_ms+=1000;t.observe(s,120);check(t.rows().size()==2&&t.rows()[0]["minute"]==1439&&t.rows()[1]["minute"]==1440,"day boundary exact");
 const auto directory=std::filesystem::absolute("data/tests/usage-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64()));std::filesystem::create_directories(directory);const auto file=directory/"monitor.db";
 {DatabaseManager db(file);db.save_usage("session",t.rows());db.save_usage("session",t.rows());check(db.usage(0,2000).size()==2,"idempotent cumulative persistence");check(db.usage(0,2000,"session",1440).size()==1,"live/history exclusion");db.backup(directory/"copy.db");auto bad=t.rows();bad[0]["active_ms"]=90000;bool rejected=false;try{db.save_usage("bad",bad);}catch(...){rejected=true;}check(rejected&&db.usage(0,2000).size()==2,"invalid duration rolls back");}
 {DatabaseManager db(file);check(db.usage(0,2000)[1]["foreground_ms"]==900,"restart retains durations");db.prune_usage(1440);check(db.usage(0,2000).size()==1,"usage retention");db.clear();check(db.usage(0,2000).empty(),"clear removes usage");}
 // Recreate a genuine schema-v4 database, keeping existing unrelated history.
 {sqlite3 *db=nullptr;auto u=file.u8string();sqlite3_open(reinterpret_cast<const char*>(u.c_str()),&db);sqlite3_exec(db,"DROP TABLE app_usage;INSERT INTO counts VALUES('old',1,'keyboard','30',42);PRAGMA user_version=4;",nullptr,nullptr,nullptr);sqlite3_close(db);}
 {DatabaseManager db(file);check(db.migration_info()["from"]==4&&db.migration_info()["to"]==5,"schema v4 to v5 migration");check(db.query(0,2,1)[0]["value"]==42&&db.usage(0,2).empty(),"migration preserves old data");check(!db.migration_info()["backup"].get<std::string>().empty(),"migration online backup");}
 {AppUsageMonitor monitor;monitor.configure({{"usage_enabled",true}},false);Sleep(1200);check(monitor.rows().empty(),"native default pause");check(monitor.status()["event_ready"].get<bool>(),"real WinEvent and WTS registration");monitor.stop();}
 auto core_dir=directory/"core";std::filesystem::create_directories(core_dir);const auto minute=utc_now_ms()/60000;
 {DatabaseManager db(core_dir/"monitor.db");db.save_usage("fixture",Json::array({{{"minute",minute},{"key","test.exe"},{"name","Test"},{"path",""},{"foreground_ms",1000},{"active_ms",500}}}));}
 {CoreService core(core_dir);core.start();const Json range={{"from",minute},{"to",minute+1}};auto result=rpc(core,"get_app_usage",range);check(!rpc(core,"get_status")["monitoring"].get<bool>()&&result["rows"].size()==1,"native bridge/default pause/history");
  rpc(core,"set_config",{{"usage_idle_seconds",90},{"usage_exclusions",Json::array({"test.exe"})},{"usage_icons",false}});
  const auto exported=rpc(core,"export_app_usage",range);check(std::filesystem::exists(core_dir/exported["file"].get<std::string>()),"usage CSV export");
  rpc(core,"set_monitoring",{{"enabled",true}});rpc(core,"set_monitoring",{{"enabled",false}});check(rpc(core,"get_app_usage",range)["rows"].size()==1,"pause/resume preserves history");core.stop();}
 {CoreService core(core_dir);check(core.status()["config"]["usage_idle_seconds"]==90&&!core.status()["config"]["usage_icons"].get<bool>(),"usage preferences persist");core.start();rpc(core,"clear_data",{{"confirmation","DELETE"}});core.stop();DatabaseManager db(core_dir/"monitor.db");check(db.usage(0,minute+1).empty(),"clear and final flush do not resurrect usage");}
 std::cout<<checks<<" app usage checks passed\n";return 0;
 }catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
