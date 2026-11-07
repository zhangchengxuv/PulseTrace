#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include "pulsetrace/network_monitor.hpp"
#include "pulsetrace/network_selection.hpp"
#include <iphlpapi.h>
#include <memory>
#include <netioapi.h>
#include <netlistmgr.h>
#include <set>
#include <stdexcept>
#include <vector>
namespace pulsetrace {
namespace {
std::string utf8(const wchar_t *s) {
    int n = WideCharToMultiByte(CP_UTF8, 0, s, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 1)
        return {};
    std::string out(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s, -1, out.data(), n, nullptr, nullptr);
    out.pop_back();
    return out;
}
} // namespace
NetworkMonitor::NetworkMonitor(CaptureWindow *window) : capture_(std::make_unique<NdisCapture>(window)) {}
NetworkMonitor::~NetworkMonitor() = default;
NetworkMonitor::NetworkMonitor(NetworkMonitor &&) noexcept = default;
NetworkMonitor &NetworkMonitor::operator=(NetworkMonitor &&) noexcept = default;
void NetworkMonitor::reset_baseline(){previous_.clear();last_tick_=0;was_enabled_=false;capture_->configure(false,0,0,{});}
NetworkSample NetworkMonitor::sample(std::int64_t minute, bool enabled, const std::string &selected) {
    MIB_IF_TABLE2 *raw{};
    if (GetIfTable2(&raw) != NO_ERROR)
        throw std::runtime_error("GetIfTable2 failed");
    std::unique_ptr<MIB_IF_TABLE2, decltype(&FreeMibTable)> table(raw, FreeMibTable);
    const auto now = GetTickCount64();
    const double elapsed = last_tick_ ? static_cast<double>(now - last_tick_) / 1000 : 0;
    last_tick_ = now;
    std::map<ULONG, std::pair<bool, bool>> addresses;
    ULONG size = 16384;
    std::vector<unsigned char> buffer(size);
    auto *adapters = reinterpret_cast<IP_ADAPTER_ADDRESSES *>(buffer.data());
    ULONG result = GetAdaptersAddresses(
        AF_UNSPEC, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER, nullptr,
        adapters, &size);
    if (result == ERROR_BUFFER_OVERFLOW) {
        buffer.resize(size);
        adapters = reinterpret_cast<IP_ADAPTER_ADDRESSES *>(buffer.data());
        result = GetAdaptersAddresses(
            AF_UNSPEC, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER, nullptr,
            adapters, &size);
    }
    if (result == NO_ERROR)
        for (auto *a = adapters; a; a = a->Next) {
            auto &families = addresses[a->IfIndex];
            for (auto *u = a->FirstUnicastAddress; u; u = u->Next) {
                if (u->Address.lpSockaddr->sa_family == AF_INET)
                    families.first = true;
                if (u->Address.lpSockaddr->sa_family == AF_INET6)
                    families.second = true;
            }
        }
    NetworkSample sample;
    std::set<std::string> seen;
    const auto change = [&](const std::string &kind, const std::string &id, const std::string &name) {
        sample.events.push_back(
            {{"sequence", ++sequence_}, {"kind", kind}, {"interface_id", id}, {"name", name}});
    };
    FILETIME wall{};
    GetSystemTimeAsFileTime(&wall);
    const auto observed_ms =
        static_cast<std::int64_t>(
            ((static_cast<std::uint64_t>(wall.dwHighDateTime) << 32) | wall.dwLowDateTime) / 10000) -
        11644473600000LL;
    Json interfaces = Json::array();
    std::map<std::uint64_t, std::uint64_t> route_costs;
    MIB_IPFORWARD_TABLE2 *route_table{};
    if (selected.empty() && GetIpForwardTable2(AF_UNSPEC, &route_table) == NO_ERROR) {
        std::unique_ptr<MIB_IPFORWARD_TABLE2, decltype(&FreeMibTable)> routes(route_table, FreeMibTable);
        for (ULONG i = 0; i < routes->NumEntries; ++i) {
            const auto &route = routes->Table[i];
            if (route.DestinationPrefix.PrefixLength || !route.ValidLifetime || route.Loopback) continue;
            MIB_IPINTERFACE_ROW ip{};
            InitializeIpInterfaceEntry(&ip);
            ip.Family = route.DestinationPrefix.Prefix.si_family;
            ip.InterfaceLuid = route.InterfaceLuid;
            if (GetIpInterfaceEntry(&ip) != NO_ERROR || !ip.Connected || ip.DisableDefaultRoutes) continue;
            const auto cost = std::uint64_t(route.Metric) + ip.Metric;
            auto [entry, inserted] = route_costs.emplace(route.InterfaceLuid.Value, cost);
            if (!inserted) entry->second = std::min(entry->second, cost);
        }
    }
    std::vector<NetworkCandidate> candidates;
    for (ULONG i = 0; i < raw->NumEntries; ++i) {
        const auto &r = raw->Table[i];
        if (r.Type == IF_TYPE_SOFTWARE_LOOPBACK) continue;
        const auto route = route_costs.find(r.InterfaceLuid.Value);
        candidates.push_back({std::to_string(r.InterfaceLuid.Value), r.OperStatus == IfOperStatusUp,
                              bool(r.InterfaceAndOperStatusFlags.HardwareInterface),
                              route == route_costs.end() ? std::numeric_limits<std::uint64_t>::max()
                                                        : route->second});
    }
    const auto chosen = select_network_interface(candidates, selected, last_selected_);
    double in_rate = 0, out_rate = 0;
    const MIB_IF_ROW2 *physical_selected = nullptr;
    for (ULONG i = 0; i < raw->NumEntries; ++i) {
        const auto &r = raw->Table[i];
        if (r.Type == IF_TYPE_SOFTWARE_LOOPBACK)
            continue;
        const auto id = std::to_string(r.InterfaceLuid.Value);
        const bool up = r.OperStatus == IfOperStatusUp;
        seen.insert(id);
        const bool physical = r.InterfaceAndOperStatusFlags.HardwareInterface;
        if (id == chosen && physical && up && r.PhysicalAddressLength == 6 &&
            (r.Type == IF_TYPE_ETHERNET_CSMACD || r.Type == IF_TYPE_IEEE80211))
            physical_selected = &r;
        const auto family = addresses[r.InterfaceIndex];
        interfaces.push_back({{"id", id},
                              {"name", utf8(r.Alias)},
                              {"description", utf8(r.Description)},
                              {"up", up},
                              {"physical", physical},
                              {"type", r.Type == IF_TYPE_IEEE80211                         ? "Wi-Fi"
                                       : r.Type == IF_TYPE_ETHERNET_CSMACD                 ? "Ethernet"
                                       : r.Type == IF_TYPE_PPP || r.Type == IF_TYPE_TUNNEL ? "VPN/Tunnel"
                                                                                           : "Virtual/Other"},
                              {"ipv4_configured", family.first},
                              {"ipv6_configured", family.second}});
        const auto old = previous_.find(id);
        if (enabled && (!was_enabled_ || old == previous_.end() || old->second.up != up)) {
            ++totals_[{minute, "link/" + id + (up ? "/up" : "/down")}];
            change(!was_enabled_ || old == previous_.end() ? "observed"
                   : up                                    ? "connected"
                                                           : "disconnected",
                   id, utf8(r.Alias));
        }
        if (id == chosen && up && enabled && old != previous_.end() && old->second.up &&
            id == last_selected_ && elapsed > 0) {
            const auto in = r.InOctets >= old->second.in ? r.InOctets - old->second.in : 0;
            const auto out = r.OutOctets >= old->second.out ? r.OutOctets - old->second.out : 0;
            in_rate = in / elapsed;
            out_rate = out / elapsed;
            totals_[{minute, "interface_download_bytes"}] += in;
            totals_[{minute, "interface_upload_bytes"}] += out;
            totals_[{minute, "bytes/" + id + "/download"}] += in;
            totals_[{minute, "bytes/" + id + "/upload"}] += out;
        }
        previous_[id] = {r.InOctets, r.OutOctets, up, utf8(r.Alias)};
    }
    for (auto it = previous_.begin(); it != previous_.end();)
        if (!seen.contains(it->first)) {
            if (enabled && it->second.up)
                change("removed", it->first, it->second.name);
            it = previous_.erase(it);
        } else
            ++it;
    if (enabled && !last_selected_.empty() && last_selected_ != chosen) {
        ++totals_[{minute, "selection/" + chosen}];
        change("selection_changed", chosen, "");
    }
    last_selected_ = chosen;
    was_enabled_ = enabled;
    for (auto &event : sample.events)
        event["observed_ms"] = observed_ms;
    sample.status = {{"interfaces", interfaces},
                     {"selected", chosen},
                     {"download_bps", in_rate},
                     {"upload_bps", out_rate},
                     {"protocol_split_supported", false},
                     {"protocol_split_reason", "当前普通权限后端仅提供所选接口总 octet。IPv4/IPv6 "
                                               "分项未知；不以总量推算。不等于应用净荷或校园网计费。"},
                     {"connectivity_reason",
                      "地址族标志表示已配置地址，不证明互联网可达。物理接口与 TUN 分开，绝不相加。"}};
    sample.status["ipv4_internet"] = nullptr;
    sample.status["ipv6_internet"] = nullptr;
    const auto com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    INetworkListManager *manager{};
    if (SUCCEEDED(com) || com == RPC_E_CHANGED_MODE) {
        if (SUCCEEDED(CoCreateInstance(CLSID_NetworkListManager, nullptr, CLSCTX_ALL, IID_INetworkListManager,
                                       reinterpret_cast<void **>(&manager)))) {
            NLM_CONNECTIVITY connectivity{};
            if (SUCCEEDED(manager->GetConnectivity(&connectivity))) {
                sample.status["ipv4_internet"] = (connectivity & NLM_CONNECTIVITY_IPV4_INTERNET) != 0;
                sample.status["ipv6_internet"] = (connectivity & NLM_CONNECTIVITY_IPV6_INTERNET) != 0;
            }
            manager->Release();
        }
        if (SUCCEEDED(com))
            CoUninitialize();
    }
    sample.status["internet_hint_source"] =
        "Windows Network List Manager 系统级连通性提示；不是所选接口端到端探测，不产生本程序主动探测流量。";
    for (auto it = totals_.begin(); it != totals_.end();) {
        if (it->first.first < minute - 120)
            it = totals_.erase(it);
        else {
            sample.rows.push_back({{"minute", it->first.first},
                                   {"module", "network"},
                                   {"metric", it->first.second},
                                   {"value", it->second}});
            ++it;
        }
    }
    std::array<std::uint8_t, 6> mac{};
    if (physical_selected)
        std::copy_n(physical_selected->PhysicalAddress, 6, mac.begin());
    capture_->configure(enabled && physical_selected,
                        physical_selected ? physical_selected->InterfaceIndex : 0,
                        physical_selected ? physical_selected->InterfaceLuid.Value : 0, mac);
    sample.status["protocol_capture"] = capture_->snapshot(minute, sample.rows);
    sample.status["protocol_split_available"] = sample.status["protocol_capture"]["available"];
    sample.status["protocol_split_reason"] =
        "已接入 ETW NDIS 物理接口 IP 分族观测；显示真实观测值与数据质量，尚待独立抓包对照验收。";
    if (!physical_selected) {
        sample.status["protocol_capture"]["available"] = false;
        sample.status["protocol_split_available"] = false;
        sample.status["protocol_capture"]["error"] =
            "请选择已连接的物理 WLAN / Ethernet 接口；TUN、VPN 和回环不会与物理接口相加。";
    }
    return sample;
}
} // namespace pulsetrace
