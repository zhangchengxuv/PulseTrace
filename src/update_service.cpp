#include "pulsetrace/update.hpp"
#include "pulsetrace/version.hpp"
#include <windows.h>
#include <winhttp.h>
#include <wintrust.h>
#include <softpub.h>
#include <wincrypt.h>
#include <bcrypt.h>
#include <array>
#include <fstream>
#include <future>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#ifndef PULSETRACE_PUBLISHER_SHA256
#define PULSETRACE_PUBLISHER_SHA256 ""
#endif
namespace pulsetrace {
namespace {
std::wstring wide(const std::string &s){int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),nullptr,0);if(n<=0)throw std::runtime_error("Invalid UTF-8 URL");std::wstring w(n,0);MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),w.data(),n);return w;}
std::string utf8(const std::wstring &s){int n=WideCharToMultiByte(CP_UTF8,0,s.data(),static_cast<int>(s.size()),nullptr,0,nullptr,nullptr);std::string r(n,0);WideCharToMultiByte(CP_UTF8,0,s.data(),static_cast<int>(s.size()),r.data(),n,nullptr,nullptr);return r;}
struct Internet {HINTERNET p{};explicit Internet(HINTERNET h):p(h){if(!p)throw std::runtime_error("WinHTTP error "+std::to_string(GetLastError()));}~Internet(){WinHttpCloseHandle(p);}Internet(const Internet&)=delete;};
using Sink=std::function<void(const char*,std::size_t)>;
void receive(std::string url,std::uint64_t limit,const std::atomic<bool> &cancel,const Sink &sink){
 Internet session(WinHttpOpen(L"PulseTrace/" PULSETRACE_VERSION,WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0));
 WinHttpSetTimeouts(session.p,5000,5000,8000,8000);
 const auto deadline=GetTickCount64()+10*60*1000;
 for(int redirects=0;redirects<=5;redirects++){
  if(cancel)throw std::runtime_error("Update cancelled");if(!trusted_update_url(url,redirects>0))throw std::runtime_error("Untrusted update URL or redirect");
  const auto w=wide(url);URL_COMPONENTS parts{};parts.dwStructSize=sizeof(parts);parts.dwHostNameLength=parts.dwUrlPathLength=parts.dwExtraInfoLength=static_cast<DWORD>(-1);
  if(!WinHttpCrackUrl(w.c_str(),0,0,&parts)||parts.nScheme!=INTERNET_SCHEME_HTTPS||parts.nPort!=443)throw std::runtime_error("HTTPS required");
  const std::wstring host(parts.lpszHostName,parts.dwHostNameLength),path=std::wstring(parts.lpszUrlPath,parts.dwUrlPathLength)+std::wstring(parts.lpszExtraInfo,parts.dwExtraInfoLength);
  Internet connection(WinHttpConnect(session.p,host.c_str(),443,0));Internet request(WinHttpOpenRequest(connection.p,L"GET",path.c_str(),nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,WINHTTP_FLAG_SECURE));
  DWORD policy=WINHTTP_OPTION_REDIRECT_POLICY_NEVER;WinHttpSetOption(request.p,WINHTTP_OPTION_REDIRECT_POLICY,&policy,sizeof(policy));
  const wchar_t *headers=L"Accept: application/json, application/octet-stream;q=0.9\r\n";
  if(!WinHttpSendRequest(request.p,headers,static_cast<DWORD>(-1),WINHTTP_NO_REQUEST_DATA,0,0,0)||!WinHttpReceiveResponse(request.p,nullptr))throw std::runtime_error("Network/proxy/TLS error "+std::to_string(GetLastError()));
  DWORD status=0,length=sizeof(status);WinHttpQueryHeaders(request.p,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,WINHTTP_HEADER_NAME_BY_INDEX,&status,&length,WINHTTP_NO_HEADER_INDEX);
  if(status==301||status==302||status==303||status==307||status==308){DWORD bytes=0;WinHttpQueryHeaders(request.p,WINHTTP_QUERY_LOCATION,WINHTTP_HEADER_NAME_BY_INDEX,nullptr,&bytes,WINHTTP_NO_HEADER_INDEX);if(!bytes||bytes>16384)throw std::runtime_error("Invalid update redirect");std::wstring location(bytes/sizeof(wchar_t),0);if(!WinHttpQueryHeaders(request.p,WINHTTP_QUERY_LOCATION,WINHTTP_HEADER_NAME_BY_INDEX,location.data(),&bytes,WINHTTP_NO_HEADER_INDEX))throw std::runtime_error("Missing redirect");location.resize(wcslen(location.c_str()));url=utf8(location);continue;}
  if(status!=200)throw std::runtime_error("Release server HTTP "+std::to_string(status));
  std::array<char,65536> buffer{};std::uint64_t total=0;DWORD read=0;
  do{if(cancel)throw std::runtime_error("Update cancelled");if(GetTickCount64()>deadline)throw std::runtime_error("Update request timed out");if(!WinHttpReadData(request.p,buffer.data(),static_cast<DWORD>(buffer.size()),&read))throw std::runtime_error("Download interrupted "+std::to_string(GetLastError()));total+=read;if(total>limit)throw std::runtime_error("Response exceeds expected size");if(read)sink(buffer.data(),read);}while(read);return;
 }
 throw std::runtime_error("Too many update redirects");
}
std::string hex(const BYTE *bytes,std::size_t size){std::ostringstream o;for(std::size_t i=0;i<size;i++)o<<std::hex<<std::setw(2)<<std::setfill('0')<<static_cast<unsigned>(bytes[i]);return o.str();}
bool pe64(const std::filesystem::path &file){std::ifstream in(file,std::ios::binary);IMAGE_DOS_HEADER dos{};in.read(reinterpret_cast<char*>(&dos),sizeof(dos));if(!in||dos.e_magic!=IMAGE_DOS_SIGNATURE||dos.e_lfanew<64||dos.e_lfanew>1024*1024)return false;in.seekg(dos.e_lfanew);IMAGE_NT_HEADERS64 nt{};in.read(reinterpret_cast<char*>(&nt),sizeof(nt));return in&&nt.Signature==IMAGE_NT_SIGNATURE&&nt.FileHeader.Machine==IMAGE_FILE_MACHINE_AMD64&&nt.OptionalHeader.Magic==IMAGE_NT_OPTIONAL_HDR64_MAGIC;}
}
std::string sha256_file(const std::filesystem::path &file){
 BCRYPT_ALG_HANDLE algorithm{};BCRYPT_HASH_HANDLE hash{};if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)throw std::runtime_error("SHA-256 unavailable");
 try{DWORD size=0,got=0;if(BCryptGetProperty(algorithm,BCRYPT_OBJECT_LENGTH,reinterpret_cast<BYTE*>(&size),sizeof(size),&got,0)<0)throw std::runtime_error("SHA-256 state unavailable");std::vector<BYTE> object(size);if(BCryptCreateHash(algorithm,&hash,object.data(),size,nullptr,0,0)<0)throw std::runtime_error("Cannot create SHA-256");std::ifstream in(file,std::ios::binary);if(!in)throw std::runtime_error("Cannot read downloaded file");std::array<char,65536> buffer{};while(in){in.read(buffer.data(),buffer.size());auto n=in.gcount();if(n&&BCryptHashData(hash,reinterpret_cast<BYTE*>(buffer.data()),static_cast<ULONG>(n),0)<0)throw std::runtime_error("SHA-256 failed");}if(!in.eof())throw std::runtime_error("File read failed");BYTE digest[32]{};if(BCryptFinishHash(hash,digest,32,0)<0)throw std::runtime_error("SHA-256 finalization failed");BCryptDestroyHash(hash);hash=nullptr;BCryptCloseAlgorithmProvider(algorithm,0);return hex(digest,32);}catch(...){if(hash)BCryptDestroyHash(hash);BCryptCloseAlgorithmProvider(algorithm,0);throw;}
}
bool trusted_installer(const std::filesystem::path &file,const std::string &pin){
 if(pin.size()!=64||!pe64(file))return false;
 WINTRUST_FILE_INFO info{};info.cbStruct=sizeof(info);info.pcwszFilePath=file.c_str();WINTRUST_DATA data{};data.cbStruct=sizeof(data);data.dwUIChoice=WTD_UI_NONE;data.fdwRevocationChecks=WTD_REVOKE_WHOLECHAIN;data.dwUnionChoice=WTD_CHOICE_FILE;data.pFile=&info;data.dwStateAction=WTD_STATEACTION_VERIFY;data.dwProvFlags=WTD_REVOCATION_CHECK_CHAIN_EXCLUDE_ROOT;
 GUID action=WINTRUST_ACTION_GENERIC_VERIFY_V2;const LONG trust=WinVerifyTrust(nullptr,&action,&data);bool valid=false;
 if(trust==ERROR_SUCCESS){auto *provider=WTHelperProvDataFromStateData(data.hWVTStateData);auto *signer=provider?WTHelperGetProvSignerFromChain(provider,0,FALSE,0):nullptr;if(signer&&signer->csCertChain&&signer->pasCertChain[0].pCert){BYTE digest[32]{};DWORD n=sizeof(digest);if(CertGetCertificateContextProperty(signer->pasCertChain[0].pCert,CERT_SHA256_HASH_PROP_ID,digest,&n)&&n==32)valid=hex(digest,n)==pin;}}
 data.dwStateAction=WTD_STATEACTION_CLOSE;WinVerifyTrust(nullptr,&action,&data);return valid;
}
UpdateService::UpdateService(std::filesystem::path data,Transport transport):transport_(std::move(transport)),data_(std::move(data)),state_({{"phase","idle"},{"busy",false},{"current",app_version},{"last_check_ms",0},{"publisher_pinned",std::string(PULSETRACE_PUBLISHER_SHA256).size()==64}}){
 try{std::ifstream in(data_/"updates"/"last-check.json");Json previous;if(in>>previous)state_["last_check_ms"]=previous.value("last_check_ms",0LL);}catch(...){}
}
UpdateService::~UpdateService(){cancel();if(worker_.joinable())worker_.join();}
Json UpdateService::status()const{std::lock_guard lock(mutex_);return state_;}
void UpdateService::patch(Json values){std::lock_guard lock(mutex_);for(auto it=values.begin();it!=values.end();++it)state_[it.key()]=it.value();}
void UpdateService::read(const std::string &url,std::uint64_t limit,const Sink &sink){
 if(!trusted_update_url(url))throw std::runtime_error("Untrusted repository URL");std::uint64_t received=0;
 const auto bounded=[&](const char *s,std::size_t n){if(cancelled_)throw std::runtime_error("Update cancelled");received+=n;if(received>limit)throw std::runtime_error("Response exceeds expected size");sink(s,n);};
 if(transport_)transport_(url,limit,cancelled_,bounded);else receive(url,limit,cancelled_,bounded);
}
std::string UpdateService::fetch(const std::string &url,const std::atomic<bool> &,std::uint64_t limit){std::string body;read(url,limit,[&](const char *s,std::size_t n){body.append(s,n);});return body;}
void UpdateService::cancel(){cancelled_=true;}
void UpdateService::start(std::function<void()> work){
 {std::lock_guard lock(mutex_);if(state_.value("busy",false))throw std::runtime_error("Update operation already running");state_["busy"]=true;state_["error"]="";}
 if(worker_.joinable())worker_.join();cancelled_=false;
 worker_=std::thread([this,work=std::move(work)]{try{work();}catch(const std::exception &e){patch({{"phase",cancelled_?"cancelled":"error"},{"error",e.what()}});}patch({{"busy",false}});});
}
void UpdateService::check(std::string channel,std::string source){
 if((channel!="stable"&&channel!="preview")||(source!="auto"&&source!="github"&&source!="gitee"))throw std::invalid_argument("Invalid update source/channel");
 start([this,channel,source]{patch({{"phase","checking"},{"latest",""},{"downloaded",false},{"can_install",false},{"providers",Json::array()}});
  {std::lock_guard lock(mutex_);candidates_.clear();installer_.clear();}
  struct Result{std::optional<UpdateAsset> asset;Json report;};
  auto query=[&](const std::string &provider){const auto begin=GetTickCount64();Result result;result.report={{"source",provider}};try{
   if(provider=="github")result.asset=github_release(Json::parse(fetch("https://api.github.com/repos/zhangchengxuv/PulseTrace/releases?per_page=100",cancelled_)),channel);
   else result.asset=gitee_release(Json::parse(fetch("https://gitee.com/api/v5/repos/zhangchenxuv/pulse-trace/releases?per_page=100",cancelled_)),channel,[&](std::int64_t id){return Json::parse(fetch("https://gitee.com/api/v5/repos/zhangchenxuv/pulse-trace/releases/"+std::to_string(id)+"/attach_files?per_page=100",cancelled_));});
   if(result.asset)result.asset->sha256=parse_checksum(fetch(result.asset->checksum_url,cancelled_,4096),result.asset->name);
   result.report["ok"]=true;result.report["version"]=result.asset?result.asset->version:"";
  }catch(const std::exception &e){result.asset.reset();result.report["ok"]=false;result.report["error"]=e.what();}const auto latency=GetTickCount64()-begin;result.report["elapsed_ms"]=latency;if(result.asset)result.asset->latency_ms=latency;return result;};
  std::vector<std::future<Result>> requests;if(source!="gitee")requests.push_back(std::async(std::launch::async,query,"github"));if(source!="github")requests.push_back(std::async(std::launch::async,query,"gitee"));
  Json providers=Json::array();std::vector<UpdateAsset> assets;unsigned success=0;for(auto &f:requests){auto r=f.get();if(r.asset)assets.push_back(*r.asset);if(r.report.value("ok",false))success++;providers.push_back(r.report);}patch({{"providers",providers}});if(cancelled_)throw std::runtime_error("Update cancelled");if(!success)throw std::runtime_error("All selected release sources failed; see source diagnostics");
  auto selected=choose_updates(std::move(assets),app_version);const auto now=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();patch({{"last_check_ms",now},{"partial_sources",success<requests.size()}});
  std::filesystem::create_directories(data_/"updates");{std::ofstream out(data_/"updates"/"last-check.json");out<<Json{{"last_check_ms",now}};}
  if(selected.empty()){patch({{"phase","up_to_date"}});return;}
  const auto a=selected.front();{std::lock_guard lock(mutex_);candidates_=std::move(selected);}patch({{"phase","available"},{"latest",a.version},{"notes",a.notes},{"size",a.size},{"sha256",a.sha256},{"source",a.source}});
 });
}
void UpdateService::download(){
 std::vector<UpdateAsset> selected;{std::lock_guard lock(mutex_);selected=candidates_;}if(selected.empty())throw std::runtime_error("Check updates first");
 start([this,selected]{patch({{"phase","downloading"},{"received",0},{"downloaded",false},{"can_install",false}});
  std::filesystem::create_directories(data_/"updates");std::string errors;
  for(const auto &a:selected){if(cancelled_)throw std::runtime_error("Update cancelled");const auto partial=data_/"updates"/(a.name+".part");
   try{if(std::filesystem::space(data_/"updates").available<a.size+16*1024*1024)throw std::runtime_error("Insufficient disk space");std::ofstream out(partial,std::ios::binary|std::ios::trunc);if(!out)throw std::runtime_error("Cannot create download file");std::uint64_t n=0;ULONGLONG last=0;read(a.url,a.size,[&](const char *s,std::size_t length){out.write(s,static_cast<std::streamsize>(length));if(!out)throw std::runtime_error("Download write failed");n+=length;if(GetTickCount64()-last>200){patch({{"received",n},{"size",a.size},{"source",a.source}});last=GetTickCount64();}});out.close();if(!out||n!=a.size||sha256_file(partial)!=a.sha256||!pe64(partial))throw std::runtime_error("Installer size/hash/PE64 validation failed");
    const auto final=data_/"updates"/a.name;if(!MoveFileExW(partial.c_str(),final.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("Cannot finalize installer download");const bool trusted=trusted_installer(final,PULSETRACE_PUBLISHER_SHA256);
    {std::lock_guard lock(mutex_);installer_=final;}patch({{"phase",trusted?"ready":"manual_install"},{"received",n},{"downloaded",true},{"can_install",trusted},{"identity",trusted?"pinned_authenticode":"publisher_not_verified"}});return;
   }catch(const std::exception &e){std::error_code ignored;std::filesystem::remove(partial,ignored);errors+=a.source+": "+e.what()+"; ";if(cancelled_)break;}
  }throw std::runtime_error(errors.empty()?"No matching download source":errors);
 });
}
std::filesystem::path UpdateService::verified_installer()const{std::filesystem::path file;std::string digest;{std::lock_guard lock(mutex_);if(!state_.value("can_install",false)||state_.value("busy",false)||candidates_.empty())throw std::runtime_error("Publisher identity is not verified; manual installation required");file=installer_;digest=candidates_.front().sha256;}if(sha256_file(file)!=digest||!trusted_installer(file,PULSETRACE_PUBLISHER_SHA256))throw std::runtime_error("Installer changed or publisher verification failed");return file;}
std::string UpdateService::release_page()const{std::lock_guard lock(mutex_);return candidates_.empty()?"https://github.com/zhangchengxuv/PulseTrace/releases":candidates_.front().page;}
}
