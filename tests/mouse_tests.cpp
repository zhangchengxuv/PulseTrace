#include "pulsetrace/mouse_monitor.hpp"
#include <iostream>
#include <stdexcept>
#include <string>

using namespace pulsetrace;
namespace {
int checks{};
void require(bool ok, const char* what) { ++checks; if (!ok) throw std::runtime_error(what); }
MouseCounts total(const MouseMonitor& monitor) {
    MouseCounts result; for (const auto& source : monitor.lifetime()) result += source; return result;
}
void event(MouseMonitor& m, std::uint16_t flags, std::uint64_t ms = 0, std::int64_t minute = 100,
           std::uintptr_t device = 1, std::int16_t delta = 0, Source source = Source::DeviceReported) {
    m.consume({device, ms, minute, flags, delta, source});
}
void click(MouseMonitor& m, std::size_t button, std::uint64_t ms, std::int64_t minute = 100,
           std::uintptr_t device = 1, Source source = Source::DeviceReported) {
    event(m, down_flags[button], ms, minute, device, 0, source);
    event(m, up_flags[button], ms + 1, minute, device, 0, source);
}
void test_buttons_and_pairing() {
    MouseMonitor m;
    for (std::size_t b = 0; b < 5; ++b) {
        click(m, b, 0); click(m, b, 500); // Inclusive system timeout; time zero valid.
        click(m, b, 600); // A triple click is one pair plus one candidate.
        click(m, b, 1101); // Expired candidate.
        click(m, b, 1200); // Forms second pair.
        require(total(m).clicks[b] == 5, "all five button counts");
        require(total(m).temporal_double_clicks[b] == 2, "non-overlap and timeout boundary");
    }
    MouseMonitor duplicates;
    event(duplicates, 1, 100); event(duplicates, 1, 110); event(duplicates, 2, 120); event(duplicates, 1, 150);
    require(total(duplicates).clicks[0] == 2, "duplicate down suppressed");
    require(duplicates.diagnostics().duplicate_downs == 1, "duplicate diagnostic");
    require(total(duplicates).temporal_double_clicks[0] == 1, "release required for pair");
    MouseMonitor no_release;
    event(no_release, 1, 1); event(no_release, 1, 2);
    require(total(no_release).temporal_double_clicks[0] == 0, "no pair from held button");
    MouseMonitor backwards;
    click(backwards, 0, 1000); click(backwards, 0, 900);
    require(total(backwards).temporal_double_clicks[0] == 0, "monotonic clock regression never pairs");
    MouseMonitor wrap;
    click(wrap, 0, 0xfffffff0ULL); click(wrap, 0, 0x100000010ULL);
    require(total(wrap).temporal_double_clicks[0] == 1, "64-bit tick across 32-bit wrap");
    MouseMonitor changed;
    click(changed, 0, 0); changed.set_double_click_ms(50); click(changed, 0, 1); click(changed, 0, 52);
    require(total(changed).temporal_double_clicks[0] == 0, "settings change resets candidates and applies timeout");
}
void test_devices_and_sources() {
    MouseMonitor m;
    click(m, 0, 10, 100, 1); click(m, 0, 20, 100, 2);
    require(total(m).temporal_double_clicks[0] == 0, "devices cannot form a pair");
    click(m, 0, 30, 100, 1);
    require(total(m).temporal_double_clicks[0] == 1, "same device still pairs");
    m.remove_device(2); click(m, 0, 40, 100, 2);
    require(total(m).temporal_double_clicks[0] == 1, "removed device identity cannot survive reconnect");
    click(m, 0, 45, 100, 2, Source::Injected);
    require(total(m).temporal_double_clicks[0] == 1, "different source evidence cannot form pair");
    MouseMonitor null_device;
    click(null_device, 0, 0, 100, 0, Source::NoDevice); click(null_device, 0, 10, 100, 0, Source::NoDevice);
    require(total(null_device).clicks[0] == 2, "null-device clicks counted");
    require(total(null_device).temporal_double_clicks[0] == 0, "no fabricated identity for null device");
    MouseMonitor capacity;
    for (std::uintptr_t i = 1; i <= 65; ++i) click(capacity, 0, 0, 100, i);
    require(total(capacity).clicks[0] == 65, "device state capacity never drops counts");
    require(capacity.diagnostics().state_capacity_events == 2, "capacity degradation observable");
    capacity.remove_device(1); click(capacity, 0, 100, 100, 65); click(capacity, 0, 200, 100, 65);
    require(total(capacity).temporal_double_clicks[0] == 1, "freed device slot reusable");
}
void test_wheel() {
    MouseMonitor m;
    for (int i = 0; i < 8; ++i) event(m, wheel_flag, i, 100, 1, 15);
    event(m, wheel_flag, 9, 101, 1, -30);
    event(m, wheel_flag, 10, 101, 1, -32768);
    event(m, wheel_flag, 11, 101, 1, 0);
    event(m, horizontal_wheel_flag, 12, 101, 1, -120);
    const auto t = total(m);
    require(t.wheel_up_events == 8 && t.wheel_down_events == 2, "wheel events distinct from detents and zero ignored");
    require(t.wheel_up_delta == 120 && t.wheel_down_delta == 32798, "fractional wheel and signed short minimum");
    require(t.wheel_net_delta() == -32678, "wheel net preserves opposite direction");
    require(t.horizontal_events == 1 && t.horizontal_delta == -120, "horizontal wheel kept separate");
    auto range = m.query(100, 102, Resolution::Hour);
    require(range.size() == 1 && range[0].total().wheel_up_delta == 120, "wheel precision retained through rollup");
}
void test_ranges() {
    MouseMonitor m(1440);
    click(m, 0, 0, 59); click(m, 0, 10, 60);
    auto minute = m.query(59, 60);
    require(minute.size() == 1 && minute[0].total().clicks[0] == 1, "half-open range excludes end");
    auto hours = m.query(59, 61, Resolution::Hour);
    require(hours.size() == 2 && hours[0].utc_minute == 0 && hours[1].utc_minute == 60, "UTC hour boundaries");
    auto days = m.query(59, 61, Resolution::Day);
    require(days.size() == 1 && days[0].total().clicks[0] == 2, "day rollup");
    require(m.query(60, 61)[0].total().temporal_double_clicks[0] == 1, "cross-minute pair belongs to second down");
    click(m, 1, 1000, 1500);
    require(m.query(0, 61).empty(), "expired buckets hidden even when ring slot not overwritten");
    const auto before = m.query(1500, 1501)[0].total().clicks[1];
    click(m, 0, 2000, 60); // Exact ring collision with newer minute 1500.
    require(m.diagnostics().expired_bucket_events == 2, "late out-of-retention events observable");
    require(m.query(1500, 1501)[0].total().clicks[1] == before, "old events never overwrite newer bucket");
    require(total(m).clicks[0] == 3, "lifetime totals retain events outside ring");
    event(m, 0, 3000, 2000); // Movement alone creates no bucket.
    require(m.latest_minute() == 1500, "movement-only packets have no statistics");
    bool threw = false; try { (void)m.query(1, 1); } catch (const std::invalid_argument&) { threw = true; }
    require(threw, "empty query rejected");
    threw = false; try { MouseMonitor invalid(0); } catch (const std::invalid_argument&) { threw = true; }
    require(threw, "zero retention rejected");
    event(m, 1, 0, -1); event(m, 1, 0, 1500, 1, 0, Source::Count);
    require(m.diagnostics().invalid_samples == 2, "invalid sample rejected");
}
void test_large_exact_stream() {
    MouseMonitor m(2);
    constexpr std::uint64_t count = 1000000;
    for (std::uint64_t i = 0; i < count; ++i) {
        const auto button = static_cast<std::size_t>(i % 5);
        click(m, button, i * 2, 100 + static_cast<std::int64_t>(i / 10000));
        event(m, wheel_flag, i * 2, 100 + static_cast<std::int64_t>(i / 10000), 1, i % 2 ? -15 : 15);
    }
    const auto t = total(m);
    for (std::size_t b = 0; b < 5; ++b) {
        require(t.clicks[b] == count / 5, "million-stream exact click count");
        require(t.temporal_double_clicks[b] == count / 10, "million-stream exact pair count");
    }
    require(t.wheel_up_delta == count / 2 * 15 && t.wheel_down_delta == count / 2 * 15, "million-stream exact wheel amount");
    require(t.wheel_up_events == count / 2 && t.wheel_down_events == count / 2, "million-stream exact wheel events");
    require(m.query(0, 1000).size() == 2, "bounded retention after stream");
}
}
int main() {
    try {
        test_buttons_and_pairing(); test_devices_and_sources(); test_wheel(); test_ranges(); test_large_exact_stream();
        std::cout << checks << " checks passed, including 3,000,000 replayed samples.\n"; return 0;
    } catch (const std::exception& e) { std::cerr << "FAIL after " << checks << " checks: " << e.what() << '\n'; return 1; }
}
