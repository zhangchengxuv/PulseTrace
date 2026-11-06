#include "pulsetrace/app_usage.hpp"
#include <chrono>
#include <windows.h>
#include <wtsapi32.h>
#include <algorithm>
#include <atomic>
#include <filesystem>
#include <mutex>
#include <thread>
namespace pulsetrace {
void UsageTimeline::observe(const UsageObservation &s,unsigned idle_seconds){
    if(!has_previous_){anchor_tick_=s.tick;anchor_utc_=s.utc_ms;}
    if(has_previous_&&s.tick>previous_.tick){
        const auto elapsed=s.tick-previous_.tick;
        const auto wall=s.utc_ms-previous_.utc_ms;
        const auto mapped_now=anchor_utc_+static_cast<std::int64_t>(s.tick-anchor_tick_);
        if(elapsed>2500||std::abs(wall-static_cast<std::int64_t>(elapsed))>1500||std::abs(s.utc_ms-mapped_now)>1500){
            ++gaps_;anchor_tick_=s.tick;anchor_utc_=s.utc_ms;
        }
        else if(previous_.valid&&!previous_.identity.empty()){
            const auto a=previous_.tick,b=s.tick,threshold=static_cast<std::uint64_t>(idle_seconds)*1000;
            // Union of the previously known activity interval and a newly observed input.
            const auto old_end=std::clamp(previous_.last_input+threshold,a,b);
            const bool new_input=s.last_input>previous_.last_input&&s.last_input<=b;
            const auto new_begin=new_input?std::clamp(s.last_input,a,b):b;
            const auto new_end=new_input?std::clamp(s.last_input+threshold,new_begin,b):b;
            for(auto begin=a;begin<b;){
                // Map one monotonic segment with one UTC anchor. Re-anchoring at every
                // sample creates overlapping minute fragments under ordinary tick jitter.
                const auto utc=anchor_utc_+static_cast<std::int64_t>(begin-anchor_tick_);
                if(utc<0){++gaps_;break;}
                if(utc<last_credited_utc_){++gaps_;begin+=std::min(b-begin,static_cast<std::uint64_t>(last_credited_utc_-utc));continue;}
                const auto end=std::min(b,begin+static_cast<std::uint64_t>(60000-utc%60000));
                const auto overlap=[](auto x,auto y,auto lo,auto hi){return std::min(y,hi)>std::max(x,lo)?std::min(y,hi)-std::max(x,lo):0ULL;};
                auto active=s.last_input<previous_.last_input?0ULL:overlap(begin,end,a,old_end)+overlap(begin,end,new_begin,new_end)-overlap(begin,end,new_begin,std::min(old_end,new_end));
                auto key=std::make_pair(utc/60000,previous_.identity);
                if(!buckets_.contains(key)&&buckets_.size()>=20000){++gaps_;break;}
                auto &r=buckets_[key];if(r.is_null())r={{"minute",key.first},{"key",key.second},{"name",previous_.name},{"path",previous_.path},{"foreground_ms",0ULL},{"active_ms",0ULL}};
                r["foreground_ms"]=r["foreground_ms"].get<std::uint64_t>()+end-begin;
                r["active_ms"]=r["active_ms"].get<std::uint64_t>()+std::min(active,end-begin);
                last_credited_utc_=utc+static_cast<std::int64_t>(end-begin);begin=end;
            }
        }
    }
    previous_=s;has_previous_=true;
}
Json UsageTimeline::rows()const{Json result=Json::array();for(const auto &[k,r]:buckets_)result.push_back(r);return result;}
void UsageTimeline::persisted_before(std::int64_t minute){for(auto it=buckets_.begin();it!=buckets_.end()&&it->first.first<minute;)it=buckets_.erase(it);}
namespace {
std::string utf8(const std::wstring &s){const auto n=WideCharToMultiByte(CP_UTF8,0,s.data(),static_cast<int>(s.size()),nullptr,0,nullptr,nullptr);std::string out(n,'\0');WideCharToMultiByte(CP_UTF8,0,s.data(),static_cast<int>(s.size()),out.data(),n,nullptr,nullptr);return out;}
bool desktop_active(){
    DWORD session=0,size=0;LPWSTR memory=nullptr;bool active=false;
    if(ProcessIdToSessionId(GetCurrentProcessId(),&session)&&WTSQuerySessionInformationW(WTS_CURRENT_SERVER_HANDLE,session,WTSSessionInfoEx,&memory,&size)){
        if(size>=sizeof(WTSINFOEXW)){const auto *p=reinterpret_cast<WTSINFOEXW*>(memory);active=p->Level==1&&p->Data.WTSInfoExLevel1.SessionState==WTSActive&&p->Data.WTSInfoExLevel1.SessionFlags==WTS_SESSIONSTATE_UNLOCK;}
        WTSFreeMemory(memory);
    }return active;
}
}
struct AppUsageMonitor::Impl {
    mutable std::mutex mutex;UsageTimeline timeline;Json config=Json::object();bool enabled{},session{},suspended{};
    std::thread thread;std::atomic<DWORD> thread_id{};std::atomic<bool> quitting{};std::string error;bool event_ready{};
    HWINEVENTHOOK hook{};bool wts{};ULONGLONG notification_check{};
    DWORD cached_pid{};HANDLE process{};std::string identity,name,path;
    static thread_local Impl *current;
    ~Impl(){if(process)CloseHandle(process);}
    void identify(HWND window){
        DWORD pid=0;GetWindowThreadProcessId(window,&pid);
        if(pid&&pid==cached_pid&&process&&WaitForSingleObject(process,0)==WAIT_TIMEOUT)return;
        if(process){CloseHandle(process);process=nullptr;}cached_pid=0;identity.clear();name.clear();path.clear();
        if(!pid)return;
        process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,pid);
        FILETIME created{},exited{},kernel{},user{};wchar_t buffer[32768];DWORD length=32768;
        if(!process||!GetProcessTimes(process,&created,&exited,&kernel,&user)||!QueryFullProcessImageNameW(process,0,buffer,&length)){
            identity="unknown";name="";return;
        }
        std::wstring canonical(buffer,length);path=utf8(canonical);name=utf8(std::filesystem::path(canonical).stem().wstring());
        // An open live process handle and creation-time check prevent PID reuse attribution.
        CharLowerBuffW(canonical.data(),static_cast<DWORD>(canonical.size()));identity=utf8(canonical);cached_pid=pid;
    }
    void sample(){
        std::lock_guard lock(mutex);UsageObservation s; s.tick=GetTickCount64();s.utc_ms=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        LASTINPUTINFO info{};info.cbSize=sizeof(info);const bool input=GetLastInputInfo(&info)!=FALSE;
        const auto age=static_cast<DWORD>(static_cast<DWORD>(s.tick)-info.dwTime);
        s.last_input=input&&age<=s.tick?s.tick-age:0;
        if(enabled&&event_ready&&session&&!suspended){
            // A secure/input desktop cannot be treated as an ordinary foreground app.
            HDESK desktop=OpenInputDesktop(0,FALSE,DESKTOP_READOBJECTS);wchar_t desktop_name[64]{};DWORD needed=0;
            bool ordinary=desktop&&GetUserObjectInformationW(desktop,UOI_NAME,desktop_name,sizeof(desktop_name),&needed)&&_wcsicmp(desktop_name,L"Default")==0;
            if(desktop)CloseDesktop(desktop);
            const auto window=ordinary?GetForegroundWindow():nullptr;
            if(window){identify(window);s.identity=identity;s.name=name;s.path=path;s.valid=input&&!identity.empty();
                for(const auto &ex:config.value("usage_exclusions",Json::array())){auto text=ex.get<std::string>();std::wstring w=std::filesystem::path(std::u8string(text.begin(),text.end())).wstring();CharLowerBuffW(w.data(),static_cast<DWORD>(w.size()));text=utf8(w);if(text==identity||text==identity.substr(identity.find_last_of('\\')+1))s.valid=false;}
            }
        }
        timeline.observe(s,config.value("usage_idle_seconds",120u));
    }
    static void CALLBACK foreground(HWINEVENTHOOK,DWORD,HWND,LONG,LONG,DWORD,DWORD){if(current)try{current->sample();}catch(...){current->fail();}}
    void fail(){std::lock_guard lock(mutex);timeline.reset();error="observation_failed";}
    void maintain_notifications(HWND window){
        const auto now=GetTickCount64();if(notification_check&&now-notification_check<5000)return;notification_check=now;
        if(!hook)hook=SetWinEventHook(EVENT_SYSTEM_FOREGROUND,EVENT_SYSTEM_FOREGROUND,nullptr,foreground,0,0,WINEVENT_OUTOFCONTEXT);
        if(!wts)wts=WTSRegisterSessionNotification(window,NOTIFY_FOR_THIS_SESSION)!=FALSE;
        const bool active=desktop_active();std::lock_guard lock(mutex);
        if(session!=active){timeline.reset();session=active;}
        event_ready=hook&&wts;
        if(!event_ready)error="session_or_foreground_notifications_unavailable";
        else if(error=="session_or_foreground_notifications_unavailable")error.clear();
    }
    static LRESULT CALLBACK proc(HWND w,UINT m,WPARAM p,LPARAM l){
        auto *self=reinterpret_cast<Impl*>(GetWindowLongPtrW(w,GWLP_USERDATA));
        if(m==WM_NCCREATE){self=static_cast<Impl*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);SetWindowLongPtrW(w,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
        if(!self)return DefWindowProcW(w,m,p,l);
        try{
            if(m==WM_TIMER){self->maintain_notifications(w);self->sample();return 0;}
            if(m==WM_WTSSESSION_CHANGE||m==WM_POWERBROADCAST){
                self->sample();std::lock_guard lock(self->mutex);self->timeline.reset();
                if(m==WM_WTSSESSION_CHANGE)self->session=desktop_active();
                if(m==WM_POWERBROADCAST){if(p==PBT_APMSUSPEND)self->suspended=true;else if(p==PBT_APMRESUMEAUTOMATIC||p==PBT_APMRESUMESUSPEND){self->suspended=false;self->session=desktop_active();}}return TRUE;
            }
        }catch(...){self->fail();}return DefWindowProcW(w,m,p,l);
    }
    void run(){
        current=this;MSG msg{};PeekMessageW(&msg,nullptr,WM_USER,WM_USER,PM_NOREMOVE);thread_id=GetCurrentThreadId();
        WNDCLASSW wc{};wc.lpfnWndProc=proc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"PulseTrace.AppUsage";RegisterClassW(&wc);
        HWND window=CreateWindowExW(0,wc.lpszClassName,L"",WS_OVERLAPPED,0,0,0,0,nullptr,nullptr,wc.hInstance,this);
        if(window)maintain_notifications(window);
        const auto timer=window?SetTimer(window,1,1000,nullptr):0;
        if(!window||!timer){std::lock_guard lock(mutex);error="message_window_unavailable";}
        try{sample();while(!quitting&&GetMessageW(&msg,nullptr,0,0)>0){TranslateMessage(&msg);DispatchMessageW(&msg);}sample();}catch(...){fail();}
        if(hook)UnhookWinEvent(hook);if(wts)WTSUnRegisterSessionNotification(window);if(window){KillTimer(window,1);DestroyWindow(window);}thread_id=0;current=nullptr;
    }
};
thread_local AppUsageMonitor::Impl *AppUsageMonitor::Impl::current=nullptr;
AppUsageMonitor::AppUsageMonitor():impl_(std::make_unique<Impl>()){impl_->thread=std::thread([this]{impl_->run();});}
AppUsageMonitor::~AppUsageMonitor(){stop();}
void AppUsageMonitor::stop(){impl_->quitting=true;if(auto id=impl_->thread_id.load())PostThreadMessageW(id,WM_QUIT,0,0);if(impl_->thread.joinable())impl_->thread.join();}
void AppUsageMonitor::configure(const Json &c,bool monitoring){
    Json config={{"usage_idle_seconds",c.value("usage_idle_seconds",120)},{"usage_exclusions",c.value("usage_exclusions",Json::array())}};
    const bool enabled=monitoring&&c.value("usage_enabled",true);
    std::lock_guard lock(impl_->mutex);if(config!=impl_->config||enabled!=impl_->enabled){impl_->timeline.reset();impl_->config=std::move(config);impl_->enabled=enabled;}
}
void AppUsageMonitor::clear(){std::lock_guard lock(impl_->mutex);impl_->timeline.clear();}
void AppUsageMonitor::persisted_before(std::int64_t minute){std::lock_guard lock(impl_->mutex);impl_->timeline.persisted_before(minute);}
Json AppUsageMonitor::rows()const{std::lock_guard lock(impl_->mutex);return impl_->timeline.rows();}
Json AppUsageMonitor::status()const{std::lock_guard lock(impl_->mutex);return {{"enabled",impl_->enabled},{"session_active",impl_->session&&!impl_->suspended},{"event_ready",impl_->event_ready},{"error",impl_->error},{"gaps",impl_->timeline.gaps()},{"sampling_ms",1000}};}
}
