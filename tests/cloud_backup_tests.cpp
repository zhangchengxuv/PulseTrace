#include "pulsetrace/cloud_backup.hpp"
#include "pulsetrace/database.hpp"
#include "pulsetrace/core_service.hpp"
#include "pulsetrace/protocol.hpp"
#include "cloud_internal.hpp"
#include <sqlite3.h>
#include <windows.h>
#include <psapi.h>
#include <iostream>
#include <thread>
#include <map>
#include <mutex>
#include <future>
using namespace pulsetrace;using namespace pulsetrace::cloud_detail;
namespace {
void check(bool value,const char *what){if(!value)throw std::runtime_error(what);}
template<class F>void rejected(F f){bool caught=false;try{f();}catch(...){caught=true;}check(caught,"invalid operation accepted");}
void populate(const std::filesystem::path &p,unsigned n){std::filesystem::create_directories(p);DatabaseManager db(p/"monitor.db");db.save("fixture",Json::array({{{"minute",100},{"module","keyboard"},{"metric","30"},{"value",n}}}),Json::array());json_write(p/"config.json",{{"config_version",1},{"theme","dark"},{"secret_do_not_upload","PRIVATE_FIXTURE"}});}
unsigned count(const std::filesystem::path &p){DatabaseManager db(p/"monitor.db");auto r=db.query(0,101,1);return r.empty()?0:r[0]["value"].get<unsigned>();}
void wait(CloudBackupService &s){const auto end=GetTickCount64()+15000;while(s.status().value("busy",false)&&GetTickCount64()<end)Sleep(10);check(!s.status().value("busy",false),"cloud job timed out");}
}
int main(){try{
 const auto root=std::filesystem::absolute("data/tests/cloud-"+identity());std::filesystem::create_directories(root);const auto snapshot=root/"snapshot",data=root/"data";
 write(root/"unavailable/cloud",CloudBytes{'x'});check(cloud_apply_pending_restore(root/"unavailable")["phase"]=="none","unused cloud folder blocked normal startup");{CloudBackupService unavailable(root/"unavailable",[]{return std::filesystem::path();},cloud_https_request,false);check(!unavailable.status()["config"]["enabled"].get<bool>(),"unavailable cloud storage enabled itself");}
 populate(snapshot,7);populate(data,99);json_write(data/"config.json",{{"config_version",1},{"theme","light"},{"unknown_extension",{{"keep",17}}}});
 std::filesystem::create_directories(snapshot/"network-months");sqlite3 *month{};sqlite3_open((snapshot/"network-months/2026-11.sqlite3").string().c_str(),&month);sqlite3_exec(month,"CREATE TABLE monthly(value);INSERT INTO monthly VALUES(77)",nullptr,nullptr,nullptr);sqlite3_close(month);
 write(snapshot/"network-months/calendar.json",CloudBytes{'{','}'});write(snapshot/"credentials.bin",CloudBytes{'S','E','C','R','E','T'});
 const Json campus_config={{"campus_traffic_radix",1000},{"campus_unit_price",0.6},{"campus_price_unit","GiB"},{"campus_price_basis","legacy_bytes"},{"campus_linear_tariff",false}};
 auto snapshot_config=json_read(snapshot/"config.json");snapshot_config.update(campus_config);json_write(snapshot/"config.json",snapshot_config);
 auto plain=cloud_pack(snapshot);const std::string password="synthetic recovery password";auto cipher=cloud_encrypt(plain,password);check(cipher!=plain,"archive not encrypted");check(cloud_decrypt(cipher,password)==plain,"encryption round trip");
 rejected([&]{cloud_decrypt(cipher,"another recovery password");});auto tampered=cipher;tampered.back()^=1;rejected([&]{cloud_decrypt(tampered,password);});
 auto manifest=cloud_unpack(plain,root/"unpack");check(count(root/"unpack")==7,"snapshot database mismatch");check(!std::filesystem::exists(root/"unpack/credentials.bin"),"credential file included");check(!json_read(root/"unpack/config.json").contains("secret_do_not_upload"),"secret config included");
 const auto unpacked_config=json_read(root/"unpack/config.json");for(const auto &[key,value]:campus_config.items())check(unpacked_config.at(key)==value,"campus billing basis lost in cloud archive");
 std::map<std::string,CloudBytes> remote;std::mutex remote_mutex;std::atomic<unsigned> failure{},requests{};std::atomic<bool> block{},interrupt_upload{},corrupt_download{};
 auto transport=[&](const CloudRequest &r,std::atomic_bool &cancel)->CloudResponse{
  ++requests;check(r.url.starts_with("https://dav.jianguoyun.com/dav/"),"request escaped configured server");
  while(block&&!cancel)Sleep(5);if(cancel)throw std::runtime_error("CLOUD_CANCELLED");if(failure)return {failure,{}};
  std::lock_guard lock(remote_mutex);
  if(r.method=="MKCOL")return {201,{}};
  if(r.method=="PUT"){check(r.create_only,"upload permits overwriting history");check(!remote.contains(r.url),"backup name collision");if(interrupt_upload)throw std::runtime_error("CLOUD_UPLOAD_INTERRUPTED");remote[r.url]=r.body;return {201,{}};}
  if(r.method=="GET"){auto response=remote.contains(r.url)?CloudResponse{200,remote[r.url]}:CloudResponse{404,{}};if(corrupt_download&&!response.body.empty())response.body.back()^=1;return response;}
  if(r.method=="PROPFIND"){std::string xml="<d:multistatus xmlns:d='DAV:'>";if(r.depth==1)for(const auto &[url,b]:remote)xml+="<d:response><d:href>"+url+"</d:href></d:response>";xml+="</d:multistatus>";return {207,{xml.begin(),xml.end()}};}
  throw std::runtime_error("unexpected DAV method");
 };
 CloudBackupService service(data,[&]{return snapshot;},transport);check(!service.status()["config"]["enabled"].get<bool>()&&requests==0,"cloud defaults not disabled");
 rejected([&]{service.command("cloud_configure",{{"server","http://example.test/"}});});
 rejected([&]{service.command("cloud_configure",{{"interval_minutes",1}});});
 rejected([&]{service.command("cloud_configure",{{"directory","../escape"}});});
 service.command("cloud_configure",{{"enabled",true},{"account","synthetic@example.test"},{"app_password","synthetic app password"},{"encryption_password",password}});
 auto stored=read(data/"cloud/credentials.bin");const std::string stored_text(stored.begin(),stored.end());check(stored_text.find("synthetic")==std::string::npos,"plaintext credentials persisted");check(!json_read(data/"cloud/settings.json").contains("account"),"account in plain settings");
 service.command("cloud_test");wait(service);check(service.status()["phase"]=="success","connection test failed");
 service.command("cloud_backup");wait(service);check(service.status()["phase"]=="success","backup failed");check(remote.size()==1,"backup not uploaded");const auto name=service.status()["last_backup_name"].get<std::string>();
 check(read(data/"cloud/archives"/name)==remote.begin()->second,"local encrypted archive missing");auto saved_plain=cloud_decrypt(read(data/"cloud/archives"/name),password);cloud_unpack(saved_plain,root/"saved-unpack");check(count(root/"saved-unpack")==7,"uploaded snapshot mismatch");rejected([&]{service.command("cloud_backup");});
 service.command("cloud_list");wait(service);check(service.status()["files"].size()==1,"DAV listing failed");
 service.command("cloud_prepare_restore",{{"name",name}});wait(service);const auto cancelled_token=service.status()["restore_token"].get<std::string>();service.command("cloud_cancel_restore");check(!std::filesystem::exists(data/"cloud"/("restore-"+cancelled_token)),"cancelled restore retained plaintext stage");
 service.command("cloud_prepare_restore",{{"name",name}});wait(service);check(service.status()["phase"]=="restore_ready","restore preparation failed");check(count(data)==99,"preparation changed active data");
 const auto token=service.status()["restore_token"];
 service.command("cloud_confirm_restore",{{"token",token},{"confirmation","WRONG"}});wait(service);check(!std::filesystem::exists(data/"cloud/pending.bin"),"unconfirmed restore persisted");
 service.command("cloud_confirm_restore",{{"token",token},{"confirmation","RESTORE"}});wait(service);check(count(data)==99,"confirmation overwrote a live database");check(service.status()["pending_restore"]==true,"restore intent missing");service.stop();
 auto failed_restore=cloud_apply_pending_restore(data,[&]{write(data/"monitor.db-wal",CloudBytes{'x'});throw std::runtime_error("synthetic storage failure");});check(failed_restore["phase"]=="restore_error","storage failure accepted");check(!std::filesystem::exists(data/"monitor.db-wal")||read(data/"monitor.db-wal")!=CloudBytes{'x'},"failed restore left a replacement WAL");check(count(data)==99,"failed restore changed original data");
 auto restored=cloud_apply_pending_restore(data);check(restored["phase"]=="restored"&&count(data)==7,"restore failed");const auto safety=data/"backups"/restored["safety_backup"].get<std::string>();check(count(safety)==99,"restore safety backup lost data");check(json_read(data/"config.json")["unknown_extension"]["keep"]==17,"unknown current config lost");check(!json_read(data/"config.json")["capture_on_launch"].get<bool>(),"restore restarted capture without review");
 for(const auto &[key,value]:campus_config.items())check(json_read(data/"config.json").at(key)==value,"campus billing basis lost after restore");
 for(unsigned status:{401u,403u,429u,507u,500u}){failure=status;CloudBackupService errors(data,[&]{return snapshot;},transport);errors.command("cloud_test");wait(errors);check(errors.status()["phase"]=="error","DAV failure accepted");}
 failure=0;block=true;{CloudBackupService cancelled(data,[&]{return snapshot;},transport);const auto tick=GetTickCount64();cancelled.command("cloud_test");check(GetTickCount64()-tick<100,"network blocks caller");Sleep(30);
  {DatabaseManager db(data/"monitor.db");db.save("while-network-blocked",Json::array({{{"minute",100},{"module","keyboard"},{"metric","30"},{"value",3}}}),Json::array());}
  check(count(data)==10,"network task blocked database writes");cancelled.command("cloud_cancel");wait(cancelled);check(cancelled.status()["phase"]=="cancelled","cancel ignored");}block=false;
 for(unsigned fault=0;fault<3;++fault){
  const auto failed_data=root/("failed-"+std::to_string(fault));populate(failed_data,55);failure=fault==0?507:0;interrupt_upload=fault==1;corrupt_download=fault==2;
  CloudBackupService failed(failed_data,[&]{return snapshot;},transport);failed.command("cloud_configure",{{"enabled",true},{"account","synthetic@example.test"},{"app_password","synthetic app password"},{"encryption_password",password}});failed.command("cloud_backup");wait(failed);
  check(failed.status()["phase"]=="error"&&count(failed_data)==55,"failed upload changed active data");check(!std::filesystem::is_empty(failed_data/"cloud/archives"),"failed upload lost local encrypted archive");
 }
 failure=0;interrupt_upload=false;corrupt_download=false;
 const auto quota_data=root/"quota";populate(quota_data,77);json_write(quota_data/"cloud/quota.json",{{"window_ms",std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count()},{"requests",500}});
 {CloudBackupService limited(quota_data,[&]{return snapshot;},transport);limited.command("cloud_configure",{{"enabled",true},{"account","synthetic@example.test"},{"app_password","synthetic app password"},{"encryption_password",password}});const auto before=requests.load();limited.command("cloud_test");wait(limited);check(limited.status()["error"]=="CLOUD_RATE_LIMITED"&&requests==before,"persisted DAV quota ignored");}
 check(parse_message(Json{{"type","cloud_status"},{"request_id",77},{"params",Json::object()}}.dump())["request_id"]==77,"cloud bridge lost request ID");
 rejected([&]{parse_message(Json{{"type","cloud_confirm_restore"},{"request_id",77},{"params",{{"token","0123456789abcdef0123456789abcdef"},{"confirmation","WRONG"}}}}.dump());});
 // A validly encrypted payload still cannot restore a newer SQLite schema.
 const auto future=root/"future";std::filesystem::copy(snapshot,future,std::filesystem::copy_options::recursive);sqlite3 *future_db{};sqlite3_open((future/"monitor.db").string().c_str(),&future_db);sqlite3_exec(future_db,"PRAGMA user_version=999",nullptr,nullptr,nullptr);sqlite3_close(future_db);
 auto future_archive=cloud_pack(future);rejected([&]{cloud_unpack(future_archive,root/"rejected-newer");});check(count(data)==10,"incompatible archive changed active DB");
 size_t old_head=0;for(unsigned i=0;i<4;++i)old_head|=static_cast<size_t>(plain[i])<<(8*i);auto malicious=manifest;malicious["files"][0]["name"]="../monitor.db";const auto head=malicious.dump();CloudBytes traversal;for(unsigned i=0;i<4;++i)traversal.push_back(static_cast<unsigned char>(head.size()>>(i*8)));traversal.insert(traversal.end(),head.begin(),head.end());traversal.insert(traversal.end(),plain.begin()+4+old_head,plain.end());rejected([&]{cloud_unpack(traversal,root/"traversal");});check(!std::filesystem::exists(root/"traversal"),"archive path escaped validation");
 // Simulate a crash after the original DB was moved but before the new set committed.
 const auto interrupted=root/"interrupted";populate(interrupted,88);const auto old="pre-cloud-restore-"+identity();std::filesystem::create_directories(interrupted/"backups"/old);std::filesystem::rename(interrupted/"monitor.db",interrupted/"backups"/old/"monitor.db");DatabaseManager::backup_file(snapshot/"monitor.db",interrupted/"monitor.db");
 Json journal={{"safety",old},{"committed",false},{"original",{{"monitor.db",true},{"monitor.db-wal",false},{"monitor.db-shm",false},{"network-months",false},{"config.json",true}}}};auto text=journal.dump();write(interrupted/"cloud/restore-transaction.bin",protect({reinterpret_cast<const unsigned char*>(text.data()),text.size()}));
 check(cloud_apply_pending_restore(interrupted)["phase"]=="restore_rolled_back"&&count(interrupted)==88,"interrupted restore lost original data");
 std::atomic_bool cancel=false;rejected([&]{cloud_https_request({"GET","http://127.0.0.1/","","",{},false},cancel);});
 // Exercise the same live core snapshot bridge used by the desktop application.
 const auto core_data=root/"core";std::filesystem::create_directories(core_data);json_write(core_data/"config.json",{{"config_version",1},{"capture_on_launch",false},{"usage_enabled",false},{"check_updates_on_start",false}});
 CoreService core(core_data);core.start();{
  CloudBackupService integrated(core_data,[&]{check(core.flush_now(std::chrono::seconds(5)),"core did not flush");auto promise=std::make_shared<std::promise<Json>>();auto reply=promise->get_future();core.request({{"type","cloud_snapshot"},{"request_id",77},{"params",Json::object()}},[promise](Json result){promise->set_value(std::move(result));});check(reply.wait_for(std::chrono::seconds(10))==std::future_status::ready,"core snapshot timeout");auto result=reply.get();check(result.value("ok",false),"core snapshot request rejected");return std::filesystem::path(wide(result["data"]["directory"]));},transport);
  integrated.command("cloud_configure",{{"enabled",true},{"account","synthetic@example.test"},{"app_password","synthetic app password"},{"encryption_password",password}});integrated.command("cloud_backup");wait(integrated);check(integrated.status()["phase"]=="success","live core backup integration failed");check(std::filesystem::is_empty(core_data/"cloud/snapshots"),"plaintext cloud snapshot retained after archive encryption");
 }core.stop();
 FILETIME created{},exited{},kernel_before{},user_before{},kernel_after{},user_after{};GetProcessTimes(GetCurrentProcess(),&created,&exited,&kernel_before,&user_before);{CloudBackupService idle(root/"idle",[&]{return snapshot;},transport);const auto before=requests.load();Sleep(500);check(requests==before,"disabled idle service contacted DAV");}GetProcessTimes(GetCurrentProcess(),&created,&exited,&kernel_after,&user_after);
 const auto ticks=[](FILETIME t){return (static_cast<std::uint64_t>(t.dwHighDateTime)<<32)|t.dwLowDateTime;};const auto idle_cpu_us=(ticks(kernel_after)+ticks(user_after)-ticks(kernel_before)-ticks(user_before))/10;
 PROCESS_MEMORY_COUNTERS_EX memory{};GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory),sizeof(memory));
 std::cout<<"Cloud encryption, DPAPI, Online Backup, DAV methods/errors, cancellation, restore confirmation, rollback and config preservation passed; idle_cpu_us="<<idle_cpu_us<<"; peak_working_set="<<memory.PeakWorkingSetSize<<"\n";return 0;
 }catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
