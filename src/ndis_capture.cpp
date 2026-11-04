#include <winsock2.h>
#include <windows.h>
#include <evntrace.h>
#include <evntcons.h>
#include <objbase.h>
#include "pulsetrace/ndis_capture.hpp"
#include "pulsetrace/packet_accounting.hpp"
#include <algorithm>
#include <cstring>
#include <map>
#include <mutex>
#include <thread>
namespace pulsetrace {
std::int64_t capture_clock() noexcept {
    FILETIME f{};
    GetSystemTimeAsFileTime(&f);
    return (std::int64_t(f.dwHighDateTime) << 32) | f.dwLowDateTime;
}
struct NdisCapture::Impl {
    struct Bin {
        std::int64_t minute{-1};
        std::uint64_t bytes[4]{}, accepted{}, rejected{}, unsupported{}, non_ip{};
    };
    struct Buffer {
        EVENT_TRACE_PROPERTIES p;
        wchar_t name[128];
    } buffer{};
    CaptureWindow *window{};
    TRACEHANDLE session{}, consumer{INVALID_PROCESSTRACE_HANDLE};
    GUID provider{};
    unsigned index{};
    std::uint64_t luid{}, generation{}, last_tick{};
    std::array<std::uint8_t, 6> mac{};
    std::array<Bin, 120> bins{};
    std::map<std::pair<std::int64_t, std::uint64_t>, Json> archived;
    std::mutex mutex;
    std::thread thread;
    std::atomic<std::int64_t> cutoff{};
    std::atomic<ULONG> processing{ERROR_SUCCESS};
    std::atomic<bool> consumer_done{false};
    std::array<std::uint64_t, 4> cumulative{};
    std::array<std::uint64_t, 9> rejection_reasons{};
    ULONG start_status{ERROR_NOT_READY}, enable_status{ERROR_NOT_READY}, lost{}, buffers_lost{};
    bool active{};
    std::string error;
    std::array<std::uint64_t, 4> previous{};
    explicit Impl(CaptureWindow *w) : window(w) {
        CLSIDFromString(L"{2ED6006E-4729-4609-B423-3EE7BCD678EF}", &provider);
    }
    static void WINAPI callback(EVENT_RECORD *e) noexcept {
        try {
            static_cast<Impl *>(e->UserContext)->consume(e);
        } catch (...) { /* bounded, no exceptions cross ETW */
        }
    }
    void consume(EVENT_RECORD *e) {
        if (!IsEqualGUID(e->EventHeader.ProviderId, provider) || e->EventHeader.EventDescriptor.Id != 1001 ||
            e->UserDataLength < 12)
            return;
        const auto time = e->EventHeader.TimeStamp.QuadPart;
        if ((cutoff.load() && time >= cutoff.load()) ||
            (window && ((window->start.load() && time < window->start.load()) ||
                        (window->stop.load() && time >= window->stop.load()))))
            return;
        ULONG observed{}, size{};
        std::memcpy(&observed, e->UserData, 4);
        if (observed != index)
            return;
        std::memcpy(&size, static_cast<const char *>(e->UserData) + 8, 4);
        const auto flags = e->EventHeader.EventDescriptor.Keyword;
        const auto minute = (time - 116444736000000000LL) / 600000000LL;
        if (minute < 0)
            return;
        std::lock_guard lock(mutex);
        auto &b = bins[minute % bins.size()];
        if (b.minute != minute)
            b = Bin{minute};
        if (e->EventHeader.EventDescriptor.Version != 0 || (flags & 0xc0000000ULL) != 0xc0000000ULL ||
            (flags & 0x2000000ULL) || size > e->UserDataLength - 12 || !(flags & (1 | 0x10000))) {
            ++b.rejected;
            ++rejection_reasons[8];
            return;
        }
        const auto packet =
            account_packet({static_cast<const std::uint8_t *>(e->UserData) + 12, std::min<ULONG>(size, 128)},
                           size, mac, (flags & 0x10000) ? 105 : 1);
        if (packet.result == PacketResult::non_ip) {
            ++b.non_ip;
            return;
        }
        const auto direction = flags & 0x300000000ULL;
        if (packet.result != PacketResult::accepted ||
            (direction != 0x100000000ULL && direction != 0x200000000ULL) ||
            packet.upload != (direction == 0x100000000ULL)) {
            ++b.rejected;
            ++rejection_reasons[packet.result == PacketResult::accepted
                                    ? 4
                                    : static_cast<unsigned>(packet.result)];
            if (packet.result == PacketResult::unsupported_jumbogram ||
                packet.result == PacketResult::unsupported_link)
                ++b.unsupported;
            return;
        }
        ++b.accepted;
        b.bytes[(packet.family == 4 ? 0 : 2) + (packet.upload ? 0 : 1)] += packet.ip_bytes;
        cumulative[(packet.family == 4 ? 0 : 2) + (packet.upload ? 0 : 1)] += packet.ip_bytes;
    }
    void query_loss() {
        if (session) {
            auto p = buffer;
            const auto code = ControlTraceW(session, buffer.name, &p.p, EVENT_TRACE_CONTROL_QUERY);
            if (code == ERROR_SUCCESS) {
                lost = p.p.EventsLost;
                buffers_lost = p.p.RealTimeBuffersLost;
            } else {
                error = "ETW quality query failed: " + std::to_string(code);
            }
        }
    }
    Json record(const Bin &b) {
        return {{"minute", b.minute},
                {"interface_id", std::to_string(luid)},
                {"ipv4_up", b.bytes[0]},
                {"ipv4_down", b.bytes[1]},
                {"ipv6_up", b.bytes[2]},
                {"ipv6_down", b.bytes[3]},
                {"source", "etw_ndis"},
                {"basis", "ip_total_length"},
                {"quality", (lost || buffers_lost || b.rejected || !error.empty())
                                ? "incomplete_observation"
                                : "observed_unverified_reference"},
                {"accepted", b.accepted},
                {"rejected", b.rejected},
                {"unsupported", b.unsupported},
                {"events_lost", lost},
                {"buffers_lost", buffers_lost}};
    }
    void stop() {
        if (!session)
            return;
        cutoff.store(capture_clock());
        const auto code = ControlTraceW(session, buffer.name, &buffer.p, EVENT_TRACE_CONTROL_STOP);
        lost = buffer.p.EventsLost;
        buffers_lost = buffer.p.RealTimeBuffersLost;
        if (code != ERROR_SUCCESS)
            error = "ETW stop failed: " + std::to_string(code);
        if (code != ERROR_SUCCESS && consumer != INVALID_PROCESSTRACE_HANDLE)
            CloseTrace(consumer);
        if (thread.joinable())
            thread.join();
        if (code == ERROR_SUCCESS && consumer != INVALID_PROCESSTRACE_HANDLE)
            CloseTrace(consumer);
        consumer = INVALID_PROCESSTRACE_HANDLE;
        session = 0;
        active = false;
        std::lock_guard lock(mutex);
        for (const auto &b : bins)
            if (b.minute >= 0) {
                auto r = record(b);
                const auto key = std::make_pair(b.minute, luid);
                if (archived.contains(key)) {
                    auto old = archived[key];
                    for (const char *k : {"events_lost", "buffers_lost"})
                        r[k] = std::max(r[k].get<std::uint64_t>(), old[k].get<std::uint64_t>());
                    for (const char *k : {"ipv4_up", "ipv4_down", "ipv6_up", "ipv6_down", "accepted",
                                          "rejected", "unsupported"})
                        r[k] = r[k].get<std::uint64_t>() + old[k].get<std::uint64_t>();
                    if (old["quality"] == "incomplete_observation")
                        r["quality"] = "incomplete_observation";
                }
                archived[key] = std::move(r);
            }
        bins = {};
        previous = {};
        last_tick = 0;
    }
    void start() {
        buffer = {};
        auto &p = buffer.p;
        p.Wnode.BufferSize = sizeof(buffer);
        p.Wnode.Flags = WNODE_FLAG_TRACED_GUID;
        p.Wnode.ClientContext = 2;
        CoCreateGuid(&p.Wnode.Guid);
        p.LogFileMode = EVENT_TRACE_REAL_TIME_MODE | EVENT_TRACE_NO_PER_PROCESSOR_BUFFERING;
        p.BufferSize = 64;
        p.MinimumBuffers = 2;
        p.MaximumBuffers = 16;
        p.FlushTimer = 1;
        p.LoggerNameOffset = offsetof(Buffer, name);
        const auto name = L"PulseTrace.Ndis." + std::to_wstring(GetCurrentProcessId()) + L"." +
                          std::to_wstring(++generation);
        wcscpy_s(buffer.name, name.c_str());
        cutoff.store(0);
        processing.store(ERROR_SUCCESS);
        consumer_done.store(false);
        cumulative = {};
        previous = {};
        last_tick = 0;
        rejection_reasons = {};
        error.clear();
        lost = 0;
        buffers_lost = 0;
        start_status = StartTraceW(&session, buffer.name, &p);
        if (start_status) {
            session = 0;
            error = "StartTraceW: " + std::to_string(start_status);
            return;
        }
        EVENT_TRACE_LOGFILEW log{};
        log.LoggerName = buffer.name;
        log.ProcessTraceMode = PROCESS_TRACE_MODE_REAL_TIME | PROCESS_TRACE_MODE_EVENT_RECORD;
        log.EventRecordCallback = callback;
        log.Context = this;
        consumer = OpenTraceW(&log);
        if (consumer == INVALID_PROCESSTRACE_HANDLE) {
            error = "OpenTraceW: " + std::to_string(GetLastError());
            stop();
            return;
        }
        thread = std::thread([this] {
            auto handle = consumer;
            processing.store(ProcessTrace(&handle, 1, nullptr, nullptr));
            consumer_done.store(true);
        });
        enable_status = EnableTraceEx2(session, &provider, EVENT_CONTROL_CODE_ENABLE_PROVIDER,
                                       TRACE_LEVEL_VERBOSE, ~0ULL, 0, 1000, nullptr);
        if (enable_status) {
            error = "EnableTraceEx2: " + std::to_string(enable_status);
            stop();
            return;
        }
        active = true;
    }
};
NdisCapture::NdisCapture(CaptureWindow *w) : p_(std::make_unique<Impl>(w)) {}
NdisCapture::~NdisCapture() { p_->stop(); }
void NdisCapture::configure(bool enabled, unsigned index, std::uint64_t luid,
                            const std::array<std::uint8_t, 6> &mac) {
    auto &p = *p_;
    if (p.active && (!enabled || p.index != index || p.luid != luid || p.mac != mac))
        p.stop();
    if (!enabled || !index) {
        p.index = 0;
        return;
    }
    if (p.active)
        return;
    if (p.index == index && p.luid == luid && !p.error.empty())
        return;
    p.index = index;
    p.luid = luid;
    p.mac = mac;
    p.start();
}
Json NdisCapture::snapshot(std::int64_t minute, Json &rows) {
    auto &p = *p_;
    if (p.active && p.consumer_done.load())
        p.error = "ETW consumer stopped: " + std::to_string(p.processing.load());
    p.query_loss();
    std::map<std::pair<std::int64_t, std::uint64_t>, Json> records;
    {
        std::lock_guard lock(p.mutex);
        records = p.archived;
        for (const auto &b : p.bins)
            if (b.minute >= minute - 119) {
                auto r = p.record(b);
                auto key = std::make_pair(b.minute, p.luid);
                if (records.contains(key)) {
                    for (const char *k : {"events_lost", "buffers_lost"})
                        r[k] = std::max(r[k].get<std::uint64_t>(), records[key][k].get<std::uint64_t>());
                    for (const char *k : {"ipv4_up", "ipv4_down", "ipv6_up", "ipv6_down", "accepted",
                                          "rejected", "unsupported"})
                        r[k] = r[k].get<std::uint64_t>() + records[key][k].get<std::uint64_t>();
                    if (records[key]["quality"] == "incomplete_observation")
                        r["quality"] = "incomplete_observation";
                }
                records[key] = std::move(r);
            }
        for (auto it = p.archived.begin(); it != p.archived.end();)
            if (it->first.first < minute - 119)
                it = p.archived.erase(it);
            else
                ++it;
    }
    std::array<std::uint64_t, 4> sums{};
    bool observed = false, incomplete = false;
    const char *metrics[] = {"ipv4_up",  "ipv4_down", "ipv6_up",     "ipv6_down",
                             "accepted", "rejected",  "events_lost", "buffers_lost"};
    for (const auto &[key, r] : records) {
        if (key.first < minute - 119)
            continue;
        for (const auto *metric : metrics)
            rows.push_back({{"minute", r["minute"]},
                            {"module", "network_ip"},
                            {"metric", r["interface_id"].get<std::string>() + "/" + metric},
                            {"value", r[metric]},
                            {"ip_record", r}});
        rows.push_back({{"minute", r["minute"]},
                        {"module", "network_ip"},
                        {"metric", r["interface_id"].get<std::string>() + "/quality_state"},
                        {"value", r["quality"] == "incomplete_observation" ? 1 : 0},
                        {"ip_record", r}});
        if (key.second == p.luid) {
            for (unsigned i = 0; i < 4; i++)
                sums[i] += r[metrics[i]].get<std::uint64_t>();
            observed |= r["accepted"].get<std::uint64_t>() > 0;
            incomplete |= r["quality"] == "incomplete_observation";
        }
    }
    std::array<std::uint64_t, 4> lifetime;
    std::array<std::uint64_t, 9> reasons;
    {
        std::lock_guard lock(p.mutex);
        lifetime = p.cumulative;
        reasons = p.rejection_reasons;
    }
    const auto tick = GetTickCount64();
    Json rates = Json::array();
    for (unsigned i = 0; i < 4; i++) {
        rates.push_back(p.active && p.last_tick && lifetime[i] >= p.previous[i]
                            ? Json(double(lifetime[i] - p.previous[i]) * 1000 /
                                   double(std::max<std::uint64_t>(1, tick - p.last_tick)))
                            : Json(nullptr));
        p.previous[i] = lifetime[i];
    }
    p.last_tick = tick;
    return {{"rejection_reasons", reasons},
            {"available", observed},
            {"running", p.active},
            {"reference_validated", false},
            {"interface_id", std::to_string(p.luid)},
            {"source", "ETW NDIS-PacketCapture"},
            {"basis", "IP 层长度；IPv4 Total Length / IPv6 40 + Payload Length"},
            {"quality", incomplete ? "incomplete_observation" : "observed_unverified_reference"},
            {"rates", rates},
            {"bytes", sums},
            {"error", p.error},
            {"start_status", p.start_status},
            {"enable_status", p.enable_status},
            {"process_status", p.processing.load()},
            {"events_lost", p.lost},
            {"buffers_lost", p.buffers_lost},
            {"note", "真实接口观测值，尚待独立抓包对照；不等于校园网账单。丢包/分片捕获/卸载/加密/WDS/"
                     "jumbogram 会报告不完整，不估算补齐。"}};
}
} // namespace pulsetrace
