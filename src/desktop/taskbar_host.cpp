#include "pulsetrace/desktop/taskbar_host.hpp"
#include "pulsetrace/protocol.hpp"
#include "pulsetrace/localization.hpp"
#include <sddl.h>
#include <vector>
#include <chrono>
#include <array>
#include <algorithm>
namespace pulsetrace {
TaskbarWidgetHost::TaskbarWidgetHost(HWND owner,const std::filesystem::path &directory):owner_(owner){
 try{
  HANDLE token{};if(!OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token))throw std::runtime_error("Cannot identify widget IPC owner");
  DWORD bytes=0;GetTokenInformation(token,TokenUser,nullptr,0,&bytes);std::vector<BYTE> storage(bytes);const bool ok=GetTokenInformation(token,TokenUser,storage.data(),bytes,&bytes)!=0;CloseHandle(token);if(!ok)throw std::runtime_error("Cannot read widget owner SID");
  LPWSTR sid{};if(!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER *>(storage.data())->User.Sid,&sid))throw std::runtime_error("Cannot encode widget owner SID");
  const auto acl=L"D:P(A;;GA;;;"+std::wstring(sid)+L")";LocalFree(sid);PSECURITY_DESCRIPTOR descriptor{};
  if(!ConvertStringSecurityDescriptorToSecurityDescriptorW(acl.c_str(),SDDL_REVISION_1,&descriptor,nullptr))throw std::runtime_error("Cannot secure widget IPC");
  SECURITY_ATTRIBUTES attributes{sizeof(attributes),descriptor,FALSE};mapping_=CreateFileMappingW(INVALID_HANDLE_VALUE,&attributes,PAGE_READWRITE,0,sizeof(WidgetShared),widget_mapping_name(GetCurrentProcessId()).c_str());const auto last=GetLastError();LocalFree(descriptor);
  if(!mapping_||last==ERROR_ALREADY_EXISTS)throw std::runtime_error("Widget IPC mapping unavailable");
  shared_=static_cast<WidgetShared *>(MapViewOfFile(mapping_,FILE_MAP_ALL_ACCESS,0,0,sizeof(WidgetShared)));if(!shared_)throw std::runtime_error("Cannot map widget IPC");new(shared_) WidgetShared{};
  WidgetSnapshot initial;initial.owner_pid=GetCurrentProcessId();initial.owner_window=reinterpret_cast<UINT_PTR>(owner_);initial.tick=GetTickCount64();publish_snapshot(shared_->data_sequence,shared_->data,initial);
  const auto exe=directory/L"PulseTraceWidget.exe";auto command=quote_windows_argument(exe.wstring())+L" --owner "+std::to_wstring(GetCurrentProcessId());
  STARTUPINFOW start{};start.cb=sizeof(start);PROCESS_INFORMATION process{};if(!CreateProcessW(exe.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,directory.c_str(),&start,&process))throw std::runtime_error("Widget executable could not start");CloseHandle(process.hThread);process_=process.hProcess;
 }catch(const std::exception &e){error_=e.what();stop();}
}
TaskbarWidgetHost::~TaskbarWidgetHost(){stop();}
void TaskbarWidgetHost::stop() noexcept{if(shared_){WidgetSnapshot s;s.owner_pid=GetCurrentProcessId();s.tick=GetTickCount64();s.flags=0x80000000;publish_snapshot(shared_->data_sequence,shared_->data,s);}if(process_){WaitForSingleObject(process_,4000);CloseHandle(process_);process_=nullptr;}if(shared_){UnmapViewOfFile(shared_);shared_=nullptr;}if(mapping_){CloseHandle(mapping_);mapping_=nullptr;}}
void TaskbarWidgetHost::update(const Json &status){if(!shared_)return;WidgetSnapshot s;s.owner_pid=GetCurrentProcessId();s.owner_window=reinterpret_cast<UINT_PTR>(owner_);s.tick=GetTickCount64();const auto cfg=status.value("config",Json::object());const auto mode=cfg.value("taskbar_mode","embedded");s.mode=mode=="floating"?1:mode=="tray"?2:0;s.monitor=cfg.value("taskbar_monitor",0u);
 s.language=effective_language(cfg.value("language","system"))=="zh-CN"?1:0;
 const std::array<std::string,4> fonts={"Segoe UI Variable Text","Segoe UI","Microsoft YaHei UI","Consolas"};const auto font=cfg.value("taskbar_font",fonts[0]);s.font=static_cast<unsigned>(std::find(fonts.begin(),fonts.end(),font)-fonts.begin());
 s.font_size=cfg.value("taskbar_font_size",10u);const auto unit=cfg.value("taskbar_unit","auto");s.unit=unit=="bytes"?1:unit=="kib"?2:unit=="mib"?3:0;s.precision=cfg.value("taskbar_precision",1u);s.offset_x=cfg.value("taskbar_offset_x",0);s.offset_y=cfg.value("taskbar_offset_y",0);
 const auto color=[&](const char *key,const char *fallback){const auto value=std::stoul(cfg.value(key,std::string(fallback)).substr(1),nullptr,16);return RGB((value>>16)&255,(value>>8)&255,value&255);};s.upload_color=color("taskbar_upload_color","#3498db");s.download_color=color("taskbar_download_color","#8bc4ea");
 s.background_alpha=cfg.value("taskbar_background","transparent")=="subtle"?32:0;
 const auto n=status.value("network",Json::object());s.upload=n.value("upload_bps",0.0);s.download=n.value("download_bps",0.0);
 if(status.value("monitoring",false))s.flags|=1;const auto selected=n.value("selected","");
 const auto heartbeat=status.value("heartbeat_ms",0LL);
 if(!(s.flags&1)||selected!=last_interface_){stable_samples_=0;last_heartbeat_=0;last_interface_=selected;}
 if(heartbeat!=last_heartbeat_){last_heartbeat_=heartbeat;stable_samples_=std::min(2u,stable_samples_+1);}
 if(stable_samples_<2)s.flags|=8;
 if(n.contains("interfaces"))for(const auto &i:n["interfaces"])if(i.value("id","")==selected&&i.value("physical",false)&&i.value("up",false))s.flags|=2;
 if(!status.value("network_error","").empty()||!valid_snapshot(s))s.flags|=4;
 const auto now=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();if(now-status.value("heartbeat_ms",0LL)>5000)s.flags|=8;
 publish_snapshot(shared_->data_sequence,shared_->data,s);
}
Json TaskbarWidgetHost::status() const{if(!shared_)return {{"mode","tray"},{"reason",error_}};if(!process_||WaitForSingleObject(process_,0)!=WAIT_TIMEOUT)return {{"mode","tray"},{"reason","Native widget process is not running"},{"helper_running",false}};WidgetFeedback f;if(!read_snapshot(shared_->feedback_sequence,shared_->feedback,f)||f.version!=widget_protocol||f.mode>2)return {{"mode","unknown"}};f.reason[159]=0;const auto reason=std::filesystem::path(f.reason).u8string();return {{"mode",f.mode==0?"embedded":f.mode==1?"floating":"tray"},{"hwnd",f.hwnd},{"parent",f.parent},{"visibility",f.visibility},{"paint_count",f.paint_count},{"text_samples",f.text_samples},{"screen_matches",f.screen_matches},{"transparent_pixels",f.transparent_pixels},{"opaque_pixels",f.opaque_pixels},{"partial_pixels",f.partial_pixels},{"background_alpha",f.background_alpha},{"attempts",f.attempts},{"error",f.win32_error},{"reason",std::string(reason.begin(),reason.end())},{"helper_running",true}};}
}
