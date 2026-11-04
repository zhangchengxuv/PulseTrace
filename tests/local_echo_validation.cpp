#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>
#include <netioapi.h>
#include <icmpapi.h>
#include <evntrace.h>
#include <evntcons.h>
#include <objbase.h>
#include "pulsetrace/packet_accounting.hpp"
#include "pulsetrace/json.hpp"
#include <array>
#include <cstring>
#include <iostream>
#include <thread>
#include <vector>
using namespace pulsetrace;
namespace {
constexpr std::array<unsigned char, 16> marker{'P', 'u', 'l', 's', 'e', 'T', 'r', 'a',
                                               'c', 'e', 'E', 'c', 'h', 'o', '1', '7'};
std::array<unsigned char, 6> mac{};
ULONG selected{};
std::uint64_t bytes[4]{}, packets[4]{}, invalid{};
void WINAPI receive(EVENT_RECORD *e) {
    if (e->EventHeader.EventDescriptor.Id != 1001 || e->UserDataLength < 12 ||
        e->EventHeader.EventDescriptor.Version != 0)
        return;
    ULONG index{}, size{};
    std::memcpy(&index, e->UserData, 4);
    std::memcpy(&size, static_cast<const char *>(e->UserData) + 8, 4);
    if (index != selected || size > e->UserDataLength - 12)
        return;
    const auto flags = e->EventHeader.EventDescriptor.Keyword;
    if ((flags & 0xc0000000ULL) != 0xc0000000ULL || flags & 0x2000000ULL)
        return;
    auto p = std::span(static_cast<const std::uint8_t *>(e->UserData) + 12, std::min<ULONG>(size, 128));
    auto r = account_packet(p, size, mac, (flags & 0x10000) ? 105 : 1);
    if (r.result != PacketResult::accepted)
        return;
    const auto o = r.header_offset;
    const auto header = r.family == 4 ? (p[o] & 15) * 4 : 40u;
    if (p.size() < o + header + 8 + marker.size())
        return;
    if ((r.family == 4 && p[o + 9] != 1) || (r.family == 6 && p[o + 6] != 58))
        return;
    if (!std::equal(marker.begin(), marker.end(), p.begin() + o + header + 8))
        return;
    const unsigned slot = (r.family == 4 ? 0 : 2) + (r.upload ? 0 : 1);
    if (r.ip_bytes != (r.family == 4 ? 44u : 64u))
        ++invalid;
    bytes[slot] += r.ip_bytes;
    ++packets[slot];
}
bool private4(ULONG ip) {
    const auto n = ntohl(ip);
    return (n >> 24) == 10 || (n >> 20) == 0xac1 || (n >> 16) == 0xc0a8 || (n >> 16) == 0xa9fe;
}
} // namespace
int main(int argc, char **argv) {
    if (argc != 2 || std::string(argv[1]) != "--local-echo") {
        std::cout << "Explicit --local-echo required: at most two 16-byte ICMP echoes per family to "
                     "private/link-local physical gateway; never public hosts.\n";
        return 0;
    }
    WSADATA w{};
    WSAStartup(MAKEWORD(2, 2), &w);
    MIB_IF_TABLE2 *t{};
    if (GetIfTable2(&t) == NO_ERROR) {
        for (ULONG i = 0; i < t->NumEntries; i++) {
            const auto &r = t->Table[i];
            if (r.OperStatus == IfOperStatusUp && r.InterfaceAndOperStatusFlags.HardwareInterface &&
                r.PhysicalAddressLength == 6 &&
                (r.Type == IF_TYPE_IEEE80211 || r.Type == IF_TYPE_ETHERNET_CSMACD)) {
                selected = r.InterfaceIndex;
                std::copy_n(r.PhysicalAddress, 6, mac.begin());
                break;
            }
        }
        FreeMibTable(t);
    }
    ULONG source4{}, target4{}, length = 32768;
    SOCKADDR_IN6 source6{}, target6{};
    bool has6 = false;
    std::vector<unsigned char> data(length);
    auto *a = reinterpret_cast<IP_ADAPTER_ADDRESSES *>(data.data());
    auto status = GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_INCLUDE_GATEWAYS, nullptr, a, &length);
    if (status == ERROR_BUFFER_OVERFLOW) {
        data.resize(length);
        a = reinterpret_cast<IP_ADAPTER_ADDRESSES *>(data.data());
        status = GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_INCLUDE_GATEWAYS, nullptr, a, &length);
    }
    if (status == NO_ERROR)
        for (auto *p = a; p; p = p->Next)
            if (p->IfIndex == selected) {
                for (auto *u = p->FirstUnicastAddress; u; u = u->Next) {
                    if (u->Address.lpSockaddr->sa_family == AF_INET)
                        source4 =
                            reinterpret_cast<sockaddr_in *>(u->Address.lpSockaddr)->sin_addr.S_un.S_addr;
                    if (u->Address.lpSockaddr->sa_family == AF_INET6) {
                        auto *ip = reinterpret_cast<sockaddr_in6 *>(u->Address.lpSockaddr);
                        if (IN6_IS_ADDR_LINKLOCAL(&ip->sin6_addr))
                            source6 = *ip;
                    }
                }
                for (auto *g = p->FirstGatewayAddress; g; g = g->Next) {
                    if (g->Address.lpSockaddr->sa_family == AF_INET) {
                        const auto ip =
                            reinterpret_cast<sockaddr_in *>(g->Address.lpSockaddr)->sin_addr.S_un.S_addr;
                        if (private4(ip))
                            target4 = ip;
                    }
                    if (g->Address.lpSockaddr->sa_family == AF_INET6) {
                        auto *ip = reinterpret_cast<sockaddr_in6 *>(g->Address.lpSockaddr);
                        if (IN6_IS_ADDR_LINKLOCAL(&ip->sin6_addr)) {
                            target6 = *ip;
                            target6.sin6_scope_id = selected;
                            has6 = source6.sin6_family == AF_INET6;
                        }
                    }
                }
            }
    Json result = {{"interface_index", selected},
                   {"ipv4_local_gateway_available", source4 && target4},
                   {"ipv6_linklocal_gateway_available", has6},
                   {"public_traffic_generated", false}};
    struct B {
        EVENT_TRACE_PROPERTIES p;
        wchar_t name[128];
    } b{};
    b.p.Wnode.BufferSize = sizeof(b);
    b.p.Wnode.Flags = WNODE_FLAG_TRACED_GUID;
    b.p.Wnode.ClientContext = 2;
    CoCreateGuid(&b.p.Wnode.Guid);
    b.p.LogFileMode = EVENT_TRACE_REAL_TIME_MODE | EVENT_TRACE_NO_PER_PROCESSOR_BUFFERING;
    b.p.BufferSize = 64;
    b.p.MinimumBuffers = 2;
    b.p.MaximumBuffers = 8;
    b.p.FlushTimer = 1;
    b.p.LoggerNameOffset = offsetof(B, name);
    const auto name = L"PulseTrace.EchoValidation." + std::to_wstring(GetCurrentProcessId());
    wcscpy_s(b.name, name.c_str());
    TRACEHANDLE session{};
    const auto start = StartTraceW(&session, name.c_str(), &b.p);
    result["start_status"] = start;
    if (start == 0) {
        EVENT_TRACE_LOGFILEW log{};
        log.LoggerName = b.name;
        log.ProcessTraceMode = PROCESS_TRACE_MODE_REAL_TIME | PROCESS_TRACE_MODE_EVENT_RECORD;
        log.EventRecordCallback = receive;
        auto consumer = OpenTraceW(&log);
        if (consumer != INVALID_PROCESSTRACE_HANDLE) {
            std::thread thread([&] { ProcessTrace(&consumer, 1, nullptr, nullptr); });
            GUID provider{};
            CLSIDFromString(L"{2ED6006E-4729-4609-B423-3EE7BCD678EF}", &provider);
            const auto enabled = EnableTraceEx2(session, &provider, EVENT_CONTROL_CODE_ENABLE_PROVIDER,
                                                TRACE_LEVEL_VERBOSE, ~0ULL, 0, 1000, nullptr);
            result["enable_status"] = enabled;
            unsigned sent4 = 0, sent6 = 0, reply4 = 0, reply6 = 0;
            if (!enabled) {
                std::array<unsigned char, 1024> reply{};
                if (source4 && target4) {
                    const auto h = IcmpCreateFile();
                    if (h != INVALID_HANDLE_VALUE) {
                        for (int i = 0; i < 2; i++) {
                            ++sent4;
                            auto n = IcmpSendEcho2Ex(h, nullptr, nullptr, nullptr, source4, target4,
                                                     const_cast<unsigned char *>(marker.data()), 16, nullptr,
                                                     reply.data(), reply.size(), 500);
                            if (n && reinterpret_cast<ICMP_ECHO_REPLY *>(reply.data())->Status == IP_SUCCESS)
                                ++reply4;
                        }
                        IcmpCloseHandle(h);
                    }
                }
                if (has6) {
                    const auto h = Icmp6CreateFile();
                    if (h != INVALID_HANDLE_VALUE) {
                        source6.sin6_scope_id = selected;
                        for (int i = 0; i < 2; i++) {
                            ++sent6;
                            auto n = Icmp6SendEcho2(h, nullptr, nullptr, nullptr, &source6, &target6,
                                                    const_cast<unsigned char *>(marker.data()), 16, nullptr,
                                                    reply.data(), reply.size(), 500);
                            if (n &&
                                reinterpret_cast<ICMPV6_ECHO_REPLY *>(reply.data())->Status == IP_SUCCESS)
                                ++reply6;
                        }
                        IcmpCloseHandle(h);
                    }
                }
            }
            const auto stop = ControlTraceW(session, b.name, &b.p, EVENT_TRACE_CONTROL_STOP);
            if (stop != ERROR_SUCCESS)
                CloseTrace(consumer);
            thread.join();
            if (stop == ERROR_SUCCESS)
                CloseTrace(consumer);
            result["stop_status"] = stop;
            result["events_lost"] = b.p.EventsLost;
            result["buffers_lost"] = b.p.RealTimeBuffersLost;
            result["requests_sent"] = {sent4, sent6};
            result["native_echo_replies"] = {reply4, reply6};
            result["observed_marker_packets"] = {packets[0], packets[1], packets[2], packets[3]};
            result["observed_ip_bytes"] = {bytes[0], bytes[1], bytes[2], bytes[3]};
            result["length_mismatches"] = invalid;
            result["ipv4_echo_validation_passed"] = sent4 == 2 && reply4 == 2 && packets[0] == 2 &&
                                                    packets[1] == 2 && bytes[0] == 88 && bytes[1] == 88 &&
                                                    !invalid && !b.p.EventsLost && !b.p.RealTimeBuffersLost;
            result["ipv6_echo_validation_passed"] = sent6 == 2 && reply6 == 2 && packets[2] == 2 &&
                                                    packets[3] == 2 && bytes[2] == 128 && bytes[3] == 128 &&
                                                    !invalid && !b.p.EventsLost && !b.p.RealTimeBuffersLost;
        } else
            ControlTraceW(session, b.name, &b.p, EVENT_TRACE_CONTROL_STOP);
    }
    result["scope"] = "Only local gateway ICMP. Does not validate all offloads, TCP/TUN/proxy behaviour, "
                      "Wireshark comparison or campus billing.";
    std::cout << result.dump(2) << '\n';
    WSACleanup();
    return 0;
}
