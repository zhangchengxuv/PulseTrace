#include "pulsetrace/network_selection.hpp"
#include <iostream>
#include <stdexcept>
#include <vector>
using namespace pulsetrace;
int main() {
    const auto none = std::numeric_limits<std::uint64_t>::max();
    std::vector<NetworkCandidate> adapters{{"ethernet",true,true,none}, {"tun",true,false,0},
                                          {"wifi",true,true,45}, {"offline",false,true,1}};
    auto check = [&](const std::string &expected, const std::string &selected = "",
                     const std::string &previous = "") {
        if (select_network_interface(adapters, selected, previous) != expected)
            throw std::runtime_error("Network interface selection regression");
    };
    check("wifi"); // A connected cable without a route must not hide Wi-Fi traffic.
    check("ethernet", "ethernet"); // Explicit choices are never silently replaced.
    check("missing", "missing");
    adapters[0].default_cost = 20;
    check("ethernet");
    adapters[0].default_cost = 45;
    check("wifi", "", "wifi");
    adapters[2].up = false;
    check("ethernet", "", "wifi");
    adapters[0].default_cost = none;
    check("ethernet"); // Local-only/offline fallback.
    adapters[0].up = false;
    check(""); // Do not automatically mix a TUN with physical observations.
    adapters.clear();
    check("");
    std::cout << "9 route-aware network selection checks passed\n";
}
