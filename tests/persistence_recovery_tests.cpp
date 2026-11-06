#include "pulsetrace/core_service.hpp"
#include "pulsetrace/pending_store.hpp"
#include "pulsetrace/probe_report.hpp"
#include "pulsetrace/protocol.hpp"
#include "sqlite_fault_vfs.hpp"
#include <wtsapi32.h>
#include <fstream>
#include <future>
#include <iostream>
using namespace pulsetrace;
namespace {
int checks{};
void check(bool ok,const char *message){++checks;if(!ok)throw std::runtime_error(message);}
Json rpc(CoreService &core,const char *type,Json params=Json::object()){
 auto p=std::make_shared<std::promise<Json>>();auto f=p->get_future();
 core.request({{"type",type},{"request_id",1},{"params",params}},[p](Json r){p->set_value(std::move(r));});
 if(f.wait_for(std::chrono::seconds(15))!=std::future_status::ready)throw std::runtime_error("RPC timeout");
 auto r=f.get();if(!r.value("ok",false))throw std::runtime_error(r["error"]["message"]);return r["data"];
}
template<class F>void wait_for(F condition,int seconds=15){
 const auto end=GetTickCount64()+seconds*1000;
 while(!condition()){if(GetTickCount64()>end)throw std::runtime_error("Recovery condition timed out");Sleep(50);}
}
Json batch(std::int64_t revision,int count){
 const auto minute=utc_now_ms()/60000;
 Json ip={{"minute",minute},{"interface_id","fixture"},{"source","fixture"},{"basis","ip_lengths"},{"quality","observed_unverified_reference"},
          {"ipv4_up",count*10},{"ipv4_down",count*20},{"ipv6_up",count*30},{"ipv6_down",count*40},{"accepted",count},
          {"rejected",0},{"unsupported",0},{"events_lost",0},{"buffers_lost",0}};
 return {{"format",1},{"session","123-456"},{"revision",revision},{"created_ms",utc_now_ms()},
  {"rows",Json::array({{{"minute",minute},{"module","keyboard"},{"metric","30"},{"value",count}},
                      {{"minute",minute},{"module","mouse"},{"metric","left"},{"value",count*2}},
                      {{"minute",minute},{"module","network"},{"metric","interface_upload_bytes"},{"value",count*10}},
                      {{"minute",minute},{"module","network_ip"},{"metric","fixture/ipv4_up"},{"value",count*10},{"ip_record",ip}}})},
  {"usage",Json::array({{{"minute",minute},{"key","fixture-app"},{"name","Fixture"},{"path",""},{"foreground_ms",count*100},{"active_ms",count*50}}})},
  {"rankings",Json::array({{{"minute",minute},{"source","fixture"},{"kind","app"},{"key","fixture-app"},{"family",4},{"name","Fixture"},{"path",""},{"attribution","test"},{"upload",count*10},{"download",count*20},{"quality",0}}})},
  {"events",Json::array()}};
}
void verify(DatabaseManager &db,int count){
 const auto m=utc_now_ms()/60000;auto rows=db.query(m-2,m+2,1);
 const auto value=[&](const char *module,const char *metric){std::int64_t n=0;for(const auto &r:rows)if(r["module"]==module&&r["metric"]==metric)n+=r["value"].get<std::int64_t>();return n;};
 check(value("keyboard","30")==count,"keyboard snapshots never regress or duplicate");
 check(value("mouse","left")==count*2,"mouse snapshots never regress or duplicate");
 check(db.protocol_samples(m-2,m+2,1)[0]["ipv6_down"]==count*40,"protocol snapshots survive retries");
 check(db.usage(m-2,m+2)[0]["active_ms"]==count*50,"usage snapshots survive retries");
 check(db.rankings(m-2,m+2,"fixture","app")[0]["upload"]==count*10,"ranking snapshots survive retries");
}
void fixtures(const std::filesystem::path &root){
 DatabaseManager db(root/"atomic.db");db.save_batch(batch(1,7));verify(db,7);
 auto invalid=batch(2,20);invalid["usage"][0]["active_ms"]=999999;
 bool rejected=false;try{db.save_batch(invalid);}catch(...){rejected=true;}
 check(rejected,"invalid usage is rejected");verify(db,7);
 check(db.save_batch(batch(2,9)),"valid retry commits all modules");verify(db,9);
 check(!db.save_batch(batch(1,7)),"stale revision is ignored even if its journal survived acknowledgment");verify(db,9);
 PendingStore journal(root/"journal");journal.stage(batch(1,7));
 check(journal.read(journal.files()[0])["revision"]==1,"atomic journal reads its aggregate identity");
 journal.stage(batch(2,9));check(journal.files().size()==1&&journal.read(journal.files()[0])["revision"]==2,"latest atomic pending snapshot replaces only its own session");
 db.clear({"123-456"});check(!db.save_batch(journal.read(journal.files()[0])),"clear tombstone prevents old pending-data resurrection");
 check(db.query(0,utc_now_ms()/60000+1,1).empty(),"cleared data stays empty");
}
void storage_faults(const std::filesystem::path &root){
 sqlite_fault::Vfs fault;
 for(const int code:{SQLITE_READONLY,SQLITE_FULL,SQLITE_IOERR_WRITE}){
  const auto file=root/("fault-"+std::to_string(code)+".db");PendingStore journal(root/("pending-"+std::to_string(code)));
  {DatabaseManager db(file);db.save_batch(batch(1,7));journal.stage(batch(2,9));sqlite_fault::write_error=code;
   bool failed=false;try{db.save_batch(batch(2,9));}catch(const SQLiteError &e){failed=true;check(e.details()["code"]==(code&255),"primary SQLite error retained");
    check(e.details()["extended_code"].get<int>()!=0&&!e.details()["phase"].get<std::string>().empty()&&!e.details()["path"].get<std::string>().empty(),"diagnostic has path, extended error and transaction phase");
   }sqlite_fault::write_error=0;check(failed,"controlled read-only/full/I/O write fault reproduced");
  }
  {DatabaseManager db(file);verify(db,7);check(db.save_batch(journal.read(journal.files()[0])),"recovery commits retained journal after disk fault clears");verify(db,9);journal.acknowledge("123-456");}
 }
}
void child_crash(const std::filesystem::path &root){
 std::filesystem::create_directories(root);DatabaseManager db(root/"monitor.db");db.save_batch(batch(1,7));
 PendingStore(root/"pending").stage(batch(2,9));
 ExitProcess(0); // Deliberate termination bypassing C++/SQLite cleanup.
}
void crash_recovery(const std::filesystem::path &root){
 const auto exe=executable_data_directory().parent_path()/"persistence_recovery_tests.exe";
 auto command=quote_windows_argument(exe.wstring())+L" --crash-writer "+quote_windows_argument(root.wstring());
 STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION process{};
 check(CreateProcessW(exe.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process)!=FALSE,"crash child launched");
 const auto wait=WaitForSingleObject(process.hProcess,15000);DWORD exit=1;GetExitCodeProcess(process.hProcess,&exit);CloseHandle(process.hThread);CloseHandle(process.hProcess);
 check(wait==WAIT_OBJECT_0&&exit==0,"crash child terminated without SQLite cleanup");
 {DatabaseManager db(root/"monitor.db");verify(db,7);}
 for(int restart=0;restart<2;restart++){
  CoreService core(root);core.start();wait_for([&]{return core.status().value("last_flush_ms",0LL)>0;});
  check(!core.status()["monitoring"].get<bool>(),"restart respects default paused policy");core.stop();
  DatabaseManager db(root/"monitor.db");verify(db,9);
  if(restart==0)PendingStore(root/"pending").stage(batch(1,7));
 }
}
void degraded_startup(const std::filesystem::path &root){
 std::filesystem::create_directories(root/"monitor.db");
 std::ofstream(root/"config.json")<<"{\"capture_on_launch\":true,\"usage_enabled\":false}";
 {
  CoreService core(root);core.start();wait_for([&]{return !core.status().value("storage_error","").empty();});
  check(core.status()["monitoring"]==true,"startup storage failure does not overwrite requested monitoring");
  check(std::filesystem::remove(root/"monitor.db"),"remove only isolated empty I/O failure directory");
  rpc(core,"retry_persistence");wait_for([&]{return core.status().value("last_flush_ms",0LL)>0;});
  check(core.status().value("storage_error","").empty(),"startup database can recover without restarting the process");
  core.stop();
 }
 const auto secondary=root/"archive-unavailable";std::filesystem::create_directories(secondary);
 std::ofstream(secondary/"network-months")<<"controlled blocked archive path";
 {
  CoreService core(secondary);core.start();wait_for([&]{return !core.status().value("archive_error","").empty();});
  check(core.status().value("storage_error","").empty(),"archive failure is distinct from primary SQLite failure");
  check(rpc(core,"get_statistics",{{"from",0},{"to",utc_now_ms()/60000+1},{"bucket",1440}})["rows"].is_array(),"primary history remains queryable when archive is unavailable");
  check(std::filesystem::remove(secondary/"network-months"),"remove only isolated archive failure file");
  rpc(core,"retry_persistence");wait_for([&]{return core.status().value("archive_error","").empty();});
  check(std::filesystem::is_directory(secondary/"network-months"),"archive recovers in the same process");core.stop();
 }
 const auto damaged=root/"pending-damaged";std::filesystem::create_directories(damaged/"pending");
 std::ofstream(damaged/"pending"/"123-456.pending")<<"{";
 {
  CoreService core(damaged);core.start();wait_for([&]{return core.status().value("last_flush_ms",0LL)>0;});
  check(!core.status().value("recovery_error","").empty(),"invalid pending data remains diagnosed");
  check(std::filesystem::exists(damaged/"pending"/"123-456.pending"),"invalid pending file is preserved");
  PendingStore(damaged/"pending").stage(batch(2,9));rpc(core,"retry_persistence");
  wait_for([&]{return core.status().value("recovery_error","").empty()&&!std::filesystem::exists(damaged/"pending"/"123-456.pending");});
  core.stop();DatabaseManager db(damaged/"monitor.db");verify(db,9);
 }
 const auto no_journal=root/"journal-unavailable";std::filesystem::create_directories(no_journal);
 std::ofstream(no_journal/"pending")<<"controlled blocked journal path";
 {
  CoreService core(no_journal);core.start();wait_for([&]{return core.status().value("last_flush_ms",0LL)>0;});
  check(core.status().value("storage_error","").empty()&&!core.status().value("journal_error","").empty(),"journal outage does not prevent healthy primary writes");
  check(std::filesystem::remove(no_journal/"pending"),"remove only isolated journal failure file");
  rpc(core,"retry_persistence");wait_for([&]{return core.status().value("journal_error","").empty();});core.stop();
 }
}
void migration_v5(const std::filesystem::path &root){
 std::filesystem::create_directories(root);const auto file=root/"monitor.db";
 {DatabaseManager db(file);db.save_batch(batch(1,7));}
 sqlite3 *old{};sqlite3_open(file.string().c_str(),&old);
 check(sqlite3_exec(old,"DROP TABLE campus_snapshots;DROP TABLE persistence_receipts;PRAGMA user_version=5",nullptr,nullptr,nullptr)==SQLITE_OK,"controlled genuine v5 fixture created");sqlite3_close(old);
 {DatabaseManager upgraded(file);check(upgraded.migration_info()["from"]==5&&upgraded.migration_info()["to"]==DatabaseManager::supported_schema,"v5 migration adds replay and campus tables transactionally");verify(upgraded,7);
  const auto backup=upgraded.migration_info()["backup"].get<std::string>();sqlite3 *before{};sqlite3_open_v2(backup.c_str(),&before,SQLITE_OPEN_READONLY,nullptr);
  sqlite3_stmt *query{};sqlite3_prepare_v2(before,"PRAGMA user_version",-1,&query,nullptr);sqlite3_step(query);
  check(sqlite3_column_int(query,0)==5,"consistent pre-v6 backup retains original v5 schema");sqlite3_finalize(query);sqlite3_close(before);
 }
}
void directory_lock(const std::filesystem::path &root){
 std::filesystem::create_directories(root);const auto handle=CreateFileW((root/"data.lock").c_str(),GENERIC_WRITE,0,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
 check(handle!=INVALID_HANDLE_VALUE,"test acquires its own data-directory lock");
 const auto exe=executable_data_directory().parent_path()/"PulseTrace.exe";auto command=quote_windows_argument(exe.wstring())+L" --self-test --data-dir "+quote_windows_argument(root.wstring());
 STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION process{};
 const bool launched=CreateProcessW(exe.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process)!=FALSE;
 if(!launched){CloseHandle(handle);throw std::runtime_error("Cannot launch directory-lock probe");}
 const auto wait=WaitForSingleObject(process.hProcess,15000);DWORD code=0;GetExitCodeProcess(process.hProcess,&code);CloseHandle(process.hThread);CloseHandle(process.hProcess);CloseHandle(handle);
 check(wait==WAIT_OBJECT_0&&code!=0,"a second writer is rejected before SQLite opens");
 check(!std::filesystem::exists(root/"monitor.db"),"locked startup never creates or replaces a database");
}
void core_recovery(const std::filesystem::path &root){
 std::filesystem::create_directories(root);CoreService core(root);core.start();
 wait_for([&]{return core.status().value("last_flush_ms",0LL)>0&&core.status().value("input_ready",false);});
 rpc(core,"set_config",{{"flush_seconds",5},{"usage_enabled",false}});rpc(core,"set_monitoring",{{"enabled",true}});
 HWND input=nullptr;EnumWindows([](HWND w,LPARAM p)->BOOL{DWORD id{};GetWindowThreadProcessId(w,&id);wchar_t cls[128]{};GetClassNameW(w,cls,128);if(id==GetCurrentProcessId()&&std::wstring(cls)==L"PulseTrace.MouseProbe.RawInput"){*reinterpret_cast<HWND*>(p)=w;return FALSE;}return TRUE;},reinterpret_cast<LPARAM>(&input));check(input!=nullptr,"independent Raw Input message window exists");
 DWORD pid{};GetWindowThreadProcessId(input,&pid);check(pid==GetCurrentProcessId(),"session tests target only this test process");
 const auto identity=core.status()["session"];
 SendMessageW(input,WM_WTSSESSION_CHANGE,WTS_SESSION_LOCK,0);
 check(core.status()["session_locked"]==true&&core.status()["monitoring"]==true&&!core.capture_enabled(),"lock pauses input without losing requested monitoring");
 SendMessageW(input,WM_WTSSESSION_CHANGE,WTS_SESSION_UNLOCK,0);
 check(core.capture_enabled()&&core.status()["session"]==identity,"unlock restores the same collector and session");
 SendMessageW(input,WM_POWERBROADCAST,PBT_APMSUSPEND,0);
 check(core.status()["suspended"]==true&&!core.capture_enabled(),"suspend pauses input");
 SendMessageW(input,WM_POWERBROADCAST,PBT_APMRESUMEAUTOMATIC,0);
 check(core.capture_enabled()&&core.status()["session"]==identity,"automatic resume preserves aggregation identity");
 RAWINPUTDEVICE devices[32]{};UINT n=32;auto count=GetRegisteredRawInputDevices(devices,&n,sizeof(RAWINPUTDEVICE));int own=0;
 for(UINT i=0;i<count&&count!=UINT(-1);i++)if(devices[i].hwndTarget==input&&(devices[i].usUsage==2||devices[i].usUsage==6))own++;
 check(own==2,"resume has exactly one mouse and keyboard registration");
 check(core.flush_now(),"pre-conflict flush succeeds");Sleep(1100);
 sqlite3 *blocker{};sqlite3_open((root/"monitor.db").string().c_str(),&blocker);
 check(sqlite3_exec(blocker,"BEGIN IMMEDIATE",nullptr,nullptr,nullptr)==SQLITE_OK,"controlled external writer lock acquired");
 core.on_flush_requested();wait_for([&]{return !core.status().value("storage_error","").empty();});
 const auto failed=core.status();const bool running=failed["monitoring"];const bool pending=failed.value("pending_retry_rows",0)>0;
 const bool prepared=core.prepare_stop();core.cancel_stop();
 sqlite3_exec(blocker,"ROLLBACK",nullptr,nullptr,nullptr);sqlite3_close(blocker);
 check(running&&pending,"transient conflict retains pending data and requested monitoring");
 check(!prepared&&core.capture_enabled(),"cancelled failed exit retains the working collector");
 check(failed["storage_diagnostic"]["code"]==SQLITE_BUSY,"raw busy code is available in core status");
 wait_for([&]{return core.status().value("storage_error","").empty();},20);
 check(core.status().value("pending_retry_rows",-1)==0&&core.status()["monitoring"]==true,"automatic recovery saves pending data without disabling capture");
 check(core.prepare_stop(),"healthy exit preparation succeeds");core.stop();
 check(core.status().value("storage_error","").empty(),"normal stop completes without a false save error");
}
void input_handoff(const std::filesystem::path &root){
 std::filesystem::create_directories(root);CoreService core(root);MouseMonitor first,empty;
 const auto minute=utc_now_ms()/60000-3;
 first.consume({1,10,minute,down_flags[0],0,Source::DeviceReported});
 first.consume({1,11,minute,up_flags[0],0,Source::DeviceReported});
 first.consume({1,12,minute,horizontal_wheel_flag,120,Source::DeviceReported});core.on_tick(first);
 first.consume({1,13,minute,horizontal_wheel_flag,-120,Source::DeviceReported});core.on_tick(first);
 // Neither snapshot has been handed to storage yet; the source cache then expires.
 core.on_tick(empty);core.start();wait_for([&]{return core.status().value("last_flush_ms",0LL)>0;});core.stop();
 const auto expected=(root/"monitor.db").u8string();check(core.status()["database_path"]==std::string(expected.begin(),expected.end()),"diagnostic database paths retain Unicode without breaking persistence");
 DatabaseManager db(root/"monitor.db");const auto rows=db.query(minute,minute+1,1);bool click=false,zero=false;
 for(const auto &r:rows){if(r["module"]=="mouse"&&r["metric"]=="left"&&r["value"]==1)click=true;if(r["metric"]=="horizontal_delta"&&r["value"]==0)zero=true;}
 check(click,"new snapshots cannot discard input not yet handed to persistence");
 check(zero,"horizontal scrolling returning to zero overwrites its earlier signed delta");
}
int inspect(const std::filesystem::path &file){
 sqlite3 *db{};auto name=file.u8string();
 if(sqlite3_open_v2(reinterpret_cast<const char*>(name.c_str()),&db,SQLITE_OPEN_READONLY,nullptr)!=SQLITE_OK)throw std::runtime_error("Cannot open diagnostic database read-only");
 Json result;sqlite3_stmt *s{};sqlite3_prepare_v2(db,"PRAGMA user_version",-1,&s,nullptr);sqlite3_step(s);result["schema"]=sqlite3_column_int(s,0);sqlite3_finalize(s);
 sqlite3_prepare_v2(db,"PRAGMA quick_check",-1,&s,nullptr);sqlite3_step(s);result["quick_check"]=reinterpret_cast<const char*>(sqlite3_column_text(s,0));sqlite3_finalize(s);
 sqlite3_close(db);std::cout<<result.dump()<<'\n';return 0;
}
}
int main(int argc,char **argv){try{
 if(argc==3&&std::string(argv[1])=="--crash-writer"){child_crash(std::filesystem::path(argv[2]));return 0;}
 if(argc==3&&std::string(argv[1])=="--inspect")return inspect(std::filesystem::path(argv[2]));
 const auto root=executable_data_directory()/"tests"/("persistence-"+std::to_string(utc_now_ms()));std::filesystem::create_directories(root);
 fixtures(root);storage_faults(root);crash_recovery(root/"crash");core_recovery(root/"core");
 degraded_startup(root/"degraded");migration_v5(root/"migration-v5");directory_lock(root/"single-writer");input_handoff(root/u8"handoff-恢复");
 std::cout<<checks<<" persistence/session recovery checks passed; controlled fixtures only\n";return 0;
}catch(const std::exception &e){sqlite_fault::write_error=0;std::cerr<<e.what()<<'\n';return 1;}}
