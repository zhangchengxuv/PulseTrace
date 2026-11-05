#include <windows.h>
#include <objbase.h>
#include <evntrace.h>
#include <evntcons.h>
#include <tdh.h>
#include "pulsetrace/traffic_rankings.hpp"
#include <atomic>
#include <cstring>
#include <mutex>
#include <thread>
#include <tuple>
#include <vector>
namespace pulsetrace {
// TDH_INTYPE_UINT32 = 8 in Microsoft's _TDH_IN_TYPE enum. Older MinGW
// tdh.h omits that enum; keep the verified manifest wire type here.
constexpr USHORT manifest_uint32 = 8;
namespace {std::string utf8(const std::wstring &s){const int n=WideCharToMultiByte(CP_UTF8,0,s.data(),static_cast<int>(s.size()),nullptr,0,nullptr,nullptr);std::string r(n,' ');WideCharToMultiByte(CP_UTF8,0,s.data(),static_cast<int>(s.size()),r.data(),n,nullptr,nullptr);return r;}
std::uint64_t stamp(FILETIME t){return (std::uint64_t(t.dwHighDateTime)<<32)|t.dwLowDateTime;}}
struct ProcessTrafficCapture::Impl {
 struct Buffer{EVENT_TRACE_PROPERTIES p{};wchar_t name[128]{};}buffer;
 struct Pending{std::uint64_t bytes{},first{~0ULL},last{};};
 struct Process{HANDLE handle{};std::uint64_t birth{};RankingIdentity id;~Process(){if(handle)CloseHandle(handle);}};
 TRACEHANDLE session{},consumer{INVALID_PROCESSTRACE_HANDLE};GUID provider{};std::thread thread;std::atomic<bool> accepting{false},gate_open{false};std::atomic<std::int64_t> gate_start{};
 std::mutex mutex;std::map<std::tuple<std::int64_t,DWORD,int,bool>,Pending> pending;
 std::map<unsigned,bool> schemas;std::map<DWORD,std::vector<std::shared_ptr<Process>>> processes;
 ULONG start_code{ERROR_NOT_READY},enable_code{ERROR_NOT_READY},lost{},buffers_lost{};std::atomic<ULONG> process_code{0};std::uint64_t rejected{},overflow{};bool requested{};
 Impl(){CLSIDFromString(L"{7DD42A49-5329-4832-8DFD-43D979153A88}",&provider);}
 bool schema(EVENT_RECORD *e){const unsigned key=(e->EventHeader.EventDescriptor.Id<<8)|e->EventHeader.EventDescriptor.Version;if(schemas.contains(key))return schemas[key];
  ULONG length=0;TdhGetEventInformation(e,0,nullptr,nullptr,&length);if(length<sizeof(TRACE_EVENT_INFO)||length>65536)return schemas[key]=false;
  std::vector<BYTE> data(length);auto info=reinterpret_cast<TRACE_EVENT_INFO*>(data.data());if(TdhGetEventInformation(e,0,nullptr,info,&length)!=ERROR_SUCCESS||info->TopLevelPropertyCount<2)return schemas[key]=false;
  for(unsigned i=0;i<2;++i){const auto &p=info->EventPropertyInfoArray[i];if(p.Flags||p.count!=1||p.nonStructType.InType!=manifest_uint32||p.NameOffset>=length)return schemas[key]=false;const auto name=reinterpret_cast<const wchar_t*>(data.data()+p.NameOffset);if(wcscmp(name,i?L"size":L"PID"))return schemas[key]=false;}
  return schemas[key]=true;
 }
 static void WINAPI callback(EVENT_RECORD *e)noexcept{try{static_cast<Impl*>(e->UserContext)->consume(e);}catch(...){auto *p=static_cast<Impl*>(e->UserContext);std::lock_guard lock(p->mutex);++p->rejected;}}
 void consume(EVENT_RECORD *e){if(!accepting.load()||!gate_open.load()||e->EventHeader.TimeStamp.QuadPart<gate_start.load()||!IsEqualGUID(e->EventHeader.ProviderId,provider))return;const auto id=e->EventHeader.EventDescriptor.Id;
  if(id!=10&&id!=11&&id!=26&&id!=27&&id!=42&&id!=43&&id!=58&&id!=59)return;
  if(e->EventHeader.EventDescriptor.Version!=0||e->UserDataLength<8||!schema(e)){std::lock_guard lock(mutex);++rejected;return;}
  DWORD pid{},bytes{};std::memcpy(&pid,e->UserData,4);std::memcpy(&bytes,static_cast<const BYTE*>(e->UserData)+4,4);
  const auto time=e->EventHeader.TimeStamp.QuadPart;if(time<116444736000000000LL)return;
  const auto minute=(time-116444736000000000LL)/600000000LL;const int family=id==26||id==27||id==58||id==59?6:4;const bool up=id==10||id==26||id==42||id==58;
  std::lock_guard lock(mutex);const auto key=std::make_tuple(minute,pid,family,up);if(!pending.contains(key)&&pending.size()>=32768){++overflow;return;}
  auto &p=pending[key];p.bytes+=bytes;p.first=std::min(p.first,static_cast<std::uint64_t>(time));p.last=std::max(p.last,static_cast<std::uint64_t>(time));
 }
 void stop(){accepting.store(false);if(session){ControlTraceW(session,buffer.name,&buffer.p,EVENT_TRACE_CONTROL_STOP);lost=buffer.p.EventsLost;buffers_lost=buffer.p.RealTimeBuffersLost;session=0;}if(consumer!=INVALID_PROCESSTRACE_HANDLE){CloseTrace(consumer);consumer=INVALID_PROCESSTRACE_HANDLE;}if(thread.joinable())thread.join();}
 void start(){buffer={};auto &p=buffer.p;p.Wnode.BufferSize=sizeof(buffer);p.Wnode.Flags=WNODE_FLAG_TRACED_GUID;p.Wnode.ClientContext=2;CoCreateGuid(&p.Wnode.Guid);p.LogFileMode=EVENT_TRACE_REAL_TIME_MODE|EVENT_TRACE_NO_PER_PROCESSOR_BUFFERING;p.BufferSize=64;p.MinimumBuffers=2;p.MaximumBuffers=16;p.FlushTimer=1;p.LoggerNameOffset=offsetof(Buffer,name);
  wcscpy_s(buffer.name,(L"PulseTrace.ProcessTraffic."+std::to_wstring(GetCurrentProcessId())+L"."+std::to_wstring(GetTickCount64())).c_str());
  start_code=StartTraceW(&session,buffer.name,&p);if(start_code){session=0;return;}
  EVENT_TRACE_LOGFILEW log{};log.LoggerName=buffer.name;log.Context=this;log.ProcessTraceMode=PROCESS_TRACE_MODE_REAL_TIME|PROCESS_TRACE_MODE_EVENT_RECORD;log.EventRecordCallback=callback;consumer=OpenTraceW(&log);if(consumer==INVALID_PROCESSTRACE_HANDLE){enable_code=GetLastError();stop();return;}
  thread=std::thread([this,h=consumer]()mutable{process_code=ProcessTrace(&h,1,nullptr,nullptr);});
  enable_code=EnableTraceEx2(session,&provider,EVENT_CONTROL_CODE_ENABLE_PROVIDER,TRACE_LEVEL_VERBOSE,0x30,0,1000,nullptr);if(enable_code){stop();return;}accepting.store(true);
 }
 RankingIdentity resolve(DWORD pid,const Pending &event){
  if(processes.size()>4096)processes.clear();auto &versions=processes[pid];
  // Re-open once per drained PID/bucket, never in the event callback. A PID is not an identity.
  HANDLE current=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,pid);
  if(current){FILETIME created{},exited{},kernel{},user{};if(GetProcessTimes(current,&created,&exited,&kernel,&user)){
   const auto birth=stamp(created);bool known=false;for(const auto &p:versions)known|=p->birth==birth;
   if(!known){wchar_t path[32768]{};DWORD size=32768;auto p=std::make_shared<Process>();p->handle=current;current=nullptr;p->birth=birth;
    if(QueryFullProcessImageNameW(p->handle,0,path,&size))p->id=ranking_identity(utf8(std::wstring(path,size)));else p->id={"unknown","","","unknown"};versions.push_back(p);if(versions.size()>4)versions.erase(versions.begin());}
  }if(current)CloseHandle(current);}
  for(const auto &p:versions){FILETIME created{},exited{},kernel{},user{};if(GetProcessTimes(p->handle,&created,&exited,&kernel,&user)&&process_lifetime_contains(p->birth,stamp(exited),event.first,event.last))return p->id;}
  return {"unknown","","","unknown"};
 }
};
ProcessTrafficCapture::ProcessTrafficCapture():p_(std::make_unique<Impl>()){}
ProcessTrafficCapture::~ProcessTrafficCapture(){p_->stop();}
void ProcessTrafficCapture::configure(bool enabled){if(enabled==p_->requested)return;p_->requested=enabled;if(enabled)p_->start();else p_->stop();}
void ProcessTrafficCapture::gate(bool enabled)noexcept{if(enabled&&!p_->gate_open.load()){FILETIME now{};GetSystemTimeAsFileTime(&now);p_->gate_start=static_cast<std::int64_t>(stamp(now));}p_->gate_open=enabled;}
Json ProcessTrafficCapture::drain(RankingAccumulator &out){auto &p=*p_;if(p.session){auto query=p.buffer;if(ControlTraceW(p.session,p.buffer.name,&query.p,EVENT_TRACE_CONTROL_QUERY)==ERROR_SUCCESS){p.lost=query.p.EventsLost;p.buffers_lost=query.p.RealTimeBuffersLost;}}
 std::map<std::tuple<std::int64_t,DWORD,int,bool>,Impl::Pending> pending;std::uint64_t rejected{},overflow{};{std::lock_guard lock(p.mutex);pending.swap(p.pending);rejected=p.rejected;overflow=p.overflow;}
 for(const auto &[key,value]:pending){const auto id=p.resolve(std::get<1>(key),value);out.add(std::get<0>(key),"windows_etw","app",id,std::get<2>(key),std::get<3>(key)?value.bytes:0,std::get<3>(key)?0:value.bytes,(p.lost||p.buffers_lost||rejected||overflow?ranking_loss:0)|(id.key=="unknown"?ranking_unresolved:0));}
 return {{"state",p.accepting?"observing":!p.requested?"disabled":p.start_code==ERROR_ACCESS_DENIED||p.enable_code==ERROR_ACCESS_DENIED?"permission_denied":"unavailable"},{"source","windows_etw"},{"basis","etw_reported_size"},{"start_status",p.start_code},{"enable_status",p.enable_code},{"process_status",p.process_code.load()},{"events_lost",p.lost},{"buffers_lost",p.buffers_lost},{"rejected_events",rejected},{"queue_overflow",overflow},{"reference_validated",false},{"interface_binding",false}};
}
}
