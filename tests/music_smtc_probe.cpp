#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Media.Control.h>
#include <iostream>
#include <string>
#include "pulsetrace/music.hpp"
#include <windows.h>
#include <psapi.h>
#include <thread>
// Read-only capability probe. Deliberately omit song/account metadata from output.
int main(int argc,char **argv){
 try{
  winrt::init_apartment(winrt::apartment_type::multi_threaded);
  auto manager=winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionManager::RequestAsync().get();
  unsigned qq=0,netease=0;
  for(auto const &s:manager.GetSessions()){
   auto id=winrt::to_string(s.SourceAppUserModelId());
   for(auto &c:id)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
   const auto player=pulsetrace::music_player(id);const bool q=player=="qq",n=player=="netease";
   if(!q&&!n)continue;
   q?++qq:++netease;
   auto media=s.TryGetMediaPropertiesAsync().get();auto t=s.GetTimelineProperties();
   std::cout<<(q?"QQ Music":"NetEase Cloud Music")<<": title="<<!media.Title().empty()<<" artist="<<!media.Artist().empty()
    <<" status="<<static_cast<int>(s.GetPlaybackInfo().PlaybackStatus())<<" timeline="<<(t.EndTime()>t.StartTime())<<'\n';
  }
  std::cout<<"QQ sessions="<<qq<<"; NetEase sessions="<<netease<<"\nAbsent sessions do not prove client incompatibility. No player controls were invoked.\n";
  if(argc>1&&std::string(argv[1])=="--monitor"){
   pulsetrace::MusicMonitor monitor;monitor.configure({{"music_enabled",true},{"music_qq",true},{"music_netease",true}},true);
   std::this_thread::sleep_for(std::chrono::seconds(2));
   const auto cpu=[](){FILETIME created{},exit{},kernel{},user{};GetProcessTimes(GetCurrentProcess(),&created,&exit,&kernel,&user);ULARGE_INTEGER k{},u{};k.LowPart=kernel.dwLowDateTime;k.HighPart=kernel.dwHighDateTime;u.LowPart=user.dwLowDateTime;u.HighPart=user.dwHighDateTime;return k.QuadPart+u.QuadPart;};
   const auto start=GetTickCount64(),before=cpu();SIZE_T peak=0;
   for(int i=0;i<30;i++){std::this_thread::sleep_for(std::chrono::milliseconds(500));PROCESS_MEMORY_COUNTERS_EX m{};m.cb=sizeof(m);GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&m),sizeof(m));peak=std::max(peak,m.PrivateUsage);}
   auto status=monitor.status();const bool detected=!monitor.rows().empty();monitor.stop();
   SYSTEM_INFO info{};GetSystemInfo(&info);std::cout<<"Monitor error="<<!status["error"].get<std::string>().empty()<<" supported_session="<<!status["sessions"].empty()<<" detected_aggregates="<<detected<<" sample_ms="<<GetTickCount64()-start<<" cpu_total_machine_pct="<<(cpu()-before)/10000.0/(GetTickCount64()-start)/info.dwNumberOfProcessors*100<<" private_peak_mib="<<peak/(1024.0*1024)<<"\n";
  }
  return 0;
 }catch(winrt::hresult_error const &e){std::cerr<<"SMTC unavailable: "<<std::hex<<static_cast<unsigned>(e.code())<<'\n';return 1;}
}
