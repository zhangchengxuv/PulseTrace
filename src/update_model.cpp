#include "pulsetrace/update.hpp"
#include <algorithm>
#include <cctype>
#include <sstream>
#include <stdexcept>
namespace pulsetrace {
namespace {
std::vector<std::string> split(const std::string &s, char delimiter){std::vector<std::string> r;std::size_t a=0;do{auto b=s.find(delimiter,a);r.push_back(s.substr(a,b-a));if(b==s.npos)break;a=b+1;}while(true);return r;}
bool numeric(const std::string &s){return !s.empty()&&s.find_first_not_of("0123456789")==s.npos;}
int number_compare(const std::string &a,const std::string &b){if(a.size()!=b.size())return a.size()<b.size()?-1:1;return a==b?0:a<b?-1:1;}
std::string version_tag(const Json &r){auto v=r.at("tag_name").get<std::string>();if(v.starts_with('v'))v.erase(0,1);SemVersion::parse(v);return v;}
bool eligible(const Json &r,const std::string &channel){if(r.value("draft",false))return false;const auto v=SemVersion::parse(version_tag(r));return channel=="preview"||(!r.value("prerelease",false)&&v.pre.empty());}
UpdateAsset asset(const Json &r,const Json &files,const std::string &source){
 UpdateAsset a;a.source=source;a.version=version_tag(r);a.name="PulseTrace-Setup-v"+a.version+"-x64.exe";
 a.notes=r.value("body",std::string());if(a.notes.size()>65536)a.notes.resize(65536);
 a.page=source=="github"?"https://github.com/zhangchengxuv/PulseTrace/releases/tag/v"+a.version:"https://gitee.com/zhangchenxuv/pulse-trace/releases/tag/v"+a.version;
 if(!files.is_array())throw std::runtime_error("Invalid release attachments");
 for(const auto &f:files){if(!f.is_object())continue;const auto n=f.value("name",std::string());
  if(n==a.name){if(!a.url.empty())throw std::runtime_error("Duplicate installer attachment");a.url=f.at("browser_download_url");a.size=f.at("size").get<std::uint64_t>();}
  if(n==a.name+".sha256"){if(!a.checksum_url.empty())throw std::runtime_error("Duplicate checksum attachment");a.checksum_url=f.at("browser_download_url");}
 }
 if(a.size<1024||a.size>512ULL*1024*1024||!trusted_update_url(a.url)||!trusted_update_url(a.checksum_url))throw std::runtime_error("Release has no valid x64 installer/checksum pair");
 return a;
}
template<class F>std::optional<UpdateAsset> release(const Json &list,const std::string &channel,F files,const std::string &source){
 if(channel!="stable"&&channel!="preview")throw std::invalid_argument("Invalid update channel");
 if(!list.is_array()||list.size()>100)throw std::runtime_error("Invalid release response");
 const Json *latest=nullptr;
 for(const auto &r:list){try{if(!eligible(r,channel))continue;if(!latest||SemVersion::parse(version_tag(r)).compare(SemVersion::parse(version_tag(*latest)))>0)latest=&r;}catch(const std::exception &){continue;}}
 if(!latest)return {};return asset(*latest,files(*latest),source);
}
}
SemVersion SemVersion::parse(std::string text){
 if(text.empty()||text.size()>128)throw std::invalid_argument("Invalid semantic version");
 auto plus=text.find('+');if(plus!=text.npos){auto build=text.substr(plus+1);for(const auto &id:split(build,'.'))if(id.empty()||id.find_first_not_of("0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ-")!=id.npos)throw std::invalid_argument("Invalid build metadata");text.resize(plus);}
 SemVersion v;auto dash=text.find('-');if(dash!=text.npos){v.pre=split(text.substr(dash+1),'.');text.resize(dash);for(const auto &id:v.pre)if(id.empty()||id.find_first_not_of("0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ-")!=id.npos||(numeric(id)&&id.size()>1&&id[0]=='0'))throw std::invalid_argument("Invalid prerelease");}
 v.numbers=split(text,'.');if(v.numbers.size()!=3)throw std::invalid_argument("Version needs major.minor.patch");for(const auto &n:v.numbers)if(!numeric(n)||(n.size()>1&&n[0]=='0'))throw std::invalid_argument("Invalid version number");return v;
}
int SemVersion::compare(const SemVersion &o)const{for(int i=0;i<3;i++){int c=number_compare(numbers[i],o.numbers[i]);if(c)return c;}if(pre.empty()||o.pre.empty())return pre.empty()==o.pre.empty()?0:pre.empty()?1:-1;for(std::size_t i=0;i<std::min(pre.size(),o.pre.size());i++){if(pre[i]==o.pre[i])continue;bool a=numeric(pre[i]),b=numeric(o.pre[i]);if(a&&b)return number_compare(pre[i],o.pre[i]);if(a!=b)return a?-1:1;return pre[i]<o.pre[i]?-1:1;}return pre.size()==o.pre.size()?0:pre.size()<o.pre.size()?-1:1;}
std::optional<UpdateAsset> github_release(const Json &r,const std::string &channel){return release(r,channel,[](const Json &v){return v.at("assets");},"github");}
std::optional<UpdateAsset> gitee_release(const Json &r,const std::string &channel,const std::function<Json(std::int64_t)> &attachments){return release(r,channel,[&](const Json &v){const auto id=v.at("id").get<std::int64_t>();if(id<=0)throw std::runtime_error("Invalid Gitee release ID");return attachments(id);},"gitee");}
std::string parse_checksum(const std::string &text,const std::string &filename){if(text.size()>4096)throw std::runtime_error("Checksum file too large");std::istringstream input(text);std::string hash,name,extra;input>>hash>>name;if(name.starts_with('*'))name.erase(0,1);if(hash.size()!=64||hash.find_first_not_of("0123456789abcdefABCDEF")!=hash.npos||name!=filename||input>>extra)throw std::runtime_error("Invalid SHA-256 manifest");std::transform(hash.begin(),hash.end(),hash.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});return hash;}
std::vector<UpdateAsset> choose_updates(std::vector<UpdateAsset> assets,const std::string &current){
 const auto installed=SemVersion::parse(current);assets.erase(std::remove_if(assets.begin(),assets.end(),[&](const auto &a){return SemVersion::parse(a.version).compare(installed)<=0;}),assets.end());
 std::stable_sort(assets.begin(),assets.end(),[](const auto &a,const auto &b){const auto c=SemVersion::parse(a.version).compare(SemVersion::parse(b.version));return c?c>0:a.latency_ms<b.latency_ms;});if(assets.empty())return assets;
 const auto top=assets.front();assets.erase(std::remove_if(assets.begin(),assets.end(),[&](const auto &a){return a.version!=top.version;}),assets.end());
 for(const auto &a:assets)if(a.sha256.size()!=64||a.sha256!=top.sha256||a.size!=top.size||a.name!=top.name)throw std::runtime_error("Release sources disagree on installer size/SHA-256; update blocked");return assets;
}
bool trusted_update_url(const std::string &url,bool redirect){
 if(url.size()>4096||url.find_first_of("\r\n\\")!=url.npos||!url.starts_with("https://"))return false;
 auto end=url.find('/',8);const auto host=url.substr(8,end-8);if(host.find_first_of("@:#%")!=host.npos||end==url.npos)return false;
 const auto path=url.substr(end);
 if(host=="api.github.com")return path.starts_with("/repos/zhangchengxuv/PulseTrace/releases");
 if(host=="github.com")return path.starts_with("/zhangchengxuv/PulseTrace/releases/");
 if(host=="gitee.com")return path.starts_with("/api/v5/repos/zhangchenxuv/pulse-trace/releases")||path.starts_with("/zhangchenxuv/pulse-trace/releases/");
 // A redirect is accepted only after an HTTPS request to the fixed repository origin.
 return redirect&&(host=="release-assets.githubusercontent.com"||host=="objects.githubusercontent.com"||host=="gitee-release-file.oss-cn-beijing.aliyuncs.com");
}
}
