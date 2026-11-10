#include "pulsetrace/music.hpp"
#include <windows.h>
#include <algorithm>
#include <cwctype>
#include <set>
namespace pulsetrace {
namespace {
std::string normalized(const std::string &text){
 if(text.empty())return {};
 int count=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),nullptr,0);if(!count)return {};
 std::wstring wide(count,0);MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),wide.data(),count);
 count=NormalizeString(NormalizationKC,wide.data(),static_cast<int>(wide.size()),nullptr,0);
 if(count>0){std::wstring n(count,0);const int size=NormalizeString(NormalizationKC,wide.data(),static_cast<int>(wide.size()),n.data(),count);if(size>0){n.resize(size);wide=std::move(n);}}
 std::wstring clean;bool space=false;for(wchar_t c:wide){if(iswspace(c)){space=!clean.empty();continue;}if(space)clean+=L' ';space=false;clean+=static_cast<wchar_t>(towlower(c));}
 count=WideCharToMultiByte(CP_UTF8,0,clean.data(),static_cast<int>(clean.size()),nullptr,0,nullptr,nullptr);std::string result(count,0);WideCharToMultiByte(CP_UTF8,0,clean.data(),static_cast<int>(clean.size()),result.data(),count,nullptr,nullptr);return result;
}
}
std::string music_player(std::string source){
 std::transform(source.begin(),source.end(),source.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
 // Exact executable / packaged app identities, never enumerate files or processes.
 if(source=="qqmusic.exe"||source=="qqmusic"||source.starts_with("tencent.qqmusic_")||source.starts_with("tencenttechnology(shenzhen)co.ltd.qqmusic_"))return "qq";
 if(source=="cloudmusic.exe"||source=="cloudmusic"||source.starts_with("netease.cloudmusic_"))return "netease";
 return {};
}
std::string music_identity(const MusicObservation &s){
 // SMTC exposes no reliable song ID. Keep album, track and exact reported duration
 // to distinguish versions. Missing artist is deliberately session-scoped.
 return Json::array({normalized(s.title),normalized(s.artist),normalized(s.album),s.track,s.duration_ms,(s.artist.empty()||s.album.empty()||s.duration_ms<=0)?s.session:""}).dump();
}
void MusicTimeline::observe(const std::vector<MusicObservation> &values,std::uint64_t tick,std::int64_t utc,unsigned threshold){
 std::set<std::string> seen;
 for(const auto &s:values){
  if(s.session.empty()||s.title.empty()||(s.player!="qq"&&s.player!="netease")||!seen.insert(s.session).second)continue;
  const auto key=music_identity(s);auto it=sessions_.find(s.session);
  if(it==sessions_.end()||it->second.key!=key||it->second.song.player!=s.player){sessions_[s.session]={s,key,tick,0,utc,false};continue;}
  auto &p=it->second;const auto dt=tick>=p.tick?tick-p.tick:0;const auto wall=utc-p.utc;
  if(dt==0&&wall==0){if(!s.playing||!p.song.playing)p.continuous=0;p.song=s;continue;}
  const bool bounded=dt>0&&dt<=7500&&wall>=0&&std::abs(wall-static_cast<std::int64_t>(dt))<=1500;
  bool loop=false,seek=false;
  if(s.position_ms>=0&&p.song.position_ms>=0&&bounded){
   const auto delta=s.position_ms-p.song.position_ms;
   loop=s.duration_ms>0&&p.song.position_ms>=s.duration_ms-7500&&s.position_ms<=7500&&delta< -10000;
   seek=delta< -1000||delta>static_cast<std::int64_t>(dt)+2500;
  }
  if(loop){p.continuous=0;p.counted=false;}
  if(!bounded||!s.playing||!p.song.playing||seek){p.continuous=0;}
  else{
   // Credit monotonic elapsed time, never the position jump or a suspend gap.
   const auto before=p.continuous;p.continuous+=dt;
   std::int64_t start=utc-static_cast<std::int64_t>(dt);auto remaining=dt;
   const bool qualifies=!p.counted&&p.continuous>=static_cast<std::uint64_t>(threshold)*1000;
   const auto qualifies_at=start+static_cast<std::int64_t>(static_cast<std::uint64_t>(threshold)*1000-std::min(before,static_cast<std::uint64_t>(threshold)*1000));
   while(remaining){const auto minute=start/60000;const auto part=std::min<std::uint64_t>(remaining,60000-start%60000);const auto id=Json::array({minute,s.player,key}).dump();
    auto &r=buckets_[id];if(r.is_null())r={{"minute",minute},{"player",s.player},{"key",key},{"title",s.title},{"artist",s.artist},{"album",s.album},{"duration_ms",s.duration_ms},{"played_ms",0},{"plays",0}};
    r["played_ms"]=r["played_ms"].get<std::int64_t>()+part;
    if(qualifies&&!p.counted&&qualifies_at<=start+static_cast<std::int64_t>(part)){r["plays"]=r["plays"].get<int>()+1;p.counted=true;}
    start+=part;remaining-=part;
   }
  }
  p.song=s;p.tick=tick;p.utc=utc;
 }
 for(auto it=sessions_.begin();it!=sessions_.end();)if(!seen.contains(it->first))it=sessions_.erase(it);else ++it;
}
void MusicTimeline::reset(){for(auto &[id,s]:sessions_){s.tick=0;s.continuous=0;s.song.playing=false;}}
void MusicTimeline::clear(){sessions_.clear();buckets_.clear();}
Json MusicTimeline::rows()const{Json r=Json::array();for(const auto &[key,v]:buckets_)r.push_back(v);return r;}
void MusicTimeline::persisted_before(std::int64_t minute){for(auto it=buckets_.begin();it!=buckets_.end();)if(it->second["minute"].get<std::int64_t>()<minute)it=buckets_.erase(it);else ++it;}
}
