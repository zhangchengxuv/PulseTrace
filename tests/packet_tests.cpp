#include "pulsetrace/packet_accounting.hpp"
#include "pulsetrace/network_monitor.hpp"
#include "pulsetrace/probe_report.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>
using namespace pulsetrace;
int main() {
    unsigned checks = 0;
    const auto check = [&](bool v) {
        ++checks;
        if (!v)
            throw std::runtime_error("Packet test " + std::to_string(checks));
    };
    try {
        const std::array<std::uint8_t, 6> mac{2, 1, 2, 3, 4, 5};
        std::vector<std::uint8_t> p(14 + 100);
        std::copy(mac.begin(), mac.end(), p.begin() + 6);
        p[0] = 4;
        p[12] = 8;
        p[14] = 0x45;
        p[17] = 100;
        auto r = account_packet(p, 114, mac);
        check(r.result == PacketResult::accepted && r.family == 4 && r.upload && r.ip_bytes == 100);
        std::copy(mac.begin(), mac.end(), p.begin());
        p[6] = 4;
        r = account_packet(p, 114, mac);
        check(r.result == PacketResult::accepted && !r.upload);
        p[0] = 6;
        check(account_packet(p, 114, mac).result == PacketResult::unknown_direction);
        p[0] = 1;
        check(account_packet(p, 114, mac).result == PacketResult::accepted);
        p[0] = 2;
        p[12] = 0x86;
        p[13] = 0xdd;
        p[14] = 0x60;
        p[18] = 0;
        p[19] = 60;
        p[20] = 43;
        r = account_packet(p, 114, mac);
        check(r.result == PacketResult::accepted && r.family == 6 && r.ip_bytes == 100);
        p[19] = 0;
        check(account_packet(p, 114, mac).result == PacketResult::unsupported_jumbogram);
        p[20] = 59;
        check(account_packet(p, 54, mac).ip_bytes == 40);
        check(account_packet(p, 54, mac, 101).result == PacketResult::unsupported_link);
        p[19] = 60;
        check(account_packet(p, 100, mac).result == PacketResult::offload_suspected);
        check(account_packet(std::span(p).first(20), 114, mac).result == PacketResult::truncated);
        std::vector<std::uint8_t> vlan(118);
        std::copy(p.begin(), p.begin() + 12, vlan.begin());
        vlan[12] = 0x81;
        vlan[13] = 0;
        vlan[16] = 0x86;
        vlan[17] = 0xdd;
        std::copy(p.begin() + 14, p.end(), vlan.begin() + 18);
        check(account_packet(vlan, 118, mac).ip_bytes == 100);
        vlan[16] = 0x88;
        vlan[17] = 0x64;
        check(account_packet(vlan, 118, mac).result == PacketResult::unsupported_link);
        std::vector<std::uint8_t> wifi(32 + 100);
        wifi[0] = 8;
        wifi[1] = 2;
        std::copy(mac.begin(), mac.end(), wifi.begin() + 4);
        wifi[10] = 4;
        wifi[24] = 0xaa;
        wifi[25] = 0xaa;
        wifi[26] = 3;
        wifi[30] = 8;
        wifi[32] = 0x45;
        wifi[35] = 100;
        r = account_packet(wifi, 132, mac, 105);
        check(r.result == PacketResult::accepted && !r.upload && r.ip_bytes == 100);
        wifi[24] = 0;
        check(account_packet(wifi, 132, mac, 105).result == PacketResult::unsupported_link);
        wifi[24] = 0xaa;
        wifi[1] = 1;
        std::copy(mac.begin(), mac.end(), wifi.begin() + 10);
        wifi[4] = 4;
        check(account_packet(wifi, 132, mac, 105).upload);
        wifi[1] = 0x41;
        check(account_packet(wifi, 132, mac, 105).result == PacketResult::unsupported_link);
        wifi[1] = 3;
        check(account_packet(wifi, 132, mac, 105).result == PacketResult::unsupported_link);
        // Deterministic truncated-header fuzz: pure parser must not access beyond captured bytes.
        wifi[1] = 1;
        for (std::size_t n = 0; n < 52; n++)
            check(account_packet(std::span(wifi).first(n), 132, mac, 105).result != PacketResult::accepted);
        for (std::size_t n = 52; n < 132; n++)
            check(account_packet(std::span(wifi).first(n), 132, mac, 105).result == PacketResult::accepted);
        CaptureWindow gate;
        gate.start.store(capture_clock());
        NetworkMonitor monitor(&gate);
        auto active = monitor.sample(utc_now_ms() / 60000, true, "");
        std::this_thread::sleep_for(std::chrono::milliseconds(2200));
        active = monitor.sample(utc_now_ms() / 60000, true, "");
        gate.stop.store(capture_clock());
        auto paused = monitor.sample(utc_now_ms() / 60000, false, "");
        auto copy = paused.rows;
        std::this_thread::sleep_for(std::chrono::milliseconds(1200));
        auto after = monitor.sample(utc_now_ms() / 60000, false, "");
        check(copy == after.rows);
        check(!after.status["protocol_capture"]["running"].get<bool>());
        const auto invalid = monitor.sample(utc_now_ms() / 60000, false, "0");
        check(!invalid.status["protocol_capture"]["available"].get<bool>());
        std::cout << checks << " packet/physical capture lifecycle checks passed\n"
                  << active.status["protocol_capture"].dump() << '\n';
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
