#pragma once
#include <cstdint>
#include <limits>
#include <span>
#include <string>
namespace pulsetrace {
struct NetworkCandidate {
    std::string id;
    bool up{}, physical{};
    std::uint64_t default_cost{std::numeric_limits<std::uint64_t>::max()};
};
// Prefer a routed physical adapter; retain the current adapter for equal costs.
inline std::string select_network_interface(std::span<const NetworkCandidate> candidates,
                                            const std::string &selected,
                                            const std::string &previous) {
    if (!selected.empty()) return selected;
    const NetworkCandidate *best = nullptr;
    for (const auto &candidate : candidates) {
        if (!candidate.up || !candidate.physical) continue;
        if (!best || candidate.default_cost < best->default_cost ||
            (candidate.default_cost == best->default_cost && candidate.id == previous))
            best = &candidate;
    }
    return best ? best->id : std::string{};
}
} // namespace pulsetrace
