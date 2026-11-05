#include "pulsetrace/database.hpp"
#include "pulsetrace/monthly_traffic.hpp"
#include "pulsetrace/raw_input_host.hpp"
#include <sqlite3.h>
#include <fstream>
#include <iostream>
using namespace pulsetrace;
namespace {
void legacy_schema(const std::filesystem::path &path){
 sqlite3 *db{};const auto file=path.u8string();if(sqlite3_open(reinterpret_cast<const char*>(file.c_str()),&db)!=SQLITE_OK)throw std::runtime_error("Fixture open failed");
 const auto rc=sqlite3_exec(db,"BEGIN;DROP TABLE network_events;DROP TABLE protocol_samples;DROP TABLE traffic_rankings;DROP TABLE app_usage;PRAGMA user_version=1;COMMIT;",nullptr,nullptr,nullptr);sqlite3_close(db);if(rc!=SQLITE_OK)throw std::runtime_error("Fixture downgrade failed");
}
void verify_readonly(const std::filesystem::path &directory){
 sqlite3 *db{};const auto file=(directory/L"monitor.db").u8string();if(sqlite3_open_v2(reinterpret_cast<const char*>(file.c_str()),&db,SQLITE_OPEN_READONLY,nullptr)!=SQLITE_OK){if(db)sqlite3_close(db);throw std::runtime_error("Read-only fixture open failed");}
 sqlite3_stmt *query{};const auto rc=sqlite3_prepare_v2(db,"SELECT module,metric,SUM(value) FROM counts GROUP BY module,metric",-1,&query,nullptr);if(rc!=SQLITE_OK){sqlite3_close(db);throw std::runtime_error("Read-only fixture query failed");}
 std::int64_t keys=0,up=0,down=0;int step=0;while((step=sqlite3_step(query))==SQLITE_ROW){const std::string module=reinterpret_cast<const char*>(sqlite3_column_text(query,0)),metric=reinterpret_cast<const char*>(sqlite3_column_text(query,1));const auto n=sqlite3_column_int64(query,2);if(module=="keyboard"&&metric=="30")keys+=n;if(metric=="interface_upload_bytes")up+=n;if(metric=="interface_download_bytes")down+=n;}
 sqlite3_finalize(query);sqlite3_close(db);if(step!=SQLITE_DONE||keys!=42||up!=1000||down!=2000)throw std::runtime_error("Historical values changed");
 up=0;down=0;for(const auto &entry:std::filesystem::directory_iterator(directory/L"network-months")){const auto stem=entry.path().stem().string();if(entry.path().extension()!=L".json"||stem.size()!=7||stem[4]!='-')continue;std::ifstream in(entry.path());Json summary;in>>summary;up+=summary.at("upload_bytes").get<std::int64_t>();down+=summary.at("download_bytes").get<std::int64_t>();}if(up!=1000||down!=2000)throw std::runtime_error("Monthly archive values changed");
}
}
int wmain(int argc,wchar_t **argv){try{if(argc!=3)throw std::runtime_error("--create DIR | --verify DIR");const std::filesystem::path directory=argv[2];if(std::wstring(argv[1])==L"--create"){
 std::filesystem::create_directories(directory);if(std::filesystem::exists(directory/L"monitor.db"))throw std::runtime_error("Fixture destination must be new");const auto minute=utc_now_ms()/60000;Json rows=Json::array({{{"minute",minute},{"module","keyboard"},{"metric","30"},{"value",42}},{{"minute",minute},{"module","network"},{"metric","interface_upload_bytes"},{"value",1000}},{{"minute",minute},{"module","network"},{"metric","interface_download_bytes"},{"value",2000}}});{DatabaseManager db(directory/L"monitor.db");db.save("controlled-upgrade-fixture",rows);}MonthlyTrafficStore months(directory/L"network-months");months.save("controlled-upgrade-fixture",rows);
 legacy_schema(directory/L"monitor.db");for(const auto &entry:std::filesystem::directory_iterator(directory/L"network-months"))if(entry.path().extension()==L".sqlite3")legacy_schema(entry.path());
 std::ofstream config(directory/L"config.json");config<<R"({"theme":"dark","retention_days":90,"capture_on_launch":false,"taskbar_mode":"tray","unknown_extension":{"keep":17}})";
 }else if(std::wstring(argv[1])==L"--verify"){
  verify_readonly(directory);
 }else throw std::runtime_error("Unknown fixture command");std::cout<<"Upgrade fixture verified\n";return 0;}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
