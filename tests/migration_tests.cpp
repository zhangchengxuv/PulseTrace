#include "pulsetrace/core_service.hpp"
#include "pulsetrace/update.hpp"
#include "pulsetrace/installation.hpp"
#include <sqlite3.h>
#include <fstream>
#include <iostream>
using namespace pulsetrace;
namespace {
int checks=0;void check(bool value,const char *message){++checks;if(!value)throw std::runtime_error(message);}
template<class F>void rejects(F fn,const char *message){bool failed=false;try{fn();}catch(...){failed=true;}check(failed,message);}
struct DB{sqlite3 *p{};explicit DB(const std::filesystem::path &f){auto n=f.u8string();if(sqlite3_open(reinterpret_cast<const char*>(n.c_str()),&p))throw std::runtime_error("open fixture");}~DB(){sqlite3_close(p);}void exec(const char *s){char *e{};if(sqlite3_exec(p,s,nullptr,nullptr,&e)){std::string error=e;sqlite3_free(e);throw std::runtime_error(error);}}std::int64_t scalar(const char *s){sqlite3_stmt *pstmt{};if(sqlite3_prepare_v2(p,s,-1,&pstmt,nullptr))throw std::runtime_error("prepare fixture");auto rc=sqlite3_step(pstmt);auto n=rc==SQLITE_ROW?sqlite3_column_int64(pstmt,0):-1;sqlite3_finalize(pstmt);return n;}};
std::string text(const std::filesystem::path &p){std::ifstream in(p,std::ios::binary);return {std::istreambuf_iterator<char>(in),{}};}
}
int main(){try{
 const auto root=std::filesystem::absolute("data/tests/migrations-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64()));std::filesystem::create_directories(root);
 const auto app=root/"installation";std::filesystem::create_directories(app);check(DataDirectoryManager::resolve(app)==app/L"data","portable data follows executable root");
 const auto external=root/"legacy";std::filesystem::create_directories(external);WritePrivateProfileStringW(L"PulseTrace",L"DataDirectory",external.c_str(),(app/L"installed.ini").c_str());rejects([&]{DataDirectoryManager::resolve(app);},"external installed data requires explicit migration");check(DataDirectoryManager::resolve(app,external)==external,"explicit test/user override takes priority");
 WritePrivateProfileStringW(L"PulseTrace",L"DataDirectory",(app/L"data").c_str(),(app/L"installed.ini").c_str());check(DataDirectoryManager::resolve(app)==app/L"data","installed data follows executable root");
 set_ui_language("en-US");check(std::wstring(ui_text("tray.pause"))==L"Pause monitoring","native English menu");set_ui_language("zh-CN");check(std::wstring(ui_text("tray.pause"))==L"暂停监测","native Chinese menu");check(effective_language("unsupported")==effective_language("system"),"native system fallback");
 for(int version:{1,2}){auto file=root/("v"+std::to_string(version)+".db");{DB old(file);old.exec("CREATE TABLE counts(session TEXT NOT NULL,minute INTEGER NOT NULL,module TEXT NOT NULL,metric TEXT NOT NULL,value INTEGER NOT NULL,PRIMARY KEY(session,minute,module,metric)) WITHOUT ROWID;INSERT INTO counts VALUES('legacy',100,'keyboard','30',42)");old.exec(("PRAGMA user_version="+std::to_string(version)).c_str());}
  std::string backup;{DatabaseManager db(file);check(db.query(100,101,1)[0]["value"]==42,"legacy rows preserved");auto m=db.migration_info();check(m["from"]==version&&m["to"]==DatabaseManager::supported_schema&&m["status"]=="migrated","sequential migration info");backup=m["backup"];check(std::filesystem::exists(std::filesystem::path(std::u8string(backup.begin(),backup.end()))),"consistent pre-migration backup retained");}
  {DB verify(file);check(verify.scalar("PRAGMA user_version")==DatabaseManager::supported_schema,"schema committed");}
  {DB original(std::filesystem::path(std::u8string(backup.begin(),backup.end())));check(original.scalar("PRAGMA user_version")==version&&original.scalar("SELECT SUM(value) FROM counts")==42,"backup contains original schema and counts");}
  {DatabaseManager again(file);check(again.migration_info()["status"]=="current"&&again.query(100,101,1)[0]["value"]==42,"migration idempotent");check(again.migration_info().contains("previous"),"migration receipt survives restart");}
 }
 auto wal=root/"wal.db";{DB active(wal);active.exec("PRAGMA journal_mode=WAL;PRAGMA wal_autocheckpoint=0;CREATE TABLE sample(n);INSERT INTO sample VALUES(7);BEGIN;INSERT INTO sample VALUES(9);COMMIT;");check(std::filesystem::file_size(wal.wstring()+L"-wal")>0,"fixture has live WAL");DatabaseManager::backup_file(wal,root/"wal-copy.db");DB snapshot(root/"wal-copy.db");check(snapshot.scalar("SELECT SUM(n) FROM sample")==16,"online backup includes committed WAL");rejects([&]{DatabaseManager::backup_file(wal,root/"wal-copy.db");},"backup never overwrites");}
 auto broken=root/"broken.db";{DB bad(broken);bad.exec("CREATE TABLE counts(bad TEXT);INSERT INTO counts VALUES('retained');PRAGMA user_version=1;");}
 rejects([&]{DatabaseManager migration(broken);},"invalid legacy schema rolls back");{DB bad(broken);check(bad.scalar("PRAGMA user_version")==1,"failed migration preserves version");check(bad.scalar("SELECT COUNT(*) FROM counts")==1,"failed migration preserves rows");check(bad.scalar("SELECT COUNT(*) FROM sqlite_schema WHERE name='protocol_samples'")==0,"new migration table rolled back");}
 auto future=root/"future.db";{DB db(future);db.exec("CREATE TABLE future(n);INSERT INTO future VALUES(99);PRAGMA user_version=99;");}const auto before=sha256_file(future);rejects([&]{DatabaseManager unsupported(future);},"future schema refused");check(sha256_file(future)==before,"future database byte-for-byte unchanged");
 auto cfg=root/"config-case";std::filesystem::create_directories(cfg);{std::ofstream out(cfg/"config.json");out<<R"({"theme":"dark","unknown_extension":{"keep":17},"network_interface":"","retention_days":90})";}
 {CoreService service(cfg);const auto c=service.status()["config"];check(c["unknown_extension"]["keep"]==17,"unknown configuration preserved");check(c["config_version"]==1&&c["update_source"]=="auto"&&!c["check_updates_on_start"].get<bool>(),"new defaults filled");check(c["retention_days"]==90&&c["theme"]=="dark","user preferences retained");}
 check(std::filesystem::exists(cfg/"backups"),"old configuration backed up");const auto saved=text(cfg/"config.json");{CoreService again(cfg);check(text(cfg/"config.json")==saved,"configuration migration idempotent");}
 {std::ofstream out(cfg/"config.json");out<<R"({"config_version":99,"important":"preserve"})";}const auto newer=text(cfg/"config.json");rejects([&]{CoreService futureConfig(cfg);},"future config refused");check(text(cfg/"config.json")==newer,"future configuration unchanged");
 std::cout<<checks<<" migration checks passed; fixtures: "<<root<<'\n';return 0;
 }catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
