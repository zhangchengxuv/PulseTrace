#include "pulsetrace/keyboard_monitor.hpp"
namespace pulsetrace {
void KeyboardMonitor::consume(std::uintptr_t device, std::uint16_t scan, std::uint16_t flags,
                              std::uint16_t vk, std::int64_t minute) noexcept {
    if (scan == 0 || scan > 255 || vk == 255 || minute < 0 || ((flags & 2) && (scan == 42 || scan == 54)))
        return;
    const auto key = static_cast<std::size_t>(scan) + ((flags & 2) ? 256 : ((flags & 4) ? 512 : 0));
    Device *state = nullptr;
    Device *free = nullptr;
    for (auto &d : devices_) {
        if (d.used && d.id == device) {
            state = &d;
            break;
        }
        if (!d.used && !free)
            free = &d;
    }
    if (!state && free) {
        state = free;
        state->used = true;
        state->id = device;
    }
    if (!state)
        ++state_capacity_events_;
    if (flags & 1) {
        if (state)
            state->held[key] = false;
        return;
    }
    auto &bucket = minutes_[static_cast<std::size_t>(minute) % minutes_.size()];
    if (bucket.minute != minute) {
        bucket = {};
        bucket.minute = minute;
    }
    if (state && state->held[key] && vk != 0x13)
        ++bucket.repeats[key];
    else {
        ++bucket.presses[key];
        ++total_;
    }
    if (state)
        state->held[key] = vk != 0x13;
}
} // namespace pulsetrace
