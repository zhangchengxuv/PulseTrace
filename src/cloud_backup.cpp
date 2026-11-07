#include "cloud_internal.hpp"
#include "pulsetrace/version.hpp"
#include <winhttp.h>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <set>
#include <utility>
namespace pulsetrace {
namespace {
using namespace cloud_detail;
std::int64_t now_ms(){return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();}
std::string directory_url(const Json &cfg){
 const auto server=cfg.at("server").get<std::string>(),directory=cfg.at("directory").get<std::string>();
 std::string result=server;constexpr char hex[]="0123456789ABCDEF";for(unsigned char c:directory){if((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='/'||c=='-'||c=='_'||c=='.')result+=static_cast<char>(c);else{result+='%';result+=hex[c>>4];result+=hex[c&15];}}return result+"/";
}
void validate_settings(Json &cfg){
 const auto s=cfg.at("server").get<std::string>(),d=cfg.at("directory").get<std::string>();
 const auto url=wide(s);URL_COMPONENTS u{};u.dwStructSize=sizeof(u);u.dwHostNameLength=u.dwUrlPathLength=u.dwExtraInfoLength=u.dwUserNameLength=u.dwPasswordLength=static_cast<DWORD>(-1);
 if(s.size()>2048||!s.ends_with('/')||s.find('\\')!=std::string::npos||std::any_of(s.begin(),s.end(),[](unsigned char c){return c<32||c==127;})||!WinHttpCrackUrl(url.c_str(),static_cast<DWORD>(url.size()),0,&u)||u.nScheme!=INTERNET_SCHEME_HTTPS||u.dwExtraInfoLength||u.dwUserNameLength||u.dwPasswordLength||!u.dwHostNameLength)throw std::runtime_error("CLOUD_HTTPS_REQUIRED");
 if(d.empty()||d.size()>512||d.front()=='/'||d.back()=='/'||d.find('\\')!=std::string::npos||d.find(':')!=std::string::npos||d.find('%')!=std::string::npos||std::any_of(d.begin(),d.end(),[](unsigned char c){return c<32||c==127;}))throw std::runtime_error("CLOUD_DIRECTORY_INVALID");
 unsigned depth=0;for(size_t p=0;p<d.size();){auto end=d.find('/',p);auto part=d.substr(p,end==std::string::npos?end:end-p);if(++depth>8||part.empty()||part=="."||part==".."||part.find_first_of("\r\n\t")!=std::string::npos)throw std::runtime_error("CLOUD_DIRECTORY_INVALID");if(end==std::string::npos)break;p=end+1;}
 if(!cfg.at("enabled").is_boolean()||!cfg.at("automatic").is_boolean()||!cfg.at("interval_minutes").is_number_integer()||cfg["interval_minutes"]<60||cfg["interval_minutes"]>43200)throw std::runtime_error("CLOUD_INTERVAL_INVALID");
}
bool backup_name(const std::string &s){static const std::regex pattern(R"(^PulseTrace-[0-9]{13}-[a-f0-9]{32}-v[0-9A-Za-z.-]{1,40}\.ptb$)");return std::regex_match(s,pattern);}
void http_ok(unsigned status,std::initializer_list<unsigned> accepted){if(std::find(accepted.begin(),accepted.end(),status)!=accepted.end())return;if(status==401||status==403)throw std::runtime_error("CLOUD_AUTH_FAILED");if(status==429)throw std::runtime_error("CLOUD_RATE_LIMITED");if(status==507)throw std::runtime_error("CLOUD_STORAGE_FULL");if(status>=300&&status<400)throw std::runtime_error("CLOUD_REDIRECT_REFUSED");throw std::runtime_error("CLOUD_HTTP_FAILED");}
Json load_credentials(const std::filesystem::path &p){if(!std::filesystem::exists(p))return Json::object();auto bytes=unprotect(read(p,16384));auto out=Json::parse(bytes);SecureZeroMemory(bytes.data(),bytes.size());return out;}
}
struct CloudBackupService::Impl{
 std::filesystem::path data,root;std::function<std::filesystem::path()> capture;CloudTransport transport;
 mutable std::mutex mutex;std::condition_variable wake;std::thread worker;std::atomic_bool cancel{false};bool stopping{},busy{};std::string job;Json params;
 Json config={{"enabled",false},{"server","https://dav.jianguoyun.com/dav/"},{"directory","PulseTrace/Backups"},{"automatic",false},{"interval_minutes",1440}},view={{"phase","idle"},{"error",""},{"files",Json::array()},{"history",Json::array()},{"encrypted",true}};
 std::int64_t last_attempt{},last_manual{};Json prepared,quota;
 Impl(std::filesystem::path p,std::function<std::filesystem::path()> c,CloudTransport t,bool start):data(std::move(p)),root(data/"cloud"),capture(std::move(c)),transport(std::move(t)){
  try{std::filesystem::create_directories(root);if(std::filesystem::exists(root/"settings.json")){const auto stored=json_read(root/"settings.json");for(auto it=config.begin();it!=config.end();++it)if(stored.contains(it.key()))it.value()=stored[it.key()];validate_settings(config);}if(std::filesystem::exists(root/"history.json")){auto history=json_read(root/"history.json");if(!history.is_array())throw std::runtime_error("CLOUD_HISTORY_INVALID");view["history"]=std::move(history);}if(std::filesystem::exists(root/"restore-result.json"))view["restore_result"]=json_read(root/"restore-result.json");}catch(...){config["enabled"]=false;config["automatic"]=false;view["error"]="CLOUD_CONFIG_OR_HISTORY_INVALID";}
  try{if(std::filesystem::exists(root/"last-attempt.json")){last_attempt=std::clamp(json_read(root/"last-attempt.json").value("time_ms",0LL),0LL,static_cast<long long>(now_ms()));last_manual=last_attempt;}if(view["history"].is_array())for(const auto &item:view["history"])if(item.is_object()&&item.value("operation","")=="cloud_backup"&&item.value("result","")=="verified"){view["last_backup_ms"]=item["time_ms"];view["last_backup_name"]=item["name"];}}catch(...){view["error"]="CLOUD_HISTORY_READ_FAILED";}
  if(start)worker=std::thread([this]{run();});
 }
 void record(const std::string &operation,const std::string &name,const std::string &result){std::lock_guard lock(mutex);try{if(!view["history"].is_array())view["history"]=Json::array();view["history"].push_back({{"operation",operation},{"name",name},{"result",result},{"time_ms",now_ms()}});while(view["history"].size()>100)view["history"].erase(view["history"].begin());json_write(root/"history.json",view["history"]);}catch(...){view["error"]="CLOUD_HISTORY_WRITE_FAILED";}}
 CloudResponse request(const Json &,const Json &credentials,const std::string &method,const std::string &url,CloudBytes body={},bool create=false,unsigned depth=0){
  if(cancel)throw std::runtime_error("CLOUD_CANCELLED");
  if(quota.is_null())quota=std::filesystem::exists(root/"quota.json")?json_read(root/"quota.json"):Json{{"window_ms",now_ms()},{"requests",0}};
  if(now_ms()-quota.at("window_ms").get<std::int64_t>()>=1800000)quota={{"window_ms",now_ms()},{"requests",0}};
  const auto count=quota.at("requests").get<int>();if(count<0||count+2>500)throw std::runtime_error("CLOUD_RATE_LIMITED");
  // Reserve two requests for a possible HTTP authentication challenge; persist
  // the 30-minute budget so restarting cannot create an automatic retry burst.
  quota["requests"]=count+2;json_write(root/"quota.json",quota);
  auto result=transport({method,url,credentials.value("account",""),credentials.value("app_password",""),std::move(body),create,depth},cancel);if(cancel)throw std::runtime_error("CLOUD_CANCELLED");return result;
 }
 void perform(const std::string &operation,const Json &p,Json cfg){
  if(!cfg.value("enabled",false))throw std::runtime_error("CLOUD_DISABLED");const auto credentials=load_credentials(root/"credentials.bin");
  if(credentials.value("server","")!=cfg.at("server").get<std::string>()||credentials.value("account","").empty()||credentials.value("app_password","").empty()||credentials.value("encryption_password","").size()<12)throw std::runtime_error("CLOUD_CREDENTIALS_REQUIRED");
  const auto remote=directory_url(cfg);
  if(operation=="cloud_test"){http_ok(request(cfg,credentials,"PROPFIND",cfg["server"]).status,{207});return;}
  if(operation=="cloud_list"){
   auto response=request(cfg,credentials,"PROPFIND",remote,{},false,1);http_ok(response.status,{207});const std::string xml(response.body.begin(),response.body.end());
   const std::regex href(R"(<(?:[A-Za-z_][\w.-]*:)?href\s*>\s*([^<]+))");std::set<std::string> names;
   for(std::sregex_iterator it(xml.begin(),xml.end(),href),end;it!=end;++it){auto name=(*it)[1].str();while(!name.empty()&&isspace(static_cast<unsigned char>(name.back())))name.pop_back();name=name.substr(name.find_last_of('/')+1);if(backup_name(name)){names.insert(name);if(names.size()>200)names.erase(names.begin());}}
   Json files=Json::array();for(auto it=names.rbegin();it!=names.rend();++it)files.push_back(*it);std::lock_guard lock(mutex);view["files"]=files;return;
  }
  if(operation=="cloud_backup"){
   const auto snapshot=capture();
   struct Cleanup{std::filesystem::path path,root;~Cleanup(){try{const auto actual=std::filesystem::weakly_canonical(path);if(actual.parent_path()==std::filesystem::weakly_canonical(root)/"snapshots"&&std::regex_match(actual.filename().string(),std::regex("snapshot-[0-9]+-[0-9]+"))){std::error_code error;std::filesystem::remove_all(actual,error);}}catch(...){}}} cleanup{snapshot,root};
   if(cancel)throw std::runtime_error("CLOUD_CANCELLED");auto plain=cloud_pack(snapshot);auto encrypted=cloud_encrypt(plain,credentials["encryption_password"]);SecureZeroMemory(plain.data(),plain.size());CloudBytes().swap(plain);
   const auto name="PulseTrace-"+std::to_string(now_ms())+"-"+identity()+"-v"+std::string(app_version)+".ptb";
   // Keep a usable encrypted local archive even when any network step fails.
   write(root/"archives"/name,encrypted);
   const auto expected_hash=cloud_sha256(encrypted);
   auto directory=cfg["server"].get<std::string>();auto encoded=remote.substr(directory.size());for(size_t at=0;(at=encoded.find('/',at))!=std::string::npos;++at){auto response=request(cfg,credentials,"MKCOL",directory+encoded.substr(0,at+1));http_ok(response.status,{201,405});}
   http_ok(request(cfg,credentials,"PUT",remote+name,std::move(encrypted),true).status,{201,204});auto verified=request(cfg,credentials,"GET",remote+name);http_ok(verified.status,{200});if(cloud_sha256(verified.body)!=expected_hash)throw std::runtime_error("CLOUD_UPLOAD_VERIFICATION_FAILED");
   record(operation,name,"verified");{std::lock_guard lock(mutex);view["last_backup_ms"]=now_ms();view["last_backup_name"]=name;}return;
  }
  if(operation=="cloud_prepare_restore"){
   const auto name=p.at("name").get<std::string>();if(!backup_name(name))throw std::runtime_error("CLOUD_BACKUP_NAME_INVALID");
   auto response=request(cfg,credentials,"GET",remote+name);http_ok(response.status,{200});auto plain=cloud_decrypt(response.body,credentials["encryption_password"]);const auto token=identity();Json manifest;
   try{manifest=cloud_unpack(plain,root/("restore-"+token));}catch(...){SecureZeroMemory(plain.data(),plain.size());discard_restore_stage(root,Json{{"token",token}});throw;}SecureZeroMemory(plain.data(),plain.size());
   std::lock_guard lock(mutex);discard_restore_stage(root,prepared);prepared={{"token",token},{"manifest",manifest},{"name",name}};view["restore_token"]=token;view["restore_version"]=manifest["version"];view["restore_created_ms"]=manifest["created_ms"];view["phase"]="restore_ready";return;
  }
  if(operation=="cloud_confirm_restore"){
   Json intent;{std::lock_guard lock(mutex);if(prepared.empty()||p.value("token","")!=prepared.value("token","")||p.value("confirmation","")!="RESTORE")throw std::runtime_error("CLOUD_RESTORE_CONFIRM_REQUIRED");intent=prepared;}
   const auto text=intent.dump();auto protected_data=protect({reinterpret_cast<const unsigned char*>(text.data()),text.size()});write(root/"pending.bin",protected_data,true);
   record(operation,intent.value("name",""),"restart_required");std::lock_guard lock(mutex);view["phase"]="restart_required";view.erase("restore_token");prepared=nullptr;return;
  }
  throw std::runtime_error("CLOUD_UNKNOWN_OPERATION");
 }
 void run(){
  for(;;){std::string operation;Json p,cfg;{std::unique_lock lock(mutex);wake.wait_for(lock,std::chrono::seconds(30),[&]{return stopping||!job.empty();});if(stopping)return;
   std::error_code storage_error;const bool pending=std::filesystem::exists(root/"pending.bin",storage_error);
   if(job.empty()&&prepared.empty()&&config.value("enabled",false)&&config.value("automatic",false)&&!pending&&!storage_error&&now_ms()-last_attempt>=config.value("interval_minutes",1440)*60000LL)job="cloud_backup";
   if(job.empty())continue;operation=std::exchange(job,{});p=std::move(params);cfg=config;busy=true;cancel=false;if(operation=="cloud_backup")last_attempt=now_ms();view["phase"]="working";view["operation"]=operation;view["error"]="";}
   try{if(operation=="cloud_backup")json_write(root/"last-attempt.json",{{"time_ms",now_ms()}});perform(operation,p,cfg);std::lock_guard lock(mutex);if(view["phase"]=="working")view["phase"]="success";}
   catch(const std::exception &e){const std::string text=e.what();const auto code=text.starts_with("CLOUD_")?text:"CLOUD_OPERATION_FAILED";record(operation,"",code);std::lock_guard lock(mutex);view["phase"]=cancel?"cancelled":"error";view["error"]=code;}
   {std::lock_guard lock(mutex);busy=false;}wake.notify_all();
  }
 }
};
CloudBackupService::CloudBackupService(std::filesystem::path d,std::function<std::filesystem::path()> capture,CloudTransport t,bool start):impl_(std::make_unique<Impl>(std::move(d),std::move(capture),std::move(t),start)){}
CloudBackupService::~CloudBackupService(){stop();}
void CloudBackupService::stop(){auto &s=*impl_;{std::lock_guard lock(s.mutex);s.stopping=true;s.cancel=true;}s.wake.notify_all();if(s.worker.joinable())s.worker.join();{std::lock_guard lock(s.mutex);std::error_code error;if(!std::filesystem::exists(s.root/"pending.bin",error)&&!error)discard_restore_stage(s.root,s.prepared);}}
Json CloudBackupService::status()const{auto &s=*impl_;std::lock_guard lock(s.mutex);auto out=s.view;out["config"]=s.config;out["busy"]=s.busy||!s.job.empty();std::error_code error;out["pending_restore"]=std::filesystem::exists(s.root/"pending.bin",error);try{const auto c=load_credentials(s.root/"credentials.bin");out["account"]=c.value("account","");out["credentials_saved"]=c.contains("app_password");out["encryption_password_saved"]=c.contains("encryption_password");}catch(...){out["credentials_saved"]=false;out["encryption_password_saved"]=false;}return out;}
Json CloudBackupService::command(const std::string &kind,const Json &p){auto &s=*impl_;if(kind=="cloud_status")return status();if(kind=="cloud_cancel"){std::lock_guard lock(s.mutex);s.cancel=true;if(!s.job.empty()){if(s.job=="cloud_backup"){s.last_attempt=now_ms();try{json_write(s.root/"last-attempt.json",{{"time_ms",s.last_attempt}});}catch(...){}}s.job.clear();s.view["phase"]="cancelled";}return {{"cancelled",true}};}
 std::lock_guard lock(s.mutex);if(s.busy||!s.job.empty())throw std::runtime_error("CLOUD_BUSY");if(s.stopping)throw std::runtime_error("CLOUD_STOPPED");
 if(kind=="cloud_cancel_restore"){try{auto bytes=unprotect(read(s.root/"pending.bin",2*1024*1024));auto intent=Json::parse(bytes);SecureZeroMemory(bytes.data(),bytes.size());discard_restore_stage(s.root,intent);}catch(...){}std::filesystem::remove(s.root/"pending.bin");discard_restore_stage(s.root,s.prepared);s.prepared=nullptr;s.view.erase("restore_token");s.view["phase"]="idle";return {{"cancelled",true}};}
 if(kind=="cloud_configure"){
  auto cfg=s.config;for(const auto *key:{"enabled","server","directory","automatic","interval_minutes"})if(p.contains(key))cfg[key]=p[key];validate_settings(cfg);
  auto c=load_credentials(s.root/"credentials.bin");const auto account=p.value("account",c.value("account",""));if(account.size()>320||std::any_of(account.begin(),account.end(),[](unsigned char c){return c<32||c==127;}))throw std::runtime_error("CLOUD_ACCOUNT_INVALID");
  if((cfg["server"]!=s.config["server"]||account!=c.value("account",""))&&p.value("app_password","").empty())c.erase("app_password");c["account"]=account;c["server"]=cfg["server"];
  for(const auto *key:{"app_password","encryption_password"})if(p.contains(key)&&!p[key].get<std::string>().empty()){const auto value=p[key].get<std::string>();if(value.size()>1024||value.find('\0')!=std::string::npos||(std::string(key)=="encryption_password"&&wide(value).size()<12))throw std::runtime_error("CLOUD_ENCRYPTION_PASSWORD_REQUIRED");c[key]=value;}
  if(cfg.value("enabled",false)&&(account.empty()||c.value("app_password","").empty()||c.value("encryption_password","").size()<12))throw std::runtime_error("CLOUD_CREDENTIALS_REQUIRED");
  const auto text=c.dump();auto encrypted=protect({reinterpret_cast<const unsigned char*>(text.data()),text.size()});write(s.root/"credentials.bin",encrypted,true);json_write(s.root/"settings.json",cfg);s.config=cfg;if(!std::filesystem::exists(s.root/"pending.bin"))discard_restore_stage(s.root,s.prepared);s.prepared=nullptr;s.view.erase("restore_token");s.view["phase"]=std::filesystem::exists(s.root/"pending.bin")?"restart_required":"idle";s.view["error"]="";s.wake.notify_all();return {{"saved",true}};
 }
 if(!s.worker.joinable())throw std::runtime_error("CLOUD_TEST_MODE_DISABLED");
 if(kind!="cloud_test"&&kind!="cloud_backup"&&kind!="cloud_list"&&kind!="cloud_prepare_restore"&&kind!="cloud_confirm_restore")throw std::runtime_error("CLOUD_UNKNOWN_OPERATION");
 if(std::filesystem::exists(s.root/"pending.bin"))throw std::runtime_error("CLOUD_RESTART_REQUIRED");
 if(kind=="cloud_backup"&&s.last_manual&&now_ms()-s.last_manual<60000)throw std::runtime_error("CLOUD_RATE_LIMITED");
 if(kind=="cloud_backup")s.last_manual=now_ms();s.job=kind;s.params=p;s.cancel=false;s.wake.notify_all();return {{"queued",true}};
}
}
