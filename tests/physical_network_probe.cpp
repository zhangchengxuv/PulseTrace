#include "pulsetrace/network_monitor.hpp"
#include "pulsetrace/probe_report.hpp"
#include <chrono>
#include <iostream>
#include <thread>
#include <psapi.h>
int main(int argc, char **argv) {
    using namespace pulsetrace;
    CaptureWindow gate;
    gate.start.store(capture_clock());
    NetworkMonitor monitor(&gate);
    int seconds = 5;
    if (argc == 3 && std::string(argv[1]) == "--seconds")
        seconds = std::clamp(std::atoi(argv[2]), 1, 3600);
    const auto cpu = []() {
        FILETIME created{}, exited{}, kernel{}, user{};
        GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user);
        return ((std::uint64_t(kernel.dwHighDateTime) << 32) | kernel.dwLowDateTime) +
               ((std::uint64_t(user.dwHighDateTime) << 32) | user.dwLowDateTime);
    };
    const auto private_bytes = []() {
        PROCESS_MEMORY_COUNTERS_EX p{};
        p.cb = sizeof(p);
        GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&p), sizeof(p));
        return p.PrivateUsage;
    };
    const auto first_private = private_bytes(), first_cpu = cpu();
    auto peak_private = first_private;
    const auto begin = std::chrono::steady_clock::now();
    auto sample = monitor.sample(utc_now_ms() / 60000, true, "");
    for (int i = 0; i < seconds; i++) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        sample = monitor.sample(utc_now_ms() / 60000, true, "");
        peak_private = std::max(peak_private, private_bytes());
    }
    gate.stop.store(capture_clock());
    sample = monitor.sample(utc_now_ms() / 60000, false, "");
    std::cout << Json{{"elapsed_seconds",
                       std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count()},
                      {"cpu_seconds", double(cpu() - first_cpu) / 10000000.0},
                      {"private_bytes_initial", first_private},
                      {"private_bytes_peak", peak_private},
                      {"private_bytes_final", private_bytes()},
                      {"status", sample.status},
                      {"rows", sample.rows},
                      {"generated_network_traffic", false},
                      {"reference_validated", false}}
                     .dump(2)
              << '\n';
    return 0;
}
