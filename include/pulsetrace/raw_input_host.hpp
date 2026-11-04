#pragma once
#include "pulsetrace/mouse_monitor.hpp"
#include <windows.h>
#include <array>
#include <cstdint>

namespace pulsetrace {
struct ProbeMetrics {
    std::uint32_t double_click_initial_ms{}, double_click_final_ms{};
    std::uint64_t double_click_setting_changes{};
    std::uint64_t mouse_packets{}, keyboard_packets{}, keyboard_downs{}, read_errors{}, message_errors{};
    std::uint64_t callback_count{}, callback_qpc_total{}, callback_qpc_max{};
    std::array<std::uint64_t, 12> callback_histogram{};
    std::array<std::uint64_t, 11> queue_delay_histogram{};
    std::uint64_t private_start{}, private_end{}, private_max{}, working_set_max{}, memory_samples{};
    double elapsed_seconds{}, cpu_seconds{}, callback_max_us{}, callback_mean_us{};
    std::int64_t start_utc_ms{}, end_utc_ms{};
};
inline constexpr std::array<double, 11> callback_bounds_us{1, 2, 5, 10, 25, 50, 100, 250, 500, 1000, 5000};
inline constexpr std::array<std::uint64_t, 10> queue_bounds_ms{0, 1, 2, 5, 10, 25, 50, 100, 250, 1000};
struct InputCallbacks {
    virtual ~InputCallbacks() = default;
    virtual bool capture_enabled() const noexcept = 0;
    virtual void on_ready(HWND window) noexcept = 0;
    virtual void on_keyboard(std::uintptr_t device, USHORT scan, USHORT flags, USHORT vk, std::int64_t minute) noexcept = 0;
    virtual void on_tick(const MouseMonitor& mouse) noexcept = 0;
    virtual void on_reset() noexcept = 0;
    virtual void on_device_removed(std::uintptr_t device) noexcept = 0;
    virtual void on_session_locked(bool locked) noexcept = 0;
    virtual void on_input_metrics(std::uint64_t mouse,std::uint64_t keyboard,std::uint64_t errors) noexcept = 0;
};
struct ProbeOptions { std::uint32_t seconds{10}; std::size_t retained_minutes{1440}; bool keyboard_count{}; InputCallbacks* callbacks{}; };

// Sole owner of Raw Input registration for this process. Both independent
// consumers share one message pump. It does not use hooks or NOLEGACY.
class RawInputHost {
public:
    explicit RawInputHost(ProbeOptions options);
    ~RawInputHost();
    RawInputHost(const RawInputHost&) = delete;
    RawInputHost& operator=(const RawInputHost&) = delete;
    void run();
    [[nodiscard]] const MouseMonitor& mouse() const noexcept { return mouse_; }
    [[nodiscard]] const ProbeMetrics& metrics() const noexcept { return metrics_; }
private:
    static LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) noexcept;
    LRESULT handle_message(HWND window, UINT message, WPARAM wparam, LPARAM lparam) noexcept;
    void handle_raw(LPARAM lparam) noexcept;
    void sample_memory() noexcept;
    void sync_clock() noexcept;
    ProbeOptions options_;
    MouseMonitor mouse_;
    ProbeMetrics metrics_;
    HWND window_{};
    ATOM window_class_{};
    LARGE_INTEGER frequency_{};
    std::uint64_t anchor_tick_{}, start_tick_{};
    std::int64_t anchor_utc_ms_{};
    bool registered_{};
};
[[nodiscard]] std::int64_t utc_now_ms() noexcept;
}
