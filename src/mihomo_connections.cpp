#include <windows.h>
#include <winhttp.h>
#include <wincrypt.h>
#include "pulsetrace/traffic_rankings.hpp"
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <fstream>
#include <mutex>
#include <thread>

namespace pulsetrace {
namespace {
std::int64_t milliseconds(){return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();}
std::wstring wide(const std::string &s){int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),nullptr,0);if(!n&&!s.empty())throw std::invalid_argument("Invalid UTF-8");std::wstring w(n,L' ');MultiByteToWideChar(CP_UTF8,0,s.data(),static_cast<int>(s.size()),w.data(),n);return w;}
struct Internet {HINTERNET h{};~Internet(){if(h)WinHttpCloseHandle(h);}operator HINTERNET()const{return h;}};
struct Endpoint {std::wstring host;INTERNET_PORT port{};bool tls{};};
Endpoint endpoint(const std::string &url){
 if(url.size()>128)throw std::invalid_argument("Controller must be a loopback HTTP(S) origin with an explicit port");
 Endpoint e;std::string rest;if(url.starts_with("http://")){rest=url.substr(7);}else if(url.starts_with("https://")){rest=url.substr(8);e.tls=true;}else throw std::invalid_argument("Controller requires HTTP or HTTPS");
 const auto colon=rest.rfind(':');if(colon==std::string::npos)throw std::invalid_argument("Controller requires an explicit port");
 const auto host=rest.substr(0,colon),port=rest.substr(colon+1);
 if(host!="127.0.0.1"&&host!="[::1]"&&host!="localhost")throw std::invalid_argument("Only explicit loopback controllers are supported");
 if(port.empty()||port.size()>5||port.find_first_not_of("0123456789")!=std::string::npos)throw std::invalid_argument("Invalid controller port");
 const auto p=std::stoul(port);if(!p||p>65535)throw std::invalid_argument("Invalid controller port");e.port=static_cast<INTERNET_PORT>(p);e.host=host=="[::1]"?L"::1":L"127.0.0.1";return e;
}
Json read_connections(const std::string &origin,const std::string &secret){
 const auto e=endpoint(origin);Internet session{WinHttpOpen(L"PulseTrace/TrafficRankings",WINHTTP_ACCESS_TYPE_NO_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0)};
 if(!session.h||!WinHttpSetTimeouts(session,1500,1500,1500,1500))throw std::runtime_error("controller_transport");
 Internet connection{WinHttpConnect(session,e.host.c_str(),e.port,0)};if(!connection.h)throw std::runtime_error("controller_transport");
 Internet request{WinHttpOpenRequest(connection,L"GET",L"/connections",nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,e.tls?WINHTTP_FLAG_SECURE:0)};if(!request.h)throw std::runtime_error("controller_transport");
 DWORD redirects=WINHTTP_OPTION_REDIRECT_POLICY_NEVER,features=WINHTTP_DISABLE_COOKIES|WINHTTP_DISABLE_AUTHENTICATION;
 if(!WinHttpSetOption(request,WINHTTP_OPTION_REDIRECT_POLICY,&redirects,sizeof(redirects))||!WinHttpSetOption(request,WINHTTP_OPTION_DISABLE_FEATURE,&features,sizeof(features)))throw std::runtime_error("controller_transport");
 const auto auth=secret.empty()?std::wstring():L"Authorization: Bearer "+wide(secret)+L"\r\n";
 if(!WinHttpSendRequest(request,auth.empty()?WINHTTP_NO_ADDITIONAL_HEADERS:auth.c_str(),static_cast<DWORD>(auth.size()),WINHTTP_NO_REQUEST_DATA,0,0,0)||!WinHttpReceiveResponse(request,nullptr))throw std::runtime_error("controller_transport");
 DWORD status{},size=sizeof(status);if(!WinHttpQueryHeaders(request,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,WINHTTP_HEADER_NAME_BY_INDEX,&status,&size,WINHTTP_NO_HEADER_INDEX))throw std::runtime_error("controller_transport");
 if(status==401||status==403)throw std::runtime_error("controller_auth");if(status!=200)throw std::runtime_error("controller_http");
 std::string body;char chunk[16384];DWORD count{};const auto deadline=GetTickCount64()+4000;
 do{if(GetTickCount64()>deadline)throw std::runtime_error("controller_timeout");if(!WinHttpReadData(request,chunk,sizeof(chunk),&count))throw std::runtime_error("controller_transport");if(body.size()+count>8*1024*1024)throw std::runtime_error("controller_capacity");body.append(chunk,count);}while(count);
 return Json::parse(body,[](int depth,Json::parse_event_t,Json &){if(depth>24)throw std::runtime_error("controller_document");return true;});
}
std::string decrypt(const std::filesystem::path &path){
 std::error_code error;const auto length=std::filesystem::file_size(path,error);if(!error&&length>16384)throw std::runtime_error("credential_unavailable");std::ifstream f(path,std::ios::binary);if(!f)return {};std::string bytes((std::istreambuf_iterator<char>(f)),{});if(bytes.size()>16384)throw std::runtime_error("credential_unavailable");
 DATA_BLOB input{static_cast<DWORD>(bytes.size()),reinterpret_cast<BYTE*>(bytes.data())},output{};
 if(!CryptUnprotectData(&input,nullptr,nullptr,nullptr,nullptr,CRYPTPROTECT_UI_FORBIDDEN,&output))throw std::runtime_error("credential_unavailable");
 std::string plain(reinterpret_cast<char*>(output.pbData),output.cbData);SecureZeroMemory(output.pbData,output.cbData);LocalFree(output.pbData);return plain;
}
}
void validate_controller(const std::string &url){if(!url.empty())endpoint(url);}
struct TrafficRankings::Impl {
 std::filesystem::path data;mutable std::mutex mutex;std::condition_variable wake;std::thread worker;bool quit{},active{},native{},mihomo{},domains{};unsigned generation{};
 std::string controller,secret;RankingAccumulator bins;Json state=Json::object(),speed=Json::array();ProcessTrafficCapture capture;
 Impl(std::filesystem::path dir):data(std::move(dir)){try{secret=decrypt(data/"mihomo.secret");}catch(...){state["credential_error"]="credential_unavailable";}worker=std::thread([this]{run();});}
 void run(){
  MihomoDeltaTracker tracker;unsigned seen=~0u;std::int64_t prior=milliseconds();
  std::unique_lock lock(mutex);
  while(!quit){
   const auto revision=generation;const bool running=active,win=running&&native,mi=running&&mihomo&&!controller.empty(),record_domains=domains;const auto origin=controller,token=secret;
   const bool changed=revision!=seen;seen=revision;lock.unlock();RankingAccumulator batch;Json ws,ms;
   try{if(changed){capture.configure(false);capture.drain(batch);tracker.reset();}capture.configure(win);ws=capture.drain(batch);
    if(mi){auto document=read_connections(origin,token);ms=tracker.consume(document,milliseconds(),record_domains,batch);}else ms={{"state",!running?"paused":mihomo?"not_configured":"disabled"}};
   }catch(const std::exception &e){tracker.interrupt();const std::string code=e.what();ms={{"state",code.starts_with("controller_")?code:"controller_document"}};}
   const auto now=milliseconds();lock.lock();
   // A pause/configuration change invalidates the whole in-flight sample. Never
   // attribute bytes from a paused interval or from a different controller.
   if(revision==generation){
    speed=Json::array();const double seconds=std::max(0.001,(now-prior)/1000.0);
    for(const auto &r:batch.rows()){
     bins.add(r["minute"],r["source"],r["kind"],{r["key"],r["name"],r["path"],r["attribution"]},r["family"],r["upload"],r["download"],r["quality"]);
     auto rate=r;rate["up_bps"]=r["upload"].get<double>()/seconds;rate["down_bps"]=r["download"].get<double>()/seconds;rate["sample_ms"]=now;speed.push_back(std::move(rate));
    }
    state["windows_etw"]=ws;state["mihomo"]=ms;state["sample_ms"]=now;
   }
   prior=now;if(!win&&!mi)wake.wait(lock,[&]{return quit||generation!=seen;});else wake.wait_for(lock,std::chrono::seconds(2),[&]{return quit||generation!=seen;});
  }
  lock.unlock();capture.configure(false);RankingAccumulator final;capture.drain(final);lock.lock();
  // The ETW gate is already stopped; its already-observed queue is safe to flush.
  for(const auto &r:final.rows())bins.add(r["minute"],r["source"],r["kind"],{r["key"],r["name"],r["path"],r["attribution"]},r["family"],r["upload"],r["download"],r["quality"]);
 }
};
TrafficRankings::TrafficRankings(const std::filesystem::path &data):p_(std::make_unique<Impl>(data)){}
TrafficRankings::~TrafficRankings(){stop();}
void TrafficRankings::stop(){{std::lock_guard l(p_->mutex);p_->quit=true;p_->wake.notify_all();}if(p_->worker.joinable())p_->worker.join();}
void TrafficRankings::configure(const Json &c,bool running){std::lock_guard l(p_->mutex);const bool native=c.value("ranking_windows_enabled",false),mi=c.value("ranking_mihomo_enabled",false),domains=c.value("ranking_domains_enabled",false);const auto origin=c.value("mihomo_controller",std::string());
 if(p_->active==running&&p_->native==native&&p_->mihomo==mi&&p_->domains==domains&&p_->controller==origin)return;
 p_->active=running;p_->native=native;p_->mihomo=mi;p_->domains=domains;p_->controller=origin;p_->capture.gate(running&&native);++p_->generation;p_->speed=Json::array();p_->wake.notify_all();}
Json TrafficRankings::status()const{std::lock_guard l(p_->mutex);auto s=p_->state;s["monitoring"]=p_->active;s["domains_enabled"]=p_->domains;s["has_secret"]=!p_->secret.empty();s["reference_validated"]=false;s["sources_additive"]=false;s["capacity_events"]=p_->bins.capacity_events();return s;}
Json TrafficRankings::rows()const{std::lock_guard l(p_->mutex);return p_->bins.rows();}
Json TrafficRankings::rates()const{std::lock_guard l(p_->mutex);return p_->active&&milliseconds()-p_->state.value("sample_ms",0LL)<6000?p_->speed:Json::array();}
void TrafficRankings::persisted_before(std::int64_t minute){std::lock_guard l(p_->mutex);p_->bins.discard_before(minute);}
void TrafficRankings::clear(bool domains){std::lock_guard l(p_->mutex);if(domains)p_->bins.clear_domains();else p_->bins.clear();p_->speed=Json::array();++p_->generation;p_->wake.notify_all();}
void TrafficRankings::set_secret(const std::string &secret){
 if(secret.size()>1024||std::any_of(secret.begin(),secret.end(),[](unsigned char c){return c<32||c==127;}))throw std::invalid_argument("Invalid controller secret");
 const auto file=p_->data/"mihomo.secret",temp=p_->data/"mihomo.secret.tmp";
 if(secret.empty()){std::error_code error;std::filesystem::remove(file,error);if(error)throw std::runtime_error("credential_write");}
 else{DATA_BLOB in{static_cast<DWORD>(secret.size()),reinterpret_cast<BYTE*>(const_cast<char*>(secret.data()))},out{};
  if(!CryptProtectData(&in,L"PulseTrace Mihomo controller",nullptr,nullptr,nullptr,CRYPTPROTECT_UI_FORBIDDEN,&out))throw std::runtime_error("credential_write");
  std::ofstream f(temp,std::ios::binary);f.write(reinterpret_cast<char*>(out.pbData),out.cbData);LocalFree(out.pbData);f.close();if(!f||!MoveFileExW(temp.c_str(),file.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("credential_write");}
 std::lock_guard l(p_->mutex);if(!p_->secret.empty())SecureZeroMemory(p_->secret.data(),p_->secret.size());p_->secret=secret;++p_->generation;p_->wake.notify_all();
}
}
