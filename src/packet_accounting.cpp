#include "pulsetrace/packet_accounting.hpp"
#include <algorithm>
namespace pulsetrace {
PacketCount account_packet(std::span<const std::uint8_t> p, std::uint32_t wire,
                           const std::array<std::uint8_t, 6> &mac, int link) noexcept {
    const auto fail = [](PacketResult r) { return PacketCount{r}; };
    const auto word = [&](std::size_t i) { return (unsigned(p[i]) << 8) | p[i + 1]; };
    std::size_t offset = 0;
    unsigned ether = 0;
    bool source = false, destination = false, multicast = false;
    unsigned padding = 0;
    if (link == 1) {
        if (p.size() < 14 || wire < 14)
            return fail(PacketResult::truncated);
        source = std::equal(mac.begin(), mac.end(), p.begin() + 6);
        destination = std::equal(mac.begin(), mac.end(), p.begin());
        multicast = (p[0] & 1) != 0;
        offset = 14;
        ether = word(12);
        unsigned tags = 0;
        padding = 46;
        while (ether == 0x8100 || ether == 0x88a8) {
            if (++tags > 2)
                return fail(PacketResult::unsupported_link);
            if (p.size() < offset + 4)
                return fail(PacketResult::truncated);
            ether = word(offset + 2);
            offset += 4;
        }
    } else if (link == 105) {
        if (p.size() < 24 || wire < 24)
            return fail(PacketResult::truncated);
        const unsigned control = p[0] | (unsigned(p[1]) << 8);
        if ((control & 3) != 0 || (control & 0xc) != 8 || (control & 0x40))
            return fail(PacketResult::non_ip);
        if ((control & 0x4000) || (control & 0x300) == 0x300)
            return fail(PacketResult::unsupported_link);
        // Station/IBSS sender and receiver. Four-address WDS is deliberately unsupported.
        source = std::equal(mac.begin(), mac.end(), p.begin() + 10);
        destination = std::equal(mac.begin(), mac.end(), p.begin() + 4);
        multicast = (p[4] & 1) != 0;
        offset = 24;
        if (control & 0x80) {
            if (p.size() < offset + 2)
                return fail(PacketResult::truncated);
            if (p[offset] & 0x80)
                return fail(PacketResult::unsupported_link); // A-MSDU needs subframe parsing.
            offset += 2;
            if (control & 0x8000)
                offset += 4;
        }
        if (p.size() < offset + 8)
            return fail(PacketResult::truncated);
        if (p[offset] != 0xaa || p[offset + 1] != 0xaa || p[offset + 2] != 3 || p[offset + 3] != 0 ||
            p[offset + 4] != 0 || (p[offset + 5] != 0 && p[offset + 5] != 0xf8))
            return fail(PacketResult::unsupported_link);
        ether = word(offset + 6);
        offset += 8;
    } else
        return fail(PacketResult::unsupported_link);
    if ((source && destination) || (!source && !destination && !multicast))
        return fail(PacketResult::unknown_direction);
    unsigned family = 0, length = 0;
    if (ether == 0x0800) {
        family = 4;
        if (p.size() < offset + 20)
            return fail(PacketResult::truncated);
        const unsigned ihl = (p[offset] & 15) * 4;
        if ((p[offset] >> 4) != 4 || ihl < 20)
            return fail(PacketResult::malformed);
        if (p.size() < offset + ihl)
            return fail(PacketResult::truncated);
        length = word(offset + 2);
        if (length < ihl)
            return fail(PacketResult::malformed);
    } else if (ether == 0x86dd) {
        family = 6;
        if (p.size() < offset + 40)
            return fail(PacketResult::truncated);
        if ((p[offset] >> 4) != 6)
            return fail(PacketResult::malformed);
        length = word(offset + 4);
        if (!length && p[offset + 6] != 59)
            return fail(PacketResult::unsupported_jumbogram);
        length += 40;
    } else
        return fail(ether <= 1500 || ether == 0x8864 || ether == 0x8847 || ether == 0x8848
                        ? PacketResult::unsupported_link
                        : PacketResult::non_ip);
    if (wire < offset + length || wire - offset > std::max<unsigned>(length, padding) + 4)
        return fail(PacketResult::offload_suspected);
    return {PacketResult::accepted, family, source, length, static_cast<unsigned>(offset)};
}
} // namespace pulsetrace
