#include "pulsetrace/probe_report.hpp"
#include <psapi.h>
#include <algorithm>
#include <charconv>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string_view>

using namespace pulsetrace;
namespace {
std::uint32_t parse(std::string_view value) {
    std::uint32_t result{};
    const auto converted = std::from_chars(value.data(), value.data() + value.size(), result);
    if (converted.ec != std::errc{} || converted.ptr != value.data() + value.size() || result == 0 || result > 604800)
        throw std::invalid_argument("duration must be 1..604800 seconds");
    return result;
}
std::uint64_t private_bytes() {
    PROCESS_MEMORY_COUNTERS_EX memory{}; memory.cb = sizeof(memory);
    if (!GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory), sizeof(memory)))
        throw std::runtime_error("GetProcessMemoryInfo failed");
    return memory.PrivateUsage;
}
std::uint64_t cpu_ticks() {
    FILETIME c{}, e{}, k{}, u{};
    if (!GetProcessTimes(GetCurrentProcess(), &c, &e, &k, &u)) throw std::runtime_error("GetProcessTimes failed");
    return ((static_cast<std::uint64_t>(k.dwHighDateTime) << 32) | k.dwLowDateTime) +
           ((static_cast<std::uint64_t>(u.dwHighDateTime) << 32) | u.dwLowDateTime);
}
}
int main(int argc, char** argv) {
    try {
        std::uint32_t seconds = 5;
        if (argc == 2 && std::string_view(argv[1]) == "--help") {
            std::cout << "mouse_benchmark [--seconds 5]\nIn-process replay only; no input injected, no Raw Input queue or input latency measurement.\n"; return 0;
        }
        if (argc == 3 && std::string_view(argv[1]) == "--seconds") seconds = parse(argv[2]);
        else if (argc != 1) throw std::invalid_argument("Usage: mouse_benchmark [--seconds 5]");
        MouseMonitor mouse;
        const auto data = executable_data_directory();
        const auto path = data / ("mouse-replay-" + std::to_string(utc_now_ms()) + ".json");
        std::ofstream out(path);
        if (!out) throw std::runtime_error("EXE/data is not writable");
        out << "{\"mode\":\"in_process_mouse_statistics_replay\",\"resource_samples\":[";
        // Warm every minute slot and device slot before measuring steady state.
        for (std::uint64_t i = 0; i < 1440; ++i) {
            mouse.consume({1 + i % 16, i, static_cast<std::int64_t>(i), wheel_flag, 15, Source::DeviceReported});
        }
        mouse = MouseMonitor(); // Fresh totals, same reserved capacity; no allocations inside timed path.
        const auto start = std::chrono::steady_clock::now();
        const auto cpu_start = cpu_ticks(), memory_start = private_bytes();
        std::uint64_t memory_max = memory_start, operations{}, cycles{}, sample_index{};
        auto next_sample = start + std::chrono::seconds(1);
        auto now = start;
        // Fixed complete click cycles make the expected counts independently known.
        do {
            for (int batch = 0; batch < 10000; ++batch) {
                const auto button = static_cast<std::size_t>(cycles % 5);
                const auto ms = cycles * 2;
                const auto minute = static_cast<std::int64_t>(cycles / 10000);
                mouse.consume({1, ms, minute, down_flags[button], 0, Source::DeviceReported});
                mouse.consume({1, ms + 1, minute, up_flags[button], 0, Source::DeviceReported});
                mouse.consume({1, ms, minute, wheel_flag, static_cast<std::int16_t>(cycles % 2 ? -15 : 15), Source::DeviceReported});
                ++cycles; operations += 3;
            }
            now = std::chrono::steady_clock::now();
            if (now >= next_sample) {
                const auto memory = private_bytes(); memory_max = std::max(memory_max, memory);
                out << (sample_index++ ? "," : "") << "{\"elapsed_seconds\":" << std::chrono::duration<double>(now - start).count()
                    << ",\"private_bytes\":" << memory << '}';
                next_sample = now + std::chrono::seconds(1);
            }
        } while (now - start < std::chrono::seconds(seconds));
        const auto memory_end = private_bytes();
        const double elapsed = std::chrono::duration<double>(now - start).count();
        const double cpu_seconds = static_cast<double>(cpu_ticks() - cpu_start) / 1e7;
        const auto& counts = mouse.lifetime()[static_cast<std::size_t>(Source::DeviceReported)];
        bool exact = counts.total_clicks() == cycles && counts.wheel_up_events == cycles / 2 &&
                     counts.wheel_down_events == cycles / 2 && counts.wheel_up_delta == cycles / 2 * 15 && counts.wheel_down_delta == cycles / 2 * 15;
        for (std::size_t b = 0; b < 5; ++b) exact = exact && counts.clicks[b] == cycles / 5 && counts.temporal_double_clicks[b] == cycles / 10;
        out << "],\"elapsed_seconds\":" << elapsed << ",\"operations\":" << operations << ",\"operations_per_second\":" << operations / elapsed
            << ",\"expected_clicks\":" << cycles << ",\"observed_clicks\":" << counts.total_clicks()
            << ",\"count_match\":" << (exact ? "true" : "false") << ",\"cpu_percent_one_core\":" << cpu_seconds / elapsed * 100
            << ",\"private_start_bytes\":" << memory_start << ",\"private_end_bytes\":" << memory_end
            << ",\"private_max_bytes\":" << memory_max << ",\"retained_minutes\":" << mouse.query(0, mouse.latest_minute() + 1).size()
            << ",\"warning\":\"Replay throughput does not measure hardware loss, simultaneous keyboard load, or end-to-end input latency\"}\n";
        out.close(); if (!out) throw std::runtime_error("Report write failed");
        std::cout << "Replay " << operations << " samples; exact=" << exact << "; samples/s=" << operations / elapsed
            << "; private memory change=" << static_cast<std::int64_t>(memory_end) - static_cast<std::int64_t>(memory_start)
            << " bytes.\nReport: " << path.string() << '\n';
        return exact ? 0 : 1;
    } catch (const std::exception& e) { std::cerr << "Error: " << e.what() << '\n'; return 1; }
}
