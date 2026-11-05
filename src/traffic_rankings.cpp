#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <winnls.h>
#include "pulsetrace/traffic_rankings.hpp"
#include <algorithm>
#include <limits>
#include <set>
#include <sstream>
namespace pulsetrace {
namespace {
std::string lower(std::string s){for(auto &c:s)if(c>='A'&&c<='Z')c=static_cast<char>(c-'A'+'a');return s;}
bool sane(const std::string &s,std::size_t maximum){return s.size()<=maximum&&std::none_of(s.begin(),s.end(),[](unsigned char c){return c<32||c==127;});}
std::uint64_t number(const Json &j,const char *key){const auto &v=j.at(key);if(!v.is_number_integer()||v.get<double>()<0||v.get<double>()>9007199254740991.0)throw std::invalid_argument("Invalid connection byte counter");return v.get<std::uint64_t>();}
// Mihomo serializes TrackerInfo.start as RFC3339Nano. Only a verified start
// after our previous snapshot permits counting a newly observed ID from zero.
std::int64_t started_ms(const std::string &s){
 if(s.size()<20||s[4]!='-'||s[7]!='-'||s[10]!='T'||s[13]!=':'||s[16]!=':')return -1;
 const auto field=[&](unsigned at,unsigned count){int n=0;for(unsigned i=at;i<at+count;++i){if(s[i]<'0'||s[i]>'9')return -1;n=n*10+s[i]-'0';}return n;};
 SYSTEMTIME t{};const int year=field(0,4),month=field(5,2),day=field(8,2),hour=field(11,2),minute=field(14,2),second=field(17,2);if(year<1970||month<1||month>12||day<1||day>31||hour<0||hour>23||minute<0||minute>59||second<0||second>59)return -1;
 t.wYear=static_cast<WORD>(year);t.wMonth=static_cast<WORD>(month);t.wDay=static_cast<WORD>(day);t.wHour=static_cast<WORD>(hour);t.wMinute=static_cast<WORD>(minute);t.wSecond=static_cast<WORD>(second);
 std::size_t at=19;if(s[at]=='.'){++at;unsigned digits=0;int ms=0;while(at<s.size()&&s[at]>='0'&&s[at]<='9'){if(digits<3)ms=ms*10+s[at]-'0';++digits;++at;}if(!digits||digits>9)return -1;for(;digits<3;++digits)ms*=10;t.wMilliseconds=static_cast<WORD>(ms);}
 int offset=0;if(at+1==s.size()&&s[at]=='Z'){}else if(at+6==s.size()&&(s[at]=='+'||s[at]=='-')&&s[at+3]==':'){const auto h=field(static_cast<unsigned>(at+1),2),m=field(static_cast<unsigned>(at+4),2);if(h<0||h>23||m<0||m>59)return -1;offset=(h*60+m)*(s[at]=='+'?1:-1);}else return -1;
 FILETIME f{};if(!SystemTimeToFileTime(&t,&f))return -1;SYSTEMTIME verified{};FileTimeToSystemTime(&f,&verified);if(verified.wDay!=t.wDay||verified.wMonth!=t.wMonth)return -1;const auto ticks=(std::uint64_t(f.dwHighDateTime)<<32)|f.dwLowDateTime;return static_cast<std::int64_t>((ticks-116444736000000000ULL)/10000)-offset*60000LL;
}
}
RankingIdentity ranking_identity(const std::string &raw,const std::string &reported){
 if(!sane(raw,2048)||!sane(reported,256))return {"unknown","","","unknown"};
 auto path=raw;std::replace(path.begin(),path.end(),'/', '\\');
 if(path.size()>3&&isalpha(static_cast<unsigned char>(path[0]))&&path[1]==':'&&path[2]=='\\'){
  auto lexical=std::filesystem::path(std::u8string(path.begin(),path.end())).lexically_normal().u8string();path.assign(lexical.begin(),lexical.end());
  const auto leaf=path.substr(path.find_last_of('\\')+1);return {"exe:"+lower(path),leaf,path,lower(leaf)=="svchost.exe"?"shared_service_process":"executable_path"};
 }
 if(!reported.empty())return {"name:"+lower(reported),reported,"","reported_name_only"};
 return {"unknown","","","unknown"};
}
bool process_lifetime_contains(std::uint64_t birth,std::uint64_t exit,std::uint64_t first,std::uint64_t last)noexcept{return birth&&first<=last&&birth<=first&&(!exit||exit>=last);}
std::string ranking_domain(const std::string &raw){
 if(!sane(raw,1024)||raw.empty()||raw.find_first_of("/@:?#[\\] ")!=std::string::npos)return {};
 auto host=raw;if(host.back()=='.')host.pop_back();
 IN_ADDR ipv4{};IN6_ADDR ipv6{};if(InetPtonA(AF_INET,host.c_str(),&ipv4)==1||InetPtonA(AF_INET6,host.c_str(),&ipv6)==1)return {};
 const int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,host.data(),static_cast<int>(host.size()),nullptr,0);if(n<=0)return {};
 std::wstring wide(n,L' ');MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,host.data(),static_cast<int>(host.size()),wide.data(),n);
 wchar_t ascii[254]{};const int length=IdnToAscii(IDN_USE_STD3_ASCII_RULES,wide.data(),n,ascii,254);if(length<=0||length>253)return {};
 host.clear();for(int i=0;i<length;++i){if(ascii[i]>127)return {};host.push_back(static_cast<char>(ascii[i]));}host=lower(host);
 std::istringstream labels(host);std::string label;while(std::getline(labels,label,'.'))if(label.empty()||label.size()>63||label.front()=='-'||label.back()=='-'||label.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789-")!=std::string::npos)return {};
 return host;
}
void RankingAccumulator::add(std::int64_t minute,const std::string &source,const std::string &kind,const RankingIdentity &identity,int family,std::uint64_t up,std::uint64_t down,unsigned quality){
 if(minute<0||(family!=0&&family!=4&&family!=6)||(!up&&!down))return;
 auto id=identity;std::string key=std::to_string(minute)+"/"+source+"/"+kind+"/"+std::to_string(family)+"/"+id.key;
 if(!bins_.contains(key)&&bins_.size()>=32768){++capacity_events_;id={"overflow","","","capacity_overflow"};quality|=ranking_capacity;key=std::to_string(minute)+"/"+source+"/"+kind+"/"+std::to_string(family)+"/overflow";if(!bins_.contains(key)&&bins_.size()>=32800)return;}
 auto &row=bins_[key];if(row.is_null())row={{"minute",minute},{"source",source},{"kind",kind},{"key",id.key},{"name",id.name},{"path",id.path},{"attribution",id.attribution},{"family",family},{"upload",0ULL},{"download",0ULL},{"quality",0u}};
 const auto limit=static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
 for(auto [field,value]:{std::pair{"upload",up},std::pair{"download",down}}){const auto prior=row[field].get<std::uint64_t>();if(value>limit-prior){row[field]=limit;quality|=ranking_capacity;}else row[field]=prior+value;}
 row["quality"]=row["quality"].get<unsigned>()|quality;
}
Json RankingAccumulator::rows()const{Json result=Json::array();for(const auto &[key,row]:bins_)result.push_back(row);return result;}
void RankingAccumulator::discard_before(std::int64_t minute){for(auto it=bins_.begin();it!=bins_.end();)if(it->second["minute"].get<std::int64_t>()<minute)it=bins_.erase(it);else ++it;}
void RankingAccumulator::clear_domains(){for(auto it=bins_.begin();it!=bins_.end();)if(it->second["kind"]=="domain")it=bins_.erase(it);else ++it;}
void RankingAccumulator::clear(){bins_.clear();capacity_events_=0;}
void MihomoDeltaTracker::reset(){previous_.clear();last_ms_=0;upload_total_=download_total_=0;gap_=true;}
void MihomoDeltaTracker::interrupt(){gap_=true;}
Json MihomoDeltaTracker::consume(const Json &doc,std::int64_t time,bool domains,RankingAccumulator &out){
 // Mihomo uses a nil slice (JSON null) when there are no live connections.
 const Json empty=Json::array();const auto &connections=doc.at("connections").is_null()?empty:doc.at("connections");if(!connections.is_array()||connections.size()>16384)throw std::invalid_argument("Connection snapshot exceeds capacity");
 const auto upload=number(doc,"uploadTotal"),download=number(doc,"downloadTotal");
 bool restart=last_ms_&&(upload<upload_total_||download<download_total_);
 const bool first=!last_ms_,gap=gap_||(!first&&(time<=last_ms_||time-last_ms_>10000));
 unsigned resets=0,baseline=0,unknown=0;std::map<std::string,Connection> next;RankingAccumulator pending;
 for(const auto &connection:connections){
  const auto id=connection.at("id").get<std::string>();if(id.empty()||!sane(id,256)||next.contains(id))throw std::invalid_argument("Invalid or duplicate connection ID");
  const auto up=number(connection,"upload"),down=number(connection,"download");const auto &meta=connection.at("metadata");
  const auto path=meta.value("processPath",std::string()),process=meta.value("process",std::string()),host=ranking_domain(meta.value("host",std::string()));
  const auto identity=ranking_identity(path,process);const auto destination=meta.value("destinationIP",std::string());if(!sane(destination,128)||!sane(meta.value("network",std::string()),32))throw std::invalid_argument("Invalid connection metadata");IN_ADDR v4{};IN6_ADDR v6{};const int family=InetPtonA(AF_INET,destination.c_str(),&v4)==1?4:InetPtonA(AF_INET6,destination.c_str(),&v6)==1?6:0;
  const auto start=connection.value("start",std::string());if(!sane(start,128))throw std::invalid_argument("Invalid connection start");
  const auto fingerprint=start+"|"+identity.key+"|"+host+"|"+destination+"|"+meta.value("network",std::string());
  next[id]={up,down,fingerprint};auto old=previous_.find(id);
  const bool fresh=old==previous_.end();const auto started=started_ms(start);
  const bool new_during_observation=!first&&!restart&&fresh&&started>last_ms_&&started<=time;
  if(first||restart||start.empty()||(!new_during_observation&&(fresh||old->second.fingerprint!=fingerprint))){++baseline;continue;}
  const bool reset=!fresh&&(up<old->second.up||down<old->second.down);if(reset)++resets;
  // A counter reset is not a fresh transfer. Re-baseline independently per direction.
  const auto du=fresh?up:up>=old->second.up?up-old->second.up:0,dd=fresh?down:down>=old->second.down?down-old->second.down:0;
  const unsigned quality=ranking_sampled|(gap||restart?ranking_gap:0)|(reset?ranking_reset:0)|(identity.attribution=="unknown"||identity.attribution=="reported_name_only"?ranking_unresolved:0);
  pending.add(time/60000,"mihomo","app",identity,family,du,dd,quality);
  if(domains){if(host.empty())++unknown;pending.add(time/60000,"mihomo","domain",{host.empty()?"unknown":host,host,"",host.empty()?"unknown":"mihomo_metadata_host"},family,du,dd,quality|(host.empty()?ranking_unresolved:0));}
 }
 unsigned vanished=0;for(const auto &[id,c]:previous_)if(!next.contains(id))++vanished;
 for(const auto &r:pending.rows())out.add(r["minute"],r["source"],r["kind"],{r["key"],r["name"],r["path"],r["attribution"]},r["family"],r["upload"],r["download"],r["quality"].get<unsigned>()|(vanished?ranking_gap:0));
 previous_=std::move(next);last_ms_=time;upload_total_=upload;download_total_=download;gap_=false;
 return {{"state","observing"},{"quality","sampled_incomplete"},{"connections",previous_.size()},{"baseline_connections",baseline},{"vanished_connections",vanished},{"counter_resets",resets},{"gap",gap},{"core_restart",restart},{"unknown_domains",unknown},{"last_sample_ms",time},{"closed_tail_observed",false}};
}
}
