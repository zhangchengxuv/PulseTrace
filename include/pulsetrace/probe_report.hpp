#pragma once
#include "pulsetrace/raw_input_host.hpp"
#include <filesystem>
#include <iosfwd>
#include <string_view>

namespace pulsetrace {
// Always relative to the executable, never cwd or an unannounced fallback.
std::filesystem::path executable_data_directory();
void write_probe_report(std::ostream& output, const MouseMonitor& mouse, const ProbeMetrics& metrics,
                        std::string_view mode);
}
