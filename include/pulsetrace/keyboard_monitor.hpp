#pragma once
#include <array>
#include <cstdint>
#include <vector>
namespace pulsetrace {
inline constexpr std::size_t key_slots = 768;
struct KeyboardMinute {
    std::int64_t minute{-1};
    std::array<std::uint64_t, key_slots> presses{}, repeats{};
};
class KeyboardMonitor {
  public:
    KeyboardMonitor() : minutes_(120) {}
    void consume(std::uintptr_t device, std::uint16_t scan, std::uint16_t flags, std::uint16_t vk,
                 std::int64_t minute) noexcept;
    void reset_state() noexcept { devices_ = {}; }
    void remove_device(std::uintptr_t id) noexcept {
        for (auto &d : devices_)
            if (d.used && d.id == id)
                d = {};
    }
    const std::vector<KeyboardMinute> &minutes() const noexcept { return minutes_; }
    std::uint64_t total() const noexcept { return total_; }
    std::uint64_t state_capacity_events() const noexcept { return state_capacity_events_; }

  private:
    struct Device {
        std::uintptr_t id{};
        bool used{};
        std::array<bool, key_slots> held{};
    };
    std::array<Device, 64> devices_{};
    std::vector<KeyboardMinute> minutes_;
    std::uint64_t total_{};
    std::uint64_t state_capacity_events_{};
};
} // namespace pulsetrace
