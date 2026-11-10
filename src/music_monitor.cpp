#include "pulsetrace/music.hpp"
#include "pulsetrace/database.hpp"
#include "pulsetrace/raw_input_host.hpp"
#include <windows.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Media.Control.h>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <set>
namespace pulsetrace {
namespace {
using namespace winrt::Windows::Media::Control;
struct Signal {std::mutex mutex;std::condition_variable cv;bool dirty{true},stop{};void wake(){std::lock_guard l(mutex);dirty=true;cv.notify_one();}};
template<class T>auto bounded(T operation){if(operation.wait_for(std::chrono::seconds(2))!=winrt::Windows::Foundation::AsyncStatus::Completed){operation.Cancel();throw std::runtime_error("SMTC_TIMEOUT");}return operation.GetResults();}
struct Session {
 GlobalSystemMediaTransportControlsSession value{nullptr};
 std::string identity;
 winrt::event_token media{},playback{},timeline{};
 MusicObservation cached;
 ~Session(){try{value.MediaPropertiesChanged(media);value.PlaybackInfoChanged(playback);value.TimelinePropertiesChanged(timeline);}catch(...) {}}
};
}
struct MusicMonitor::Impl {
 mutable std::mutex mutex;MusicTimeline model;Json current=Json::array();std::string error;bool active{},opted{},qq{true},netease{true};unsigned threshold{30};
 std::string run_id=std::to_string(utc_now_ms())+"-"+std::to_string(GetCurrentProcessId());std::uint64_t next_session{};
 std::atomic<unsigned> generation{};std::shared_ptr<Signal> signal=std::make_shared<Signal>();std::thread thread;
 void run(){
  try{winrt::init_apartment(winrt::apartment_type::multi_threaded);}catch(...){std::lock_guard l(mutex);error="SMTC_UNAVAILABLE";return;}
  GlobalSystemMediaTransportControlsSessionManager manager{nullptr};winrt::event_token changed{};
  std::map<std::uintptr_t,std::unique_ptr<Session>> sessions;unsigned last_generation=0;
  for(;;){
   {std::unique_lock l(signal->mutex);signal->cv.wait_for(l,std::chrono::seconds(5),[&]{return signal->dirty||signal->stop;});if(signal->stop)break;signal->cv.wait_for(l,std::chrono::milliseconds(250),[&]{return signal->stop;});if(signal->stop)break;signal->dirty=false;}
   bool enabled,optin,q,n;unsigned limit,epoch=generation.load();{std::lock_guard l(mutex);enabled=active;optin=opted;q=qq;n=netease;limit=threshold;if(epoch!=last_generation){model.reset();last_generation=epoch;}}
   // Keep identity bindings across temporary pause/lock/suspend without querying
   // metadata. Releasing them would turn resume into a spurious new play.
   if(!enabled){if(!optin){sessions.clear();if(manager){try{manager.SessionsChanged(changed);}catch(...){}manager=nullptr;}}std::lock_guard l(mutex);current=Json::array();continue;}
   try{
    if(!manager){manager=bounded(GlobalSystemMediaTransportControlsSessionManager::RequestAsync());auto wake=signal;changed=manager.SessionsChanged([wake](auto&&,auto&&){wake->wake();});}
    std::set<std::uintptr_t> seen;std::vector<MusicObservation> observations;Json now=Json::array();bool failed=false;
    for(const auto &value:manager.GetSessions()){
     const auto player=music_player(winrt::to_string(value.SourceAppUserModelId()));if(player.empty()||(player=="qq"&&!q)||(player=="netease"&&!n))continue;
     if(seen.size()>=16){failed=true;break;}
     const auto id=reinterpret_cast<std::uintptr_t>(winrt::get_abi(value));seen.insert(id);
     auto &entry=sessions[id];if(!entry){entry=std::make_unique<Session>();entry->value=value;entry->identity=run_id+"-"+std::to_string(++next_session);auto wake=signal;auto handler=[wake](auto&&,auto&&){wake->wake();};entry->media=value.MediaPropertiesChanged(handler);entry->playback=value.PlaybackInfoChanged(handler);entry->timeline=value.TimelinePropertiesChanged(handler);}
     try{
      auto properties=bounded(value.TryGetMediaPropertiesAsync());auto info=value.GetPlaybackInfo();
      MusicObservation song; song.session=entry->identity;song.player=player;song.title=winrt::to_string(properties.Title());song.artist=winrt::to_string(properties.Artist());song.album=winrt::to_string(properties.AlbumTitle());song.track=properties.TrackNumber();
      // Bound untrusted metadata; never read thumbnails, account or private files.
      if(song.title.size()>1024||song.artist.size()>1024||song.album.size()>1024)continue;
      try{auto time=value.GetTimelineProperties();if(time.EndTime()>time.StartTime()){song.duration_ms=(time.EndTime()-time.StartTime()).count()/10000;song.position_ms=(time.Position()-time.StartTime()).count()/10000;}}catch(...){}
      song.playing=info.PlaybackStatus()==GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing;
      entry->cached=song;
      observations.push_back(song);now.push_back({{"player",player},{"title",song.title},{"artist",song.artist},{"playing",song.playing},{"position_ms",song.position_ms},{"duration_ms",song.duration_ms}});
     }catch(...){failed=true;if(!entry->cached.title.empty()){auto missing=entry->cached;missing.playing=false;observations.push_back(missing);}}
    }
    for(auto it=sessions.begin();it!=sessions.end();)if(!seen.contains(it->first))it=sessions.erase(it);else ++it;
    std::lock_guard l(mutex);if(epoch==generation.load()&&active){model.observe(observations,GetTickCount64(),utc_now_ms(),limit);current=std::move(now);error=failed?"SMTC_PARTIAL":"";}
   }catch(...){sessions.clear();if(manager){try{manager.SessionsChanged(changed);}catch(...){}manager=nullptr;}std::lock_guard l(mutex);model.reset();current=Json::array();error="SMTC_UNAVAILABLE";}
  }
  sessions.clear();if(manager)try{manager.SessionsChanged(changed);}catch(...){}manager=nullptr;winrt::uninit_apartment();
 }
};
MusicMonitor::MusicMonitor():impl_(std::make_unique<Impl>()){}
MusicMonitor::~MusicMonitor(){stop();}
void MusicMonitor::configure(const Json &c,bool monitoring){
 auto &p=*impl_;bool changed=false;
 {std::lock_guard l(p.mutex);const bool active=monitoring&&c.value("music_enabled",false),qq=c.value("music_qq",true),netease=c.value("music_netease",true);const unsigned limit=c.value("music_threshold_seconds",30);
  const bool opted=c.value("music_enabled",false);changed=p.active!=active||p.opted!=opted||p.qq!=qq||p.netease!=netease||p.threshold!=limit;p.active=active;p.opted=opted;p.qq=qq;p.netease=netease;p.threshold=limit;
  if(changed){++p.generation;p.model.reset();p.current=Json::array();}if(active&&!p.thread.joinable())p.thread=std::thread([&p]{p.run();});}
 if(changed)p.signal->wake();
}
void MusicMonitor::invalidate()noexcept{auto &p=*impl_;++p.generation;try{std::lock_guard l(p.mutex);p.active=false;p.model.reset();p.current=Json::array();}catch(...){}p.signal->wake();}
void MusicMonitor::stop(){auto &p=*impl_;{std::lock_guard l(p.signal->mutex);p.signal->stop=true;p.signal->cv.notify_all();}if(p.thread.joinable())p.thread.join();}
void MusicMonitor::clear(){auto &p=*impl_;std::lock_guard l(p.mutex);++p.generation;p.model.clear();p.current=Json::array();}
Json MusicMonitor::rows()const{std::lock_guard l(impl_->mutex);return impl_->model.rows();}
Json MusicMonitor::status()const{std::lock_guard l(impl_->mutex);return {{"active",impl_->active},{"error",impl_->error},{"sessions",impl_->current},{"source","Windows SMTC"}};}
void MusicMonitor::persisted_before(std::int64_t minute){std::lock_guard l(impl_->mutex);impl_->model.persisted_before(minute);}
}
