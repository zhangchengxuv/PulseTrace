#include "pulsetrace/probe_report.hpp"
#include <fstream>
#include <iomanip>
#include <stdexcept>
#include <string>
#include <vector>

namespace pulsetrace {
std::filesystem::path executable_data_directory() {
    std::vector<wchar_t> path(32768);
    const auto n = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (!n || n >= path.size()) throw std::runtime_error("Cannot resolve executable directory");
    auto data = std::filesystem::path(std::wstring(path.data(), n)).parent_path() / L"data";
    std::filesystem::create_directories(data); // Throw; do not use another location.
    return data;
}
namespace {
void counts_json(std::ostream& out, const MouseCounts& counts) {
    out << "{\"clicks\":{";
    for (std::size_t i = 0; i < 5; ++i) out << (i ? "," : "") << '"' << button_names[i] << "\":" << counts.clicks[i];
    out << "},\"temporal_double_clicks\":{";
    for (std::size_t i = 0; i < 5; ++i) out << (i ? "," : "") << '"' << button_names[i] << "\":" << counts.temporal_double_clicks[i];
    out << "},\"wheel_up_events\":" << counts.wheel_up_events << ",\"wheel_down_events\":" << counts.wheel_down_events
        << ",\"wheel_up_delta\":" << counts.wheel_up_delta << ",\"wheel_down_delta\":" << counts.wheel_down_delta
        << ",\"wheel_net_delta\":" << counts.wheel_net_delta() << ",\"wheel_total_delta\":" << counts.wheel_up_delta + counts.wheel_down_delta
        << ",\"wheel_net_detents\":" << static_cast<double>(counts.wheel_net_delta()) / 120.0
        << ",\"horizontal_events\":" << counts.horizontal_events << ",\"horizontal_delta\":" << counts.horizontal_delta << '}';
}
template<class T> void array_json(std::ostream& out, const T& values) {
    bool first = true; out << '[';
    for (auto value : values) { out << (first ? "" : ",") << value; first = false; }
    out << ']';
}
}
void write_probe_report(std::ostream& out, const MouseMonitor& mouse, const ProbeMetrics& m, std::string_view mode) {
    const auto& d = mouse.diagnostics();
    out << std::fixed << std::setprecision(6)
        << "{\n\"schema_version\":1,\"mode\":\"" << mode << "\",\"time_zone\":\"UTC\","
        << "\"source_classification\":\"device handle evidence only; physical/injected/touchpad origin not guaranteed\","
        << "\"double_click_definition\":\"non-overlapping same-device same-button down-up-down within system time; no spatial test\","
        << "\"retained_minutes\":" << mouse.capacity_minutes() << ",\n\"metrics\":{"
        << "\"double_click_initial_ms\":" << m.double_click_initial_ms << ",\"double_click_final_ms\":" << m.double_click_final_ms
        << ",\"double_click_setting_changes\":" << m.double_click_setting_changes
        << ",\"start_utc_ms\":" << m.start_utc_ms << ",\"end_utc_ms\":" << m.end_utc_ms
        << ",\"elapsed_seconds\":" << m.elapsed_seconds << ",\"cpu_seconds\":" << m.cpu_seconds
        << ",\"cpu_percent_one_core\":" << (m.elapsed_seconds > 0 ? m.cpu_seconds / m.elapsed_seconds * 100 : 0)
        << ",\"mouse_packets\":" << m.mouse_packets << ",\"keyboard_packets\":" << m.keyboard_packets
        << ",\"keyboard_downs\":" << m.keyboard_downs << ",\"read_errors\":" << m.read_errors
        << ",\"message_errors\":" << m.message_errors << ",\"callback_count\":" << m.callback_count
        << ",\"callback_mean_us\":" << m.callback_mean_us << ",\"callback_max_us\":" << m.callback_max_us
        << ",\"callback_bounds_us\":"; array_json(out, callback_bounds_us);
    out << ",\"callback_histogram\":"; array_json(out, m.callback_histogram);
    out << ",\"queue_delay_bounds_ms\":"; array_json(out, queue_bounds_ms);
    out << ",\"queue_delay_histogram\":"; array_json(out, m.queue_delay_histogram);
    out << ",\"private_start_bytes\":" << m.private_start << ",\"private_end_bytes\":" << m.private_end
        << ",\"private_max_bytes\":" << m.private_max << ",\"working_set_max_bytes\":" << m.working_set_max
        << ",\"memory_samples\":" << m.memory_samples << "},\n\"diagnostics\":{\"samples\":" << d.samples
        << ",\"state_capacity_events\":" << d.state_capacity_events << ",\"duplicate_downs\":" << d.duplicate_downs
        << ",\"expired_bucket_events\":" << d.expired_bucket_events << ",\"invalid_samples\":" << d.invalid_samples
        << "},\n\"lifetime_by_source\":{";
    for (std::size_t i = 0; i < source_count; ++i) {
        out << (i ? "," : "") << '"' << source_names[i] << "\":"; counts_json(out, mouse.lifetime()[i]);
    }
    out << "},\n\"minute_buckets\":[";
    bool first = true;
    if (mouse.latest_minute() >= 0) {
        const auto from = mouse.latest_minute() >= static_cast<std::int64_t>(mouse.capacity_minutes()) ?
            mouse.latest_minute() - static_cast<std::int64_t>(mouse.capacity_minutes()) + 1 : 0;
        for (const auto& bucket : mouse.query(from, mouse.latest_minute() + 1)) {
            out << (first ? "" : ",") << "{\"utc_minute\":" << bucket.utc_minute << ",\"sources\":{"; first = false;
            for (std::size_t i = 0; i < source_count; ++i) {
                out << (i ? "," : "") << '"' << source_names[i] << "\":"; counts_json(out, bucket.sources[i]);
            }
            out << "}}";
        }
    }
    out << "]\n}\n";
}
}
