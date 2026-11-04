#include <windows.h>
#include <objbase.h>
#include <initguid.h>
#include <evntrace.h>
#include <cstddef>
#include <iostream>
#include <string>
// Permission-only probe. No events are consumed and no trace file is created.
// If permitted, the uniquely owned system logger is stopped immediately.
int main(){
    struct Buffer{EVENT_TRACE_PROPERTIES properties;wchar_t name[128];} buffer{};
    auto& p=buffer.properties;p.Wnode.BufferSize=sizeof(buffer);p.Wnode.Flags=WNODE_FLAG_TRACED_GUID;p.Wnode.ClientContext=1;
    CoCreateGuid(&p.Wnode.Guid);p.LogFileMode=EVENT_TRACE_REAL_TIME_MODE|EVENT_TRACE_SYSTEM_LOGGER_MODE|EVENT_TRACE_NO_PER_PROCESSOR_BUFFERING;
    p.EnableFlags=EVENT_TRACE_FLAG_NETWORK_TCPIP;
    p.BufferSize=64;p.MinimumBuffers=2;p.MaximumBuffers=8;p.LoggerNameOffset=offsetof(Buffer,name);
    const auto name=L"PulseTrace.Capability."+std::to_wstring(GetCurrentProcessId());
    wcscpy_s(buffer.name,name.c_str());TRACEHANDLE handle{};
    const auto result=StartTraceW(&handle,name.c_str(),&p);
    ULONG stopped=ERROR_SUCCESS;if(result==ERROR_SUCCESS)stopped=ControlTraceW(handle,name.c_str(),&p,EVENT_TRACE_CONTROL_STOP);
    std::cout<<"{\"kernel_session_start_status\":"<<result<<",\"stop_status\":"<<stopped
             <<",\"access_denied\":"<<(result==ERROR_ACCESS_DENIED?"true":"false")
             <<",\"kernel_network_enabled\":"<<(result==ERROR_SUCCESS?"true":"false")<<",\"validated_ipv4_ipv6_bytes\":false}\n";
    return stopped==ERROR_SUCCESS?0:1;
}
