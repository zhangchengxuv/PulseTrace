#include <windows.h>
#include <objbase.h>
#include <initguid.h>
#include <evntrace.h>
#include <cstddef>
#include <iostream>
#include <string>
#include "pulsetrace/json.hpp"
// Permission-only probe. No events are consumed and no trace file is created.
// If permitted, the uniquely owned system logger is stopped immediately.
int main() {
    pulsetrace::Json providers = pulsetrace::Json::array();
    for (const auto *id :
         {L"{2ED6006E-4729-4609-B423-3EE7BCD678EF}", L"{7DD42A49-5329-4832-8DFD-43D979153A88}",
          L"{2F07E2EE-15DB-40F1-90EF-9D7BA282188A}"}) {
        struct UserBuffer {
            EVENT_TRACE_PROPERTIES p;
            wchar_t name[128];
        } user{};
        user.p.Wnode.BufferSize = sizeof(user);
        user.p.Wnode.Flags = WNODE_FLAG_TRACED_GUID;
        user.p.Wnode.ClientContext = 1;
        CoCreateGuid(&user.p.Wnode.Guid);
        user.p.LogFileMode = EVENT_TRACE_REAL_TIME_MODE | EVENT_TRACE_NO_PER_PROCESSOR_BUFFERING;
        user.p.BufferSize = 64;
        user.p.MinimumBuffers = 2;
        user.p.MaximumBuffers = 4;
        user.p.LoggerNameOffset = offsetof(UserBuffer, name);
        const auto name = L"PulseTrace.ManifestProbe." + std::to_wstring(GetCurrentProcessId()) + L"." +
                          std::to_wstring(providers.size());
        wcscpy_s(user.name, name.c_str());
        TRACEHANDLE session{};
        auto started = StartTraceW(&session, name.c_str(), &user.p);
        ULONG enabled = ERROR_NOT_READY, stopped = ERROR_SUCCESS;
        GUID provider{};
        CLSIDFromString(id, &provider);
        if (started == ERROR_SUCCESS) {
            enabled = EnableTraceEx2(session, &provider, EVENT_CONTROL_CODE_ENABLE_PROVIDER,
                                     TRACE_LEVEL_VERBOSE, ~0ULL, 0, 1000, nullptr);
            stopped = ControlTraceW(session, name.c_str(), &user.p, EVENT_TRACE_CONTROL_STOP);
        }
        std::wstring wide(id);
        providers.push_back({{"provider_guid", std::string(wide.begin(), wide.end())},
                             {"start_status", started},
                             {"enable_status", enabled},
                             {"stop_status", stopped},
                             {"events_lost", user.p.EventsLost},
                             {"events_consumed", 0},
                             {"ip_bytes_validated", false}});
    }
    std::cout << pulsetrace::Json{{"manifest_provider_probes", providers}}.dump() << "\n";
    struct Buffer {
        EVENT_TRACE_PROPERTIES properties;
        wchar_t name[128];
    } buffer{};
    auto &p = buffer.properties;
    p.Wnode.BufferSize = sizeof(buffer);
    p.Wnode.Flags = WNODE_FLAG_TRACED_GUID;
    p.Wnode.ClientContext = 1;
    CoCreateGuid(&p.Wnode.Guid);
    p.LogFileMode =
        EVENT_TRACE_REAL_TIME_MODE | EVENT_TRACE_SYSTEM_LOGGER_MODE | EVENT_TRACE_NO_PER_PROCESSOR_BUFFERING;
    p.EnableFlags = EVENT_TRACE_FLAG_NETWORK_TCPIP;
    p.BufferSize = 64;
    p.MinimumBuffers = 2;
    p.MaximumBuffers = 8;
    p.LoggerNameOffset = offsetof(Buffer, name);
    const auto name = L"PulseTrace.Capability." + std::to_wstring(GetCurrentProcessId());
    wcscpy_s(buffer.name, name.c_str());
    TRACEHANDLE handle{};
    const auto result = StartTraceW(&handle, name.c_str(), &p);
    ULONG stopped = ERROR_SUCCESS;
    if (result == ERROR_SUCCESS)
        stopped = ControlTraceW(handle, name.c_str(), &p, EVENT_TRACE_CONTROL_STOP);
    std::cout << "{\"kernel_session_start_status\":" << result << ",\"stop_status\":" << stopped
              << ",\"access_denied\":" << (result == ERROR_ACCESS_DENIED ? "true" : "false")
              << ",\"kernel_network_enabled\":" << (result == ERROR_SUCCESS ? "true" : "false")
              << ",\"validated_ipv4_ipv6_bytes\":false}\n";
    return stopped == ERROR_SUCCESS ? 0 : 1;
}
