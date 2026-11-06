#include "pulsetrace/monthly_traffic.hpp"
#include "pulsetrace/probe_report.hpp"
#include <psapi.h>
#include <chrono>
#include <iostream>

using namespace pulsetrace;
namespace {
struct Resources {
    std::uint64_t cpu{}, writes{}, write_operations{}, private_bytes{};
};
Resources resources() {
    FILETIME created{}, exited{}, kernel{}, user{};
    IO_COUNTERS io{};
    PROCESS_MEMORY_COUNTERS_EX memory{};
    memory.cb = sizeof(memory);
    if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user) ||
        !GetProcessIoCounters(GetCurrentProcess(), &io) ||
        !GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&memory), sizeof(memory)))
        throw std::runtime_error("Cannot sample process resource counters");
    const auto ticks = [](FILETIME value) {
        return (std::uint64_t(value.dwHighDateTime) << 32) | value.dwLowDateTime;
    };
    return {ticks(kernel) + ticks(user), io.WriteTransferCount, io.WriteOperationCount, memory.PrivateUsage};
}
void verify(const Json &actual, const Json &expected) {
    for (const char *key : {"upload_bytes", "download_bytes", "ipv4_up", "ipv4_down", "ipv6_up", "ipv6_down", "accepted", "quality"})
        if (actual.at(key) != expected.at(key)) throw std::runtime_error(std::string("Recovery mismatch: ") + key);
}
}
int main(int argc, char **argv) {
    try {
        const int minutes = argc > 1 ? std::stoi(argv[1]) : 20160;
        const int rounds = argc > 2 ? std::stoi(argv[2]) : 3;
        if (argc > 3 || minutes < 1 || minutes > 44640 || rounds < 1 || rounds > 5)
            throw std::invalid_argument("Usage: monthly_recovery_benchmark [minutes:1..44640] [rounds:1..5]");
        const auto directory = executable_data_directory() / "tests" /
            ("monthly-recovery-" + std::to_string(utc_now_ms()) + "-" + std::to_string(GetCurrentProcessId()));
        std::filesystem::create_directories(directory);
        DatabaseManager source(directory / "source.db");
        Json batch = Json::array();
        // Deterministic synthetic aggregates only: no capture, sockets or user database access.
        constexpr std::int64_t first = 29924640; // 2026-11-24 UTC; default fixture spans two months.
        for (int i = 0; i < minutes; ++i) {
            const auto minute = first + i;
            const int up = 1000 + i % 61, down = 2000 + i % 97;
            for (const auto &[metric, value] : {std::pair{"interface_upload_bytes", up}, std::pair{"interface_download_bytes", down}})
                batch.push_back({{"minute", minute}, {"module", "network"}, {"metric", metric}, {"value", value}});
            Json ip = {{"minute", minute}, {"interface_id", "fixture"}, {"source", "benchmark_fixture"},
                       {"basis", "fixture_ip_lengths"}, {"quality", "observed_unverified_reference"},
                       {"ipv4_up", up / 2}, {"ipv4_down", down / 2}, {"ipv6_up", up - up / 2}, {"ipv6_down", down - down / 2},
                       {"accepted", 2}, {"rejected", 0}, {"unsupported", 0}, {"events_lost", 0}, {"buffers_lost", 0}};
            batch.push_back({{"minute", minute}, {"module", "network_ip"}, {"metric", "fixture/quality_state"}, {"value", 0}, {"ip_record", ip}});
            if (batch.size() >= 768 || i + 1 == minutes) {source.save("fixture", batch); batch = Json::array();}
        }
        const auto expected = source.traffic_summary();
        Json runs = Json::array();
        for (int round = 0; round < rounds; ++round) {
            MonthlyTrafficStore archive(directory / ("round-" + std::to_string(round)));
            for (const char *mode : {"fresh", "replay"}) {
                const auto before = resources();
                const auto start = std::chrono::steady_clock::now();
                archive.recover(source);
                const auto elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
                const auto after = resources();
                const auto saved = archive.query("all");
                verify(saved.at("totals"), expected);
                runs.push_back({{"round", round + 1}, {"mode", mode}, {"elapsed_ms", elapsed},
                                {"cpu_ms", (after.cpu - before.cpu) / 10000.0}, {"write_bytes", after.writes - before.writes},
                                {"write_operations", after.write_operations - before.write_operations},
                                {"private_bytes_before", before.private_bytes}, {"private_bytes_after", after.private_bytes},
                                {"months", saved.at("months").size()}, {"totals_verified", true}});
            }
        }
        std::cout << Json{{"fixture_minutes", minutes}, {"fixture_count_rows", minutes * 3}, {"fixture_protocol_rows", minutes},
                         {"runs", runs}, {"scope", "Only archive recovery timed; seeding and total verification excluded. Process CPU time; Windows process I/O bytes, not physical disk writes. Private Bytes sampled at boundaries, not peak. OS cache not cleared."}}.dump(2) << '\n';
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
