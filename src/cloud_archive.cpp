#include "cloud_internal.hpp"
#include "pulsetrace/core_service.hpp"
#include "pulsetrace/version.hpp"
#include <sqlite3.h>
#include <wincrypt.h>
#include <bcrypt.h>
#include <set>
namespace pulsetrace {
namespace cloud_detail {
CloudBytes protect(std::span<const unsigned char> bytes){DATA_BLOB in{static_cast<DWORD>(bytes.size()),const_cast<BYTE*>(bytes.data())},out{};if(!CryptProtectData(&in,L"PulseTrace cloud",nullptr,nullptr,nullptr,CRYPTPROTECT_UI_FORBIDDEN,&out))throw std::runtime_error("CLOUD_CREDENTIAL_STORE_FAILED");CloudBytes result(out.pbData,out.pbData+out.cbData);SecureZeroMemory(out.pbData,out.cbData);LocalFree(out.pbData);return result;}
CloudBytes unprotect(std::span<const unsigned char> bytes){DATA_BLOB in{static_cast<DWORD>(bytes.size()),const_cast<BYTE*>(bytes.data())},out{};if(!CryptUnprotectData(&in,nullptr,nullptr,nullptr,nullptr,CRYPTPROTECT_UI_FORBIDDEN,&out))throw std::runtime_error("CLOUD_CREDENTIAL_READ_FAILED");CloudBytes result(out.pbData,out.pbData+out.cbData);SecureZeroMemory(out.pbData,out.cbData);LocalFree(out.pbData);return result;}
std::string identity(){std::array<unsigned char,16> bytes{};if(BCryptGenRandom(nullptr,bytes.data(),16,BCRYPT_USE_SYSTEM_PREFERRED_RNG)<0)throw std::runtime_error("CLOUD_CRYPTO_FAILED");return cloud_sha256(bytes).substr(0,32);}
bool safe_file(const std::string &name){static const std::regex month(R"(^network-months/(?:[0-9]{4}-[0-9]{2}\.(?:sqlite3|json)|calendar\.json)$)");return name=="monitor.db"||name=="config.json"||std::regex_match(name,month);}
void validate_database(const std::filesystem::path &path,bool main){
 sqlite3 *db{};auto utf=path.u8string();if(sqlite3_open_v2(reinterpret_cast<const char*>(utf.c_str()),&db,SQLITE_OPEN_READONLY,nullptr)!=SQLITE_OK){if(db)sqlite3_close(db);throw std::runtime_error("CLOUD_DATABASE_INVALID");}
 struct Close{sqlite3 *p;~Close(){sqlite3_close(p);}} close{db};sqlite3_busy_timeout(db,2000);sqlite3_exec(db,"PRAGMA trusted_schema=OFF",nullptr,nullptr,nullptr);
 const auto scalar=[&](const char *sql){sqlite3_stmt *s{};if(sqlite3_prepare_v2(db,sql,-1,&s,nullptr)!=SQLITE_OK)throw std::runtime_error("CLOUD_DATABASE_INVALID");std::string value;if(sqlite3_step(s)==SQLITE_ROW){const auto p=sqlite3_column_text(s,0);if(p)value=reinterpret_cast<const char*>(p);}sqlite3_finalize(s);return value;};
 if(scalar("PRAGMA quick_check")!="ok")throw std::runtime_error("CLOUD_DATABASE_INVALID");
 const int version=std::stoi(scalar("PRAGMA user_version"));if(version<0||version>(main?DatabaseManager::supported_schema:1))throw std::runtime_error("CLOUD_DATABASE_NEWER_VERSION");
 if(main&&scalar("SELECT count(*) FROM sqlite_master WHERE type='table' AND name='counts'")!="1")throw std::runtime_error("CLOUD_DATABASE_INVALID");
}
}
namespace {
using namespace cloud_detail;
Json clean_config(const Json &cfg){
 static const std::set<std::string> allowed={"theme","language","retention_days","flush_seconds","close_to_tray","capture_on_launch","taskbar_font","taskbar_font_size","taskbar_unit","taskbar_precision","taskbar_upload_color","taskbar_download_color","taskbar_offset_x","taskbar_offset_y","taskbar_mode","taskbar_monitor","taskbar_background","usage_enabled","usage_idle_seconds","usage_icons","usage_retention_days","update_channel","update_source","check_updates_on_start"};
 if(!cfg.is_object())throw std::runtime_error("CLOUD_CONFIG_INVALID");Json result={{"config_version",1}};for(const auto &key:allowed)if(cfg.contains(key))result[key]=cfg[key];
 for(const char *key:{"campus_traffic_radix","campus_unit_price","campus_price_unit","campus_price_basis","campus_linear_tariff"})if(cfg.contains(key))result[key]=cfg[key];
 return result;
}
Json manifest_of(std::span<const unsigned char> bytes,size_t &offset){
 if(bytes.size()<4||bytes.size()>max_bytes)throw std::runtime_error("CLOUD_ARCHIVE_FORMAT");size_t size=0;for(unsigned i=0;i<4;++i)size|=static_cast<size_t>(bytes[i])<<(8*i);
 if(size>2*1024*1024||size>bytes.size()-4)throw std::runtime_error("CLOUD_ARCHIVE_FORMAT");offset=size+4;
 auto m=Json::parse(bytes.begin()+4,bytes.begin()+offset,[](int depth,Json::parse_event_t,Json&){if(depth>16)throw std::runtime_error("CLOUD_ARCHIVE_FORMAT");return true;});
 if(m.value("format",0)!=1||!m.contains("files")||!m["files"].is_array()||m["files"].empty()||m["files"].size()>512||m.value("schema",999)>DatabaseManager::supported_schema)throw std::runtime_error("CLOUD_ARCHIVE_FORMAT");return m;
}
}
CloudBytes cloud_pack(const std::filesystem::path &snapshot){
 using namespace cloud_detail;Json manifest={{"format",1},{"version",app_version},{"schema",DatabaseManager::supported_schema},{"created_ms",utc_now_ms()},{"files",Json::array()}};CloudBytes content;
 for(const auto &entry:std::filesystem::recursive_directory_iterator(snapshot)){
  if(!entry.is_regular_file()||entry.is_symlink())continue;const auto name=entry.path().lexically_relative(snapshot).generic_string();if(!safe_file(name))continue;
  CloudBytes bytes;
  if(name=="config.json"){const auto text=clean_config(json_read(entry.path())).dump();bytes.assign(text.begin(),text.end());}
  else bytes=read(entry.path());
  if(bytes.size()>max_bytes-content.size()||manifest["files"].size()>=512)throw std::runtime_error("CLOUD_BACKUP_TOO_LARGE");manifest["files"].push_back({{"name",name},{"size",bytes.size()},{"sha256",cloud_sha256(bytes)}});content.insert(content.end(),bytes.begin(),bytes.end());
 }
 if(!std::filesystem::exists(snapshot/"monitor.db")||!std::filesystem::exists(snapshot/"config.json"))throw std::runtime_error("CLOUD_SNAPSHOT_INCOMPLETE");
 const auto head=manifest.dump();if(head.size()+4+content.size()>max_bytes)throw std::runtime_error("CLOUD_BACKUP_TOO_LARGE");CloudBytes result;result.reserve(4+head.size()+content.size());for(unsigned i=0;i<4;++i)result.push_back(static_cast<unsigned char>(head.size()>>(8*i)));result.insert(result.end(),head.begin(),head.end());result.insert(result.end(),content.begin(),content.end());return result;
}
Json cloud_unpack(std::span<const unsigned char> payload,const std::filesystem::path &destination){
 using namespace cloud_detail;size_t offset{};auto m=manifest_of(payload,offset);std::set<std::string> names;
 for(const auto &file:m["files"]){const auto name=file.at("name").get<std::string>();const auto n=file.at("size").get<std::uint64_t>();if(!safe_file(name)||!names.insert(name).second||n>payload.size()-offset||cloud_sha256(payload.subspan(offset,static_cast<size_t>(n)))!=file.at("sha256").get<std::string>())throw std::runtime_error("CLOUD_ARCHIVE_INTEGRITY_FAILED");offset+=static_cast<size_t>(n);}
 if(offset!=payload.size()||!names.contains("monitor.db")||!names.contains("config.json")||std::filesystem::exists(destination))throw std::runtime_error("CLOUD_ARCHIVE_FORMAT");
 std::filesystem::create_directories(destination);manifest_of(payload,offset);
 for(const auto &file:m["files"]){const auto name=file.at("name").get<std::string>();const auto n=file.at("size").get<size_t>();write(destination/std::filesystem::path(name),payload.subspan(offset,n));offset+=n;if(name.ends_with(".db")||name.ends_with(".sqlite3"))validate_database(destination/name,name=="monitor.db");}
 if(clean_config(json_read(destination/"config.json"))!=json_read(destination/"config.json"))throw std::runtime_error("CLOUD_CONFIG_INVALID");return m;
}
Json cloud_apply_pending_restore(const std::filesystem::path &data,const std::function<void()> &after_database_install){
 using namespace cloud_detail;const auto pending=data/"cloud"/"pending.bin",transaction=data/"cloud"/"restore-transaction.bin";
 const std::vector<std::string> restore_names={"monitor.db","monitor.db-wal","monitor.db-shm","network-months","config.json"};
 // A durable journal makes interrupted multi-file replacement recoverable.
 if(std::filesystem::exists(transaction)){
  try{
   auto bytes=unprotect(read(transaction,16384));auto journal=Json::parse(bytes);SecureZeroMemory(bytes.data(),bytes.size());
   const auto name=journal.at("safety").get<std::string>();if(!std::regex_match(name,std::regex("pre-cloud-restore-[a-f0-9]{32}")))throw std::runtime_error("CLOUD_RESTORE_INVALID");
   if(!journal.value("committed",false)){
    const auto safety=data/"backups"/name,rejected=data/"backups"/("interrupted-cloud-restore-"+identity());std::filesystem::create_directories(rejected);
    for(const auto &file:restore_names){const bool original=journal.at("original").at(file).get<bool>();
     if(std::filesystem::exists(safety/file)){if(std::filesystem::exists(data/file))std::filesystem::rename(data/file,rejected/file);std::filesystem::rename(safety/file,data/file);}
     else if(!original&&std::filesystem::exists(data/file))std::filesystem::rename(data/file,rejected/file);
    }
   }
   std::filesystem::remove(pending);std::filesystem::remove(transaction);discard_restore_stage(data/"cloud",journal);
   Json result={{"phase",journal.value("committed",false)?"restored":"restore_rolled_back"}};try{json_write(data/"cloud"/"restore-result.json",result);}catch(...){}return result;
  }catch(...){throw std::runtime_error("CLOUD_RESTORE_ROLLBACK_FAILED");}
 }
 if(!std::filesystem::exists(pending))return {{"phase","none"}};
 Json result;
 try{
  auto plaintext=unprotect(read(pending,2*1024*1024));auto intent=Json::parse(plaintext);SecureZeroMemory(plaintext.data(),plaintext.size());
  const auto token=intent.at("token").get<std::string>();if(!std::regex_match(token,std::regex("[a-f0-9]{32}")))throw std::runtime_error("CLOUD_RESTORE_INVALID");
  const auto stage=data/"cloud"/("restore-"+token);const auto manifest=intent.at("manifest");
  // Revalidate both the staged hashes and schema immediately before any change.
  for(const auto &file:manifest.at("files")){const auto name=file.at("name").get<std::string>();if(!safe_file(name)||cloud_sha256(read(stage/name))!=file.at("sha256").get<std::string>())throw std::runtime_error("CLOUD_RESTORE_CHANGED");if(name.ends_with(".db")||name.ends_with(".sqlite3"))validate_database(stage/name,name=="monitor.db");}
  if(std::filesystem::exists(data/"pending"))for(const auto &entry:std::filesystem::directory_iterator(data/"pending"))if(entry.path().extension()==".pending")throw std::runtime_error("CLOUD_RECOVER_PENDING_FIRST");
  auto config=std::filesystem::exists(data/"config.json")?json_read(data/"config.json"):Json::object();config.update(clean_config(json_read(stage/"config.json")));config["capture_on_launch"]=false;
  const auto config_check=stage/"config-check";std::filesystem::create_directories(config_check);json_write(config_check/"config.json",config);{CoreService validate(config_check);}
  const auto safety=data/"backups"/("pre-cloud-restore-"+identity());std::filesystem::create_directories(safety);
  if(std::filesystem::exists(data/"monitor.db"))DatabaseManager::backup_file(data/"monitor.db",safety/"monitor.db.snapshot");
  Json journal={{"token",token},{"safety",safety.filename().string()},{"original",Json::object()},{"committed",false}};for(const auto &name:restore_names)journal["original"][name]=std::filesystem::exists(data/name);
  const auto persist_journal=[&]{const auto text=journal.dump();write(transaction,protect({reinterpret_cast<const unsigned char*>(text.data()),text.size()}),true);};persist_journal();
  std::vector<std::string> moved;
  try{
   for(const auto &name:restore_names)if(std::filesystem::exists(data/name)){std::filesystem::rename(data/name,safety/name);moved.push_back(name);}
   DatabaseManager::backup_file(stage/"monitor.db",data/"monitor.db");
   if(after_database_install)after_database_install();
   std::filesystem::create_directories(data/"network-months");
   for(const auto &file:manifest["files"]){const auto name=file["name"].get<std::string>();if(name.starts_with("network-months/"))std::filesystem::copy_file(stage/name,data/name);}
   json_write(data/"config.json",config);
   journal["committed"]=true;persist_journal();
  }catch(...){
   const auto rejected=data/"cloud"/("rollback-"+identity());std::filesystem::create_directories(rejected);
   try{for(const auto &name:restore_names)if(std::filesystem::exists(data/name)&&(std::filesystem::exists(safety/name)||!journal["original"][name].get<bool>()))std::filesystem::rename(data/name,rejected/name);for(auto it=moved.rbegin();it!=moved.rend();++it)std::filesystem::rename(safety/ *it,data/ *it);}catch(...){throw std::runtime_error("CLOUD_RESTORE_ROLLBACK_FAILED");}
   std::filesystem::remove(transaction);
   throw std::runtime_error("CLOUD_RESTORE_FAILED_ORIGINAL_RETAINED");
  }
  result={{"phase","restored"},{"time_ms",utc_now_ms()},{"safety_backup",safety.filename().string()}};std::error_code cleanup;std::filesystem::remove(pending,cleanup);if(!cleanup)std::filesystem::remove(transaction,cleanup);discard_restore_stage(data/"cloud",intent);
 }catch(const std::exception &e){if(std::string(e.what())=="CLOUD_RESTORE_ROLLBACK_FAILED")throw;result={{"phase","restore_error"},{"error","CLOUD_RESTORE_VALIDATION_OR_STORAGE_FAILED"}};}
 try{json_write(data/"cloud"/"restore-result.json",result);}catch(...){}return result;
}
}
