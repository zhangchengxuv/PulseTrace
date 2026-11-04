#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <evntrace.h>
#include <evntcons.h>
#include <objbase.h>
#include <iphlpapi.h>
#include <netioapi.h>
#include <atomic>
#include <cstring>
#include <iostream>
#include <map>
#include <thread>
#include "pulsetrace/json.hpp"
#include "pulsetrace/packet_accounting.hpp"
namespace {
std::atomic<unsigned long long> records{}, fragments{}, complete{}, rejected{};
struct Counter {
    std::array<std::uint8_t, 6> mac;
    std::uint64_t bytes[4]{};
};
std::map<ULONG, Counter> interfaces;
std::map<int, unsigned long long> rejectedReasons;
std::map<std::tuple<std::uint64_t, unsigned, unsigned>, std::uint64_t> metadata;
void WINAPI event(EVENT_RECORD *e) {
    ++records;
    if (e->EventHeader.EventDescriptor.Id != 1001)
        return;
    ++fragments;
    const auto flags = e->EventHeader.EventDescriptor.Keyword;
    ULONG diagnosticIndex{};
    if (e->UserDataLength >= 4)
        std::memcpy(&diagnosticIndex, e->UserData, 4);
    ++metadata[{flags, diagnosticIndex, e->EventHeader.EventDescriptor.Version}];
    if (e->EventHeader.EventDescriptor.Version != 0 || (flags & 0xc0000000ULL) != 0xc0000000ULL ||
        (flags & 0x2000000ULL) || !(flags & (1 | 0x10000)) || e->UserDataLength < 12) {
        ++rejected;
        return;
    }
    ULONG index{}, size{};
    std::memcpy(&index, e->UserData, 4);
    std::memcpy(&size, static_cast<const char *>(e->UserData) + 8, 4);
    auto it = interfaces.find(index);
    if (it == interfaces.end())
        return;
    if (size > e->UserDataLength - 12) {
        ++rejected;
        return;
    }
    const auto header =
        std::span(static_cast<const std::uint8_t *>(e->UserData) + 12, std::min<ULONG>(size, 128));
    const auto p = pulsetrace::account_packet(header, size, it->second.mac, (flags & 0x10000) ? 105 : 1);
    if (p.result != pulsetrace::PacketResult::accepted) {
        ++rejected;
        ++rejectedReasons[static_cast<int>(p.result)];
        return;
    }
    const auto direction = flags & 0x300000000ULL;
    if ((direction != 0x100000000ULL && direction != 0x200000000ULL) ||
        p.upload != (direction == 0x100000000ULL)) {
        ++rejected;
        return;
    }
    ++complete;
    it->second.bytes[(p.family == 4 ? 0 : 2) + (p.upload ? 0 : 1)] += p.ip_bytes;
}
} // namespace
int main() {
    MIB_IF_TABLE2 *table{};
    if (GetIfTable2(&table) == NO_ERROR) {
        for (ULONG i = 0; i < table->NumEntries; i++) {
            const auto &r = table->Table[i];
            if (r.InterfaceAndOperStatusFlags.HardwareInterface && r.PhysicalAddressLength == 6 &&
                (r.Type == IF_TYPE_ETHERNET_CSMACD || r.Type == IF_TYPE_IEEE80211)) {
                Counter c{};
                std::copy_n(r.PhysicalAddress, 6, c.mac.begin());
                interfaces[r.InterfaceIndex] = c;
            }
        }
        FreeMibTable(table);
    }
    struct Buffer {
        EVENT_TRACE_PROPERTIES p;
        wchar_t name[128];
    } b{};
    b.p.Wnode.BufferSize = sizeof(b);
    b.p.Wnode.Flags = WNODE_FLAG_TRACED_GUID;
    b.p.Wnode.ClientContext = 1;
    CoCreateGuid(&b.p.Wnode.Guid);
    b.p.LogFileMode = EVENT_TRACE_REAL_TIME_MODE | EVENT_TRACE_NO_PER_PROCESSOR_BUFFERING;
    b.p.BufferSize = 64;
    b.p.MinimumBuffers = 2;
    b.p.MaximumBuffers = 8;
    b.p.FlushTimer = 1;
    b.p.LoggerNameOffset = offsetof(Buffer, name);
    const auto name = L"PulseTrace.NdisValidation." + std::to_wstring(GetCurrentProcessId());
    wcscpy_s(b.name, name.c_str());
    GUID provider{};
    CLSIDFromString(L"{2ED6006E-4729-4609-B423-3EE7BCD678EF}", &provider);
    TRACEHANDLE session{};
    ULONG started = StartTraceW(&session, name.c_str(), &b.p), enabled = ERROR_NOT_READY,
          opened = ERROR_NOT_READY, processed = ERROR_NOT_READY, stopped = ERROR_SUCCESS;
    if (started == ERROR_SUCCESS) {
        EVENT_TRACE_LOGFILEW log{};
        log.LoggerName = b.name;
        log.ProcessTraceMode = PROCESS_TRACE_MODE_REAL_TIME | PROCESS_TRACE_MODE_EVENT_RECORD;
        log.EventRecordCallback = event;
        auto consumer = OpenTraceW(&log);
        opened = consumer == INVALID_PROCESSTRACE_HANDLE ? GetLastError() : ERROR_SUCCESS;
        if (opened == ERROR_SUCCESS) {
            std::thread thread([&] { processed = ProcessTrace(&consumer, 1, nullptr, nullptr); });
            enabled = EnableTraceEx2(session, &provider, EVENT_CONTROL_CODE_ENABLE_PROVIDER,
                                     TRACE_LEVEL_VERBOSE, ~0ULL, 0, 1000, nullptr);
            HANDLE timer = CreateWaitableTimerW(nullptr, TRUE, nullptr);
            LARGE_INTEGER due{};
            due.QuadPart = -30000000LL;
            if (timer) {
                SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE);
                WaitForSingleObject(timer, 5000);
                CloseHandle(timer);
            }
            stopped = ControlTraceW(session, name.c_str(), &b.p, EVENT_TRACE_CONTROL_STOP);
            if (stopped != ERROR_SUCCESS)
                CloseTrace(consumer);
            thread.join();
            if (stopped == ERROR_SUCCESS)
                CloseTrace(consumer);
        } else
            stopped = ControlTraceW(session, name.c_str(), &b.p, EVENT_TRACE_CONTROL_STOP);
    }
    pulsetrace::Json reasons = pulsetrace::Json::array();
    for (const auto &[id, n] : rejectedReasons)
        reasons.push_back({{"reason", id}, {"count", n}});
    pulsetrace::Json observed = pulsetrace::Json::array();
    for (const auto &[key, n] : metadata)
        observed.push_back({{"keyword", std::get<0>(key)},
                            {"miniport_if_index", std::get<1>(key)},
                            {"version", std::get<2>(key)},
                            {"count", n}});
    pulsetrace::Json totals = pulsetrace::Json::array();
    for (const auto &[id, c] : interfaces)
        totals.push_back({{"interface_index", id},
                          {"ipv4_up_observed", c.bytes[0]},
                          {"ipv4_down_observed", c.bytes[1]},
                          {"ipv6_up_observed", c.bytes[2]},
                          {"ipv6_down_observed", c.bytes[3]}});
    std::cout << pulsetrace::Json{{"rejected_reasons", reasons},
                                  {"observed_event_metadata", observed},
                                  {"start_status", started},
                                  {"enable_status", enabled},
                                  {"open_status", opened},
                                  {"process_status", processed},
                                  {"stop_status", stopped},
                                  {"records", records.load()},
                                  {"fragments", fragments.load()},
                                  {"accepted", complete.load()},
                                  {"rejected", rejected.load()},
                                  {"events_lost", b.p.EventsLost},
                                  {"buffers_lost", b.p.RealTimeBuffersLost},
                                  {"interfaces", totals},
                                  {"physical_ip_bytes_validated", false},
                                  {"note", "3s passive probe; no traffic generated, no trace file, no "
                                           "payload saved; zero events is not proof of zero traffic"}}
                     .dump(2)
              << '\n';
    return stopped == ERROR_SUCCESS ? 0 : 1;
}
