#include "pulsetrace/raw_input_host.hpp"
#include <psapi.h>
#include <wtsapi32.h>
#include <algorithm>
#include <atomic>
#include <cstddef>
#include <stdexcept>
#include <string>

namespace pulsetrace {
namespace {
constexpr wchar_t class_name[] = L"PulseTrace.MouseProbe.RawInput";
std::atomic<HWND> control_window{};
BOOL WINAPI console_control(DWORD event) {
    if (event != CTRL_C_EVENT && event != CTRL_BREAK_EVENT && event != CTRL_CLOSE_EVENT) return FALSE;
    const auto window = control_window.load(std::memory_order_relaxed);
    return window && PostMessageW(window, WM_CLOSE, 0, 0);
}
std::uint64_t as_u64(FILETIME time) noexcept {
    return (static_cast<std::uint64_t>(time.dwHighDateTime) << 32) | time.dwLowDateTime;
}
std::uint64_t process_cpu_100ns() noexcept {
    FILETIME created{}, exited{}, kernel{}, user{};
    if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) return 0;
    return as_u64(kernel) + as_u64(user);
}
std::runtime_error windows_error(const char* operation) {
    return std::runtime_error(std::string(operation) + " failed, Win32=" + std::to_string(GetLastError()));
}
}
std::int64_t utc_now_ms() noexcept {
    FILETIME time{}; GetSystemTimeAsFileTime(&time);
    return static_cast<std::int64_t>(as_u64(time) / 10000) - 11644473600000LL;
}
RawInputHost::RawInputHost(ProbeOptions options)
    : options_(options), mouse_(options.retained_minutes) {
    if ((!options.seconds && !options.callbacks) || options.seconds > 604800) throw std::invalid_argument("seconds must be 1..604800");
    if (!QueryPerformanceFrequency(&frequency_)) throw windows_error("QueryPerformanceFrequency");
    metrics_.double_click_initial_ms = metrics_.double_click_final_ms = GetDoubleClickTime();
    mouse_.set_double_click_ms(metrics_.double_click_initial_ms);
}
RawInputHost::~RawInputHost() {
    control_window.store(nullptr, std::memory_order_relaxed);
    SetConsoleCtrlHandler(console_control, FALSE);
    if (registered_) {
        RAWINPUTDEVICE devices[2]{{1, 2, RIDEV_REMOVE, nullptr}, {1, 6, RIDEV_REMOVE, nullptr}};
        RegisterRawInputDevices(devices, options_.keyboard_count ? 2 : 1, sizeof(RAWINPUTDEVICE));
    }
    if (window_) { WTSUnRegisterSessionNotification(window_); KillTimer(window_, 1); DestroyWindow(window_); }
    if (window_class_) UnregisterClassW(class_name, GetModuleHandleW(nullptr));
}
void RawInputHost::sample_memory() noexcept {
    PROCESS_MEMORY_COUNTERS_EX memory{}; memory.cb = sizeof(memory);
    if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory), sizeof(memory))) {
        if (metrics_.memory_samples == 0) metrics_.private_start = memory.PrivateUsage;
        ++metrics_.memory_samples;
        metrics_.private_end = memory.PrivateUsage;
        metrics_.private_max = std::max(metrics_.private_max, static_cast<std::uint64_t>(memory.PrivateUsage));
        metrics_.working_set_max = std::max(metrics_.working_set_max, static_cast<std::uint64_t>(memory.WorkingSetSize));
    }
}
void RawInputHost::sync_clock() noexcept {
    anchor_tick_ = GetTickCount64(); anchor_utc_ms_ = utc_now_ms();
}
void RawInputHost::sync_session()noexcept{
 if(!options_.callbacks)return;
 unsigned long error=0;if(!session_notifications_){session_notifications_=WTSRegisterSessionNotification(window_,NOTIFY_FOR_THIS_SESSION)!=FALSE;if(!session_notifications_)error=GetLastError();}
 options_.callbacks->on_session_status(session_notifications_,error);
 DWORD size=0;LPWSTR memory=nullptr;
 if(WTSQuerySessionInformationW(WTS_CURRENT_SERVER_HANDLE,WTS_CURRENT_SESSION,WTSSessionInfoEx,&memory,&size)){
  if(size>=sizeof(WTSINFOEXW)){const auto *info=reinterpret_cast<const WTSINFOEXW*>(memory);const bool locked=info->Level!=1||info->Data.WTSInfoExLevel1.SessionState!=WTSActive||info->Data.WTSInfoExLevel1.SessionFlags!=WTS_SESSIONSTATE_UNLOCK;
   if(locked!=session_locked_){session_locked_=locked;mouse_.reset_pairing();options_.callbacks->on_session_locked(locked);}
  }WTSFreeMemory(memory);
 }
}
bool RawInputHost::verify_registration()noexcept{
    RAWINPUTDEVICE records[64]{};UINT count=64;bool mouse=false,keyboard=!options_.keyboard_count;
    const auto n=GetRegisteredRawInputDevices(records,&count,sizeof(RAWINPUTDEVICE));
    if(n!=static_cast<UINT>(-1))for(UINT i=0;i<n;i++)if(records[i].usUsagePage==1&&records[i].hwndTarget==window_&&(records[i].dwFlags&RIDEV_INPUTSINK)){if(records[i].usUsage==2)mouse=true;if(records[i].usUsage==6)keyboard=true;}
    if(!mouse||!keyboard){RAWINPUTDEVICE devices[2]{{1,2,RIDEV_INPUTSINK|RIDEV_DEVNOTIFY,window_},{1,6,RIDEV_INPUTSINK|RIDEV_DEVNOTIFY,window_}};
        if(!RegisterRawInputDevices(devices,options_.keyboard_count?2:1,sizeof(RAWINPUTDEVICE))){if(options_.callbacks)options_.callbacks->on_registration_error(GetLastError());return false;}
    }
    registered_=true;if(options_.callbacks)options_.callbacks->on_ready(window_);return true;
}
void RawInputHost::run() {
    if (window_class_) throw std::logic_error("RawInputHost is single-use");
    WNDCLASSEXW wc{}; wc.cbSize = sizeof(wc); wc.lpfnWndProc = window_proc;
    wc.hInstance = GetModuleHandleW(nullptr); wc.lpszClassName = class_name;
    window_class_ = RegisterClassExW(&wc);
    if (!window_class_) throw windows_error("RegisterClassEx");
    window_ = CreateWindowExW(0, class_name, L"PulseTrace mouse validation", 0, 0, 0, 0, 0,
                              nullptr, nullptr, wc.hInstance, this);
    if (!window_) throw windows_error("CreateWindowEx");
    RAWINPUTDEVICE devices[2]{{1, 2, RIDEV_INPUTSINK | RIDEV_DEVNOTIFY, window_},
                              {1, 6, RIDEV_INPUTSINK | RIDEV_DEVNOTIFY, window_}};
    if (!RegisterRawInputDevices(devices, options_.keyboard_count ? 2 : 1, sizeof(RAWINPUTDEVICE)))
        throw windows_error("RegisterRawInputDevices");
    registered_ = true;
    if(options_.callbacks){session_notifications_=WTSRegisterSessionNotification(window_,NOTIFY_FOR_THIS_SESSION)!=FALSE;sync_session();}
    if (!SetTimer(window_, 1, 1000, nullptr)) throw windows_error("SetTimer");
    if (!options_.callbacks) {
        control_window.store(window_, std::memory_order_relaxed);
        if (!SetConsoleCtrlHandler(console_control, TRUE)) throw windows_error("SetConsoleCtrlHandler");
    }
    sync_clock(); start_tick_ = anchor_tick_; metrics_.start_utc_ms = anchor_utc_ms_;
    sample_memory();
    if (options_.callbacks) options_.callbacks->on_ready(window_);
    auto last_memory_tick = start_tick_,last_session_tick=start_tick_;
    LARGE_INTEGER start{}, end{}; QueryPerformanceCounter(&start);
    const auto cpu_start = process_cpu_100ns();
    MSG message{};
    for (;;) {
        const auto result = GetMessageW(&message, nullptr, 0, 0);
        if (result == -1) { ++metrics_.message_errors; throw windows_error("GetMessage"); }
        if (!result) break;
        TranslateMessage(&message); DispatchMessageW(&message);
        // Check outside WM_TIMER as raw input can keep low-priority timers pending.
        const auto now = GetTickCount64();
        if (now - last_memory_tick >= 1000) {
            sample_memory(); last_memory_tick = now;
            if (options_.callbacks) {options_.callbacks->on_input_metrics(metrics_.mouse_packets,metrics_.keyboard_packets,metrics_.read_errors);options_.callbacks->on_tick(mouse_);}
        }
        if(now-last_session_tick>=5000){sync_session();verify_registration();last_session_tick=now;}
        if (options_.seconds && now - start_tick_ >= static_cast<std::uint64_t>(options_.seconds) * 1000) break;
    }
    // Stop delivery before any reporting I/O, even though the window stays alive
    // until RAII cleanup. Retain registered_ on failure so the destructor retries.
    RAWINPUTDEVICE removed[2]{{1, 2, RIDEV_REMOVE, nullptr}, {1, 6, RIDEV_REMOVE, nullptr}};
    if (!RegisterRawInputDevices(removed, options_.keyboard_count ? 2 : 1, sizeof(RAWINPUTDEVICE)))
        throw windows_error("Unregister Raw Input");
    registered_ = false;
    if (options_.callbacks) {options_.callbacks->on_input_metrics(metrics_.mouse_packets,metrics_.keyboard_packets,metrics_.read_errors);options_.callbacks->on_tick(mouse_);}
    KillTimer(window_, 1);
    QueryPerformanceCounter(&end); sample_memory(); metrics_.end_utc_ms = utc_now_ms();
    metrics_.elapsed_seconds = static_cast<double>(end.QuadPart - start.QuadPart) / frequency_.QuadPart;
    metrics_.cpu_seconds = static_cast<double>(process_cpu_100ns() - cpu_start) / 1e7;
    metrics_.callback_max_us = static_cast<double>(metrics_.callback_qpc_max) * 1e6 / frequency_.QuadPart;
    if (metrics_.callback_count) metrics_.callback_mean_us =
        static_cast<double>(metrics_.callback_qpc_total) * 1e6 / frequency_.QuadPart / metrics_.callback_count;
}
LRESULT CALLBACK RawInputHost::window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) noexcept {
    auto* self = reinterpret_cast<RawInputHost*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        self = static_cast<RawInputHost*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    return self ? self->handle_message(window, message, wparam, lparam) : DefWindowProcW(window, message, wparam, lparam);
}
LRESULT RawInputHost::handle_message(HWND window, UINT message, WPARAM wparam, LPARAM lparam) noexcept {
    switch (message) {
    case WM_INPUT:
        handle_raw(lparam);
        // Required cleanup for foreground raw input, performed after reading.
        return DefWindowProcW(window, message, wparam, lparam);
    case WM_INPUT_DEVICE_CHANGE:
        // Reset on arrival too: Windows may reuse a removed device's handle.
        mouse_.remove_device(reinterpret_cast<std::uintptr_t>(reinterpret_cast<HANDLE>(lparam)));
        if(options_.callbacks)options_.callbacks->on_device_removed(static_cast<std::uintptr_t>(lparam));return 0;
    case WM_WTSSESSION_CHANGE:
        if(options_.callbacks&&(wparam==WTS_SESSION_LOCK||wparam==WTS_SESSION_UNLOCK||wparam==WTS_CONSOLE_DISCONNECT||wparam==WTS_REMOTE_DISCONNECT)){
            options_.callbacks->on_tick(mouse_);mouse_.reset_pairing();sync_clock();
            session_locked_=wparam!=WTS_SESSION_UNLOCK;options_.callbacks->on_session_locked(session_locked_);
            if(wparam==WTS_SESSION_UNLOCK)verify_registration();
        }else if(wparam==WTS_REMOTE_CONNECT||wparam==WTS_CONSOLE_CONNECT)sync_session();return 0;
    case WM_SETTINGCHANGE: {
        const auto timeout = GetDoubleClickTime();
        if (timeout != metrics_.double_click_final_ms) {
            ++metrics_.double_click_setting_changes;
            metrics_.double_click_final_ms = timeout;
            mouse_.set_double_click_ms(timeout);
        }
        break;
    }
    case WM_TIMECHANGE:
        sync_clock();mouse_.reset_pairing();break;
    case WM_POWERBROADCAST:
        if(options_.callbacks&&(wparam==PBT_APMSUSPEND||wparam==PBT_APMRESUMEAUTOMATIC||wparam==PBT_APMRESUMESUSPEND)){
            options_.callbacks->on_tick(mouse_);options_.callbacks->on_power_suspended(wparam==PBT_APMSUSPEND);
            if(wparam!=PBT_APMSUSPEND)verify_registration();
        }
        sync_clock();mouse_.reset_pairing();return TRUE;
    case WM_QUERYENDSESSION:
        if(options_.callbacks){options_.callbacks->on_tick(mouse_);options_.callbacks->on_flush_requested();}return TRUE;
    case WM_ENDSESSION:
        if(options_.callbacks&&wparam){options_.callbacks->on_tick(mouse_);options_.callbacks->on_power_suspended(true);}return 0;
    case WM_APP+18:
        if(options_.callbacks){options_.callbacks->on_tick(mouse_);options_.callbacks->on_flush_requested();}return 1;
    case WM_TIMER:
        return 0; // Wake idle pump for 1 Hz resource sampling; never polls input state.
    case WM_APP+16:
        mouse_.reset_pairing(); return 0;
    case WM_APP+17:
        mouse_=MouseMonitor(options_.retained_minutes,GetDoubleClickTime());
        if(options_.callbacks)options_.callbacks->on_reset();return 1;
    case WM_CLOSE:
        PostQuitMessage(0); return 0;
    default: break;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}
void RawInputHost::handle_raw(LPARAM lparam) noexcept {
    LARGE_INTEGER start{}, end{}; QueryPerformanceCounter(&start);
    RAWINPUT input{}; UINT bytes = sizeof(input);
    const auto read = GetRawInputData(reinterpret_cast<HRAWINPUT>(lparam), RID_INPUT, &input, &bytes, sizeof(RAWINPUTHEADER));
    if (read == static_cast<UINT>(-1) || read < sizeof(RAWINPUTHEADER) ||
        input.header.dwSize < sizeof(RAWINPUTHEADER) || input.header.dwSize > read) {
        ++metrics_.read_errors;
    } else {
        const auto now = GetTickCount64();
        const auto delay = static_cast<DWORD>(static_cast<DWORD>(now) - static_cast<DWORD>(GetMessageTime()));
        const auto delay_index = static_cast<std::size_t>(std::lower_bound(queue_bounds_ms.begin(), queue_bounds_ms.end(), delay) - queue_bounds_ms.begin());
        ++metrics_.queue_delay_histogram[delay_index];
        const auto event_tick = now >= delay ? now - delay : now;
        const auto event_utc = anchor_utc_ms_ + static_cast<std::int64_t>(event_tick) - static_cast<std::int64_t>(anchor_tick_);
        if (input.header.dwType == RIM_TYPEMOUSE && read >= offsetof(RAWINPUT, data) + sizeof(RAWMOUSE)) {
            ++metrics_.mouse_packets;
            // Only these fields cross into the statistics module. No coordinates,
            // movement deltas, raw buttons, or extra-information fingerprints.
            if (!options_.callbacks || options_.callbacks->capture_enabled()) mouse_.consume({reinterpret_cast<std::uintptr_t>(input.header.hDevice), event_tick, event_utc / 60000,
                            input.data.mouse.usButtonFlags, static_cast<std::int16_t>(input.data.mouse.usButtonData),
                            input.header.hDevice ? Source::DeviceReported : Source::NoDevice});
        } else if (input.header.dwType == RIM_TYPEKEYBOARD && read >= offsetof(RAWINPUT, data) + sizeof(RAWKEYBOARD)) {
            // Optional independent load-validation sink: total transitions only.
            // This is not an implementation of the future KeyboardMonitor.
            ++metrics_.keyboard_packets;
            if (!(input.data.keyboard.Flags & RI_KEY_BREAK)) ++metrics_.keyboard_downs;
            if (options_.callbacks && options_.callbacks->capture_enabled()) {
                auto scan=input.data.keyboard.MakeCode, flags=input.data.keyboard.Flags;
                if(!scan){const auto mapped=MapVirtualKeyW(input.data.keyboard.VKey,MAPVK_VK_TO_VSC_EX);scan=static_cast<USHORT>(mapped&0xff);if((mapped&0xff00)==0xe000)flags|=RI_KEY_E0;else if((mapped&0xff00)==0xe100)flags|=RI_KEY_E1;}
                options_.callbacks->on_keyboard(reinterpret_cast<std::uintptr_t>(input.header.hDevice),scan,flags,input.data.keyboard.VKey,event_utc/60000);
            }
        } else { ++metrics_.read_errors; }
    }
    SecureZeroMemory(&input, sizeof(input));
    QueryPerformanceCounter(&end);
    const auto ticks = static_cast<std::uint64_t>(end.QuadPart - start.QuadPart);
    ++metrics_.callback_count; metrics_.callback_qpc_total += ticks;
    metrics_.callback_qpc_max = std::max(metrics_.callback_qpc_max, ticks);
    const double us = static_cast<double>(ticks) * 1e6 / frequency_.QuadPart;
    const auto index = static_cast<std::size_t>(std::lower_bound(callback_bounds_us.begin(), callback_bounds_us.end(), us) - callback_bounds_us.begin());
    ++metrics_.callback_histogram[index];
}
static_assert(RI_MOUSE_LEFT_BUTTON_DOWN == down_flags[0] && RI_MOUSE_BUTTON_5_UP == up_flags[4]);
static_assert(RI_MOUSE_WHEEL == wheel_flag && RI_MOUSE_HWHEEL == horizontal_wheel_flag);
}
