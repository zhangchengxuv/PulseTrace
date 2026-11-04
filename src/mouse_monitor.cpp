#include "pulsetrace/mouse_monitor.hpp"
#include <algorithm>
#include <stdexcept>

namespace pulsetrace {
MouseCounts& MouseCounts::operator+=(const MouseCounts& o) noexcept {
    for (std::size_t i = 0; i < 5; ++i) {
        clicks[i] += o.clicks[i]; temporal_double_clicks[i] += o.temporal_double_clicks[i];
    }
    wheel_up_events += o.wheel_up_events; wheel_down_events += o.wheel_down_events;
    wheel_up_delta += o.wheel_up_delta; wheel_down_delta += o.wheel_down_delta;
    horizontal_events += o.horizontal_events; horizontal_delta += o.horizontal_delta;
    return *this;
}
std::uint64_t MouseCounts::total_clicks() const noexcept {
    std::uint64_t count{}; for (auto n : clicks) count += n; return count;
}
std::int64_t MouseCounts::wheel_net_delta() const noexcept {
    return static_cast<std::int64_t>(wheel_up_delta) - static_cast<std::int64_t>(wheel_down_delta);
}
MouseCounts MinuteAggregate::total() const noexcept {
    MouseCounts counts; for (const auto& source : sources) counts += source; return counts;
}
MouseMonitor::MouseMonitor(std::size_t retained_minutes, std::uint32_t double_click_ms)
    : double_click_ms_(double_click_ms) {
    if (retained_minutes == 0 || retained_minutes > 10080) throw std::invalid_argument("retention must be 1..10080 minutes");
    minutes_.resize(retained_minutes);
}
void MouseMonitor::reset_pairing() noexcept { devices_ = {}; }
void MouseMonitor::set_double_click_ms(std::uint32_t value) noexcept {
    double_click_ms_ = value; reset_pairing();
}
void MouseMonitor::remove_device(std::uintptr_t device) noexcept {
    for (auto& state : devices_) if (state.used && state.device == device) state = {};
}
MouseMonitor::DeviceState* MouseMonitor::device_state(const MouseSample& sample) noexcept {
    // Null handles may combine multiple devices. Counting pairs would invent identity.
    if (!sample.device) return nullptr;
    DeviceState* free_slot{};
    for (auto& state : devices_) {
        if (state.used && state.device == sample.device && state.source == sample.source) return &state;
        if (!state.used && !free_slot) free_slot = &state;
    }
    if (!free_slot) { ++diagnostics_.state_capacity_events; return nullptr; }
    *free_slot = {}; free_slot->used = true; free_slot->device = sample.device; free_slot->source = sample.source;
    return free_slot;
}
void MouseMonitor::consume(const MouseSample& sample) noexcept {
    ++diagnostics_.samples;
    const auto source_index = static_cast<std::size_t>(sample.source);
    if (sample.utc_minute < 0 || source_index >= source_count) { ++diagnostics_.invalid_samples; return; }
    if (sample.flags == 0) return; // Movement-only packet: don't retain any data.
    latest_minute_ = std::max(latest_minute_, sample.utc_minute);
    MinuteAggregate* bucket{};
    if (latest_minute_ - sample.utc_minute < static_cast<std::int64_t>(minutes_.size())) {
        bucket = &minutes_[static_cast<std::size_t>(sample.utc_minute) % minutes_.size()];
        if (bucket->utc_minute != sample.utc_minute) { *bucket = {}; bucket->utc_minute = sample.utc_minute; }
    } else { ++diagnostics_.expired_bucket_events; }
    MouseCounts delta;
    auto* state = (sample.flags & 0x03ff) ? device_state(sample) : nullptr;
    for (std::size_t i = 0; i < 5; ++i) {
        if (sample.flags & down_flags[i]) {
            auto* button = state ? &state->buttons[i] : nullptr;
            if (button && button->down) { ++diagnostics_.duplicate_downs; }
            else {
                ++delta.clicks[i];
                if (button) {
                    if (button->candidate && button->released && sample.monotonic_ms >= button->first_down_ms &&
                        sample.monotonic_ms - button->first_down_ms <= double_click_ms_) {
                        ++delta.temporal_double_clicks[i]; button->candidate = false;
                    } else {
                        button->candidate = true; button->first_down_ms = sample.monotonic_ms;
                    }
                    button->down = true; button->released = false;
                }
            }
        }
        if ((sample.flags & up_flags[i]) && state) {
            state->buttons[i].down = false; state->buttons[i].released = true;
        }
    }
    const auto wheel = static_cast<std::int32_t>(sample.wheel_delta);
    if (sample.flags & wheel_flag) {
        if (wheel > 0) { ++delta.wheel_up_events; delta.wheel_up_delta += static_cast<std::uint64_t>(wheel); }
        if (wheel < 0) { ++delta.wheel_down_events; delta.wheel_down_delta += static_cast<std::uint64_t>(-wheel); }
    }
    if ((sample.flags & horizontal_wheel_flag) && wheel != 0) {
        ++delta.horizontal_events; delta.horizontal_delta += wheel;
    }
    lifetime_[source_index] += delta;
    if (bucket) bucket->sources[source_index] += delta;
}
std::vector<MinuteAggregate> MouseMonitor::query(std::int64_t from, std::int64_t to, Resolution resolution) const {
    const auto width = static_cast<std::int64_t>(resolution);
    if (from < 0 || to <= from || (width != 1 && width != 60 && width != 1440))
        throw std::invalid_argument("query needs a nonempty half-open UTC minute range and valid resolution");
    std::vector<MinuteAggregate> selected;
    for (const auto& bucket : minutes_) {
        if (bucket.utc_minute >= from && bucket.utc_minute < to &&
            latest_minute_ - bucket.utc_minute < static_cast<std::int64_t>(minutes_.size())) selected.push_back(bucket);
    }
    std::sort(selected.begin(), selected.end(), [](const auto& a, const auto& b) { return a.utc_minute < b.utc_minute; });
    std::vector<MinuteAggregate> output;
    for (const auto& bucket : selected) {
        const auto group = (bucket.utc_minute / width) * width;
        if (output.empty() || output.back().utc_minute != group) output.push_back({group, {}});
        for (std::size_t i = 0; i < source_count; ++i) output.back().sources[i] += bucket.sources[i];
    }
    return output;
}
}
