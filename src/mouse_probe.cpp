#include "pulsetrace/probe_report.hpp"
#include <charconv>
#include <fstream>
#include <iostream>
#include <string_view>

namespace {
std::uint32_t number(std::string_view value) {
    std::uint32_t result{};
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size()) throw std::invalid_argument("invalid number");
    return result;
}
void help() {
    std::cout << "MouseMonitor validation only. No database, HTTP service, hooks, or movement recording.\n"
        "Usage: mouse_probe --capture --seconds 30 [--keyboard-count] [--retention-minutes 1440]\n"
        "Explicit --capture starts session-wide mouse statistics for the current desktop.\n"
        "--keyboard-count additionally counts total keyboard down transitions for load validation.\n"
        "Ctrl+C stops. Writes only aggregate JSON to EXE/data after capture has stopped.\n";
}
}
int main(int argc, char** argv) {
    try {
        pulsetrace::ProbeOptions options; bool capture = false;
        for (int i = 1; i < argc; ++i) {
            const std::string_view arg = argv[i];
            if (arg == "--help") { help(); return 0; }
            if (arg == "--capture") capture = true;
            else if (arg == "--keyboard-count") options.keyboard_count = true;
            else if ((arg == "--seconds" || arg == "--retention-minutes") && i + 1 < argc) {
                const auto value = number(argv[++i]);
                if (arg == "--seconds") options.seconds = value; else options.retained_minutes = value;
            } else throw std::invalid_argument("unknown or incomplete argument");
        }
        if (!capture) { help(); return argc == 1 ? 0 : 2; }
        pulsetrace::RawInputHost host(options);
        const auto data = pulsetrace::executable_data_directory();
        const auto report_path = data / ("mouse-probe-" + std::to_string(pulsetrace::utc_now_ms()) + ".json");
        // Test permission before registering; writes are outside input processing.
        std::ofstream output(report_path, std::ios::out | std::ios::trunc);
        if (!output) throw std::runtime_error("EXE/data is not writable; move the application to a writable folder");
        std::cout << "Capture enabled for " << options.seconds << " seconds. No coordinates or input contents are retained.\n";
        host.run();
        pulsetrace::write_probe_report(output, host.mouse(), host.metrics(), "raw_input_capture");
        output.close();
        if (!output) throw std::runtime_error("Failed to save report in EXE/data");
        std::cout << "Aggregate report: " << report_path.string() << '\n';
        std::cout << "Mouse packets=" << host.metrics().mouse_packets << "; read errors=" << host.metrics().read_errors
                  << "; callback max us=" << host.metrics().callback_max_us << '\n';
        return host.metrics().read_errors || host.metrics().message_errors ? 1 : 0;
    } catch (const std::exception& e) { std::cerr << "Error: " << e.what() << '\n'; return 1; }
}
