#pragma once
#include <array>
#include <cstdint>
#include <span>
namespace pulsetrace {
enum class PacketResult {
    accepted,
    non_ip,
    truncated,
    malformed,
    unknown_direction,
    unsupported_link,
    unsupported_jumbogram,
    offload_suspected
};
struct PacketCount {
    PacketResult result{PacketResult::malformed};
    unsigned family{};
    bool upload{};
    std::uint32_t ip_bytes{};
    unsigned header_offset{};
};
// Reads only Ethernet/VLAN + IP headers. Never copies addresses or payload.
PacketCount account_packet(std::span<const std::uint8_t> header, std::uint32_t original_length,
                           const std::array<std::uint8_t, 6> &local_mac, int link_type = 1) noexcept;
} // namespace pulsetrace
