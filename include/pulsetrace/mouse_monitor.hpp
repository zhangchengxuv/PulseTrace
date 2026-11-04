#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace pulsetrace {
// Classification describes available evidence, never proof of physical origin.
enum class Source : std::size_t { DeviceReported, NoDevice, Injected, Touchpad, Unknown, Count };
inline constexpr std::size_t source_count = static_cast<std::size_t>(Source::Count);
inline constexpr std::array<const char*, source_count> source_names{
    "device_reported", "no_device", "injected_reported", "touchpad_reported", "unknown"};
inline constexpr std::array<const char*, 5> button_names{"left", "right", "middle", "mouse4", "mouse5"};
inline constexpr std::array<std::uint16_t, 5> down_flags{1, 4, 16, 64, 256};
inline constexpr std::array<std::uint16_t, 5> up_flags{2, 8, 32, 128, 512};
inline constexpr std::uint16_t wheel_flag = 0x0400, horizontal_wheel_flag = 0x0800;

struct MouseSample {
    std::uintptr_t device{}; // Transient, process-local; never serialized.
    std::uint64_t monotonic_ms{}; // Transient pairing state only.
    std::int64_t utc_minute{};
    std::uint16_t flags{};
    std::int16_t wheel_delta{};
    Source source{Source::Unknown};
};

struct MouseCounts {
    std::array<std::uint64_t, 5> clicks{}, temporal_double_clicks{};
    std::uint64_t wheel_up_events{}, wheel_down_events{};
    std::uint64_t wheel_up_delta{}, wheel_down_delta{}; // Positive magnitudes, no /120 truncation.
    std::uint64_t horizontal_events{};
    std::int64_t horizontal_delta{};
    MouseCounts& operator+=(const MouseCounts& other) noexcept;
    [[nodiscard]] std::uint64_t total_clicks() const noexcept;
    [[nodiscard]] std::int64_t wheel_net_delta() const noexcept;
};
struct MinuteAggregate {
    std::int64_t utc_minute{-1};
    std::array<MouseCounts, source_count> sources{};
    [[nodiscard]] MouseCounts total() const noexcept;
};
enum class Resolution : std::int64_t { Minute = 1, Hour = 60, Day = 1440 };
struct MouseDiagnostics {
    std::uint64_t samples{}, state_capacity_events{}, duplicate_downs{}, expired_bucket_events{}, invalid_samples{};
};

// Single-owner class: call from the input thread; query after stopping or on that
// same thread. No I/O, locks, allocations, or queues on consume().
class MouseMonitor {
public:
    explicit MouseMonitor(std::size_t retained_minutes = 1440, std::uint32_t double_click_ms = 500);
    void consume(const MouseSample& sample) noexcept;
    void remove_device(std::uintptr_t device) noexcept;
    void reset_pairing() noexcept;
    void set_double_click_ms(std::uint32_t value) noexcept;
    [[nodiscard]] std::vector<MinuteAggregate> query(std::int64_t from_minute, std::int64_t to_minute,
                                                   Resolution resolution = Resolution::Minute) const;
    [[nodiscard]] const std::array<MouseCounts, source_count>& lifetime() const noexcept { return lifetime_; }
    [[nodiscard]] const MouseDiagnostics& diagnostics() const noexcept { return diagnostics_; }
    [[nodiscard]] std::size_t capacity_minutes() const noexcept { return minutes_.size(); }
    [[nodiscard]] std::int64_t latest_minute() const noexcept { return latest_minute_; }
private:
    struct ButtonState { bool down{}, candidate{}, released{}; std::uint64_t first_down_ms{}; };
    struct DeviceState {
        bool used{};
        std::uintptr_t device{};
        Source source{Source::Unknown};
        std::array<ButtonState, 5> buttons{};
    };
    DeviceState* device_state(const MouseSample& sample) noexcept;
    std::vector<MinuteAggregate> minutes_;
    std::array<DeviceState, 64> devices_{};
    std::array<MouseCounts, source_count> lifetime_{};
    MouseDiagnostics diagnostics_{};
    std::int64_t latest_minute_{-1};
    std::uint32_t double_click_ms_{};
};
}
