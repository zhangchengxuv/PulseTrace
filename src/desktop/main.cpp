#include "pulsetrace/core_service.hpp"
#include "pulsetrace/desktop/webview_host.hpp"
#include "pulsetrace/probe_report.hpp"
#include "pulsetrace/protocol.hpp"
#include <commdlg.h>
#include <fstream>
#include <memory>
#include <shellapi.h>
#include <shobjidl.h>
#include <thread>
using namespace pulsetrace;
namespace {
constexpr UINT tray_message = WM_APP + 1, response_message = WM_APP + 2;
struct Response {
    std::uint64_t generation;
    Json json;
};
struct DesktopOptions {
    bool selftest{}, background{}, expect_failure{}, crash_test{}, early_close{};
    int cycles{2}, core_seconds{};
    std::filesystem::path data;
    std::string page{"dashboard"};
};
class DesktopApplication {
  public:
    int run(HINSTANCE instance, const DesktopOptions &options);
    ~DesktopApplication() {
        if (exit_thread_.joinable())
            exit_thread_.join();
        if (core_)
            core_->stop();
        if (tray_added_)
            Shell_NotifyIconW(NIM_DELETE, &nid_);
        if (instance_mutex_)
            CloseHandle(instance_mutex_);
    }

  private:
    static LRESULT CALLBACK proc(HWND h, UINT m, WPARAM w, LPARAM l) {
        auto *app = reinterpret_cast<DesktopApplication *>(GetWindowLongPtrW(h, GWLP_USERDATA));
        if (m == WM_NCCREATE) {
            app = static_cast<DesktopApplication *>(reinterpret_cast<CREATESTRUCTW *>(l)->lpCreateParams);
            SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
        }
        if (app)
            try {
                return app->message(h, m, w, l);
            } catch (const std::exception &e) {
                app->diagnostic(e.what());
                return 0;
            }
        return DefWindowProcW(h, m, w, l);
    }
    LRESULT message(HWND, UINT, WPARAM, LPARAM);
    void open();
    void hide();
    void tray();
    void menu();
    void quit();
    void command(Json request);
    void diagnostic(const std::string &text);
    void save_window();
    HWND window_{};
    HINSTANCE instance_{};
    std::filesystem::path data_, exe_;
    std::unique_ptr<CoreService> core_;
    std::unique_ptr<WebViewHost> web_;
    std::thread exit_thread_;
    HANDLE instance_mutex_{};
    bool maximized_{};
    bool expect_failure_{}, failure_observed_{}, shutdown_verified_{true}, crash_test_{}, crash_done_{};
    bool tray_added_{}, exiting_{}, finalizing_{}, selftest_{}, pending_open_{}, preview_started_{},
        preview_done_{}, preview_ok_{};
    int self_stage_{}, ready_count_{}, ping_count_{}, cycles_{2}, recoveries_{}, core_seconds_{};
    std::uint64_t start_tick_{}, stage_tick_{}, final_tick_{};
    std::string diagnostic_, last_tray_state_, test_page_;
    UINT taskbar_message_{};
    NOTIFYICONDATAW nid_{};
    std::ofstream log_;
};
void DesktopApplication::diagnostic(const std::string &text) {
    diagnostic_ = text;
    if (text.find("failed") != std::string::npos)
        failure_observed_ = true;
    if (log_) {
        if (log_.tellp() > 1024 * 1024) {
            log_.close();
            MoveFileExW((data_ / "logs" / "desktop.log").c_str(),
                        (data_ / "logs" / "desktop.previous.log").c_str(), MOVEFILE_REPLACE_EXISTING);
            log_.open(data_ / "logs" / "desktop.log", std::ios::app);
        }
        log_ << utc_now_ms() << " " << text << std::endl;
    }
    if (window_)
        InvalidateRect(window_, nullptr, TRUE);
}
void DesktopApplication::save_window() {
    if (!window_)
        return;
    WINDOWPLACEMENT placement{};
    placement.length = sizeof(placement);
    if (GetWindowPlacement(window_, &placement)) {
        const auto &r = placement.rcNormalPosition;
        std::ofstream out(data_ / "window.json");
        out << Json{{"left", r.left},
                    {"top", r.top},
                    {"width", r.right - r.left},
                    {"height", r.bottom - r.top},
                    {"maximized", maximized_}};
    }
}
void DesktopApplication::tray() {
    nid_ = {};
    nid_.cbSize = sizeof(nid_);
    nid_.hWnd = window_;
    nid_.uID = 1;
    nid_.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    nid_.uCallbackMessage = tray_message;
    nid_.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wcscpy_s(nid_.szTip, L"PulseTrace · 本地聚合统计");
    tray_added_ = Shell_NotifyIconW(NIM_ADD, &nid_) != FALSE;
}
void DesktopApplication::open() {
    if (exiting_)
        return;
    ShowWindow(window_, maximized_ ? SW_MAXIMIZE : SW_RESTORE);
    SetForegroundWindow(window_);
    if (web_->closing()) {
        pending_open_ = true;
        return;
    }
    pending_open_ = false;
    web_->open();
    if (web_->closing())
        pending_open_ = true;
}
void DesktopApplication::hide() {
    if (!tray_added_) {
        diagnostic("Tray unavailable; keeping window visible");
        return;
    }
    save_window();
    web_->close();
    ShowWindow(window_, SW_HIDE);
    diagnostic("Window hidden; WebView2 controller closed; core retained");
}
void DesktopApplication::menu() {
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, 1, L"打开 PulseTrace");
    AppendMenuW(m, MF_STRING, 2, L"开始监测");
    AppendMenuW(m, MF_STRING, 3, L"暂停监测");
    AppendMenuW(m, MF_STRING, 4, L"今日统计");
    AppendMenuW(m, MF_STRING, 5, L"设置");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, 6, L"完全退出");
    POINT point{};
    GetCursorPos(&point);
    SetForegroundWindow(window_);
    auto id = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, point.x, point.y, 0, window_, nullptr);
    DestroyMenu(m);
    if (id == 1 || id == 4 || id == 5) {
        open();
        if (web_->ready())
            web_->post({{"type", "navigate"}, {"page", id == 5 ? "settings" : "dashboard"}});
    } else if (id == 2 || id == 3)
        command({{"type", "set_monitoring"}, {"request_id", 0u}, {"params", {{"enabled", id == 2}}}});
    else if (id == 6)
        quit();
}
void DesktopApplication::command(Json request) {
    if (exiting_)
        return;
    validate_request(request);
    if (request["type"] == "exit") {
        quit();
        return;
    }
    if (request["type"] == "hide") {
        hide();
        return;
    }
    if (request["type"] == "set_autostart") {
        const bool enabled = request.at("params").at("enabled").get<bool>();
        HKEY key{};
        const auto result =
            RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0,
                            nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr);
        if (result != ERROR_SUCCESS)
            throw std::runtime_error("Cannot open user startup settings");
        wchar_t path[32768]{};
        GetModuleFileNameW(nullptr, path, 32768);
        const auto value = quote_windows_argument(path) + L" --background --data-dir " +
                           quote_windows_argument(data_.wstring());
        auto error = enabled ? RegSetValueExW(key, L"PulseTrace", 0, REG_SZ,
                                              reinterpret_cast<const BYTE *>(value.c_str()),
                                              static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)))
                             : RegDeleteValueW(key, L"PulseTrace");
        RegCloseKey(key);
        if (error != ERROR_SUCCESS && error != ERROR_FILE_NOT_FOUND)
            throw std::runtime_error("Cannot update startup setting");
        web_->post({{"type", "response"},
                    {"request_id", request["request_id"]},
                    {"ok", true},
                    {"data", {{"autostart", enabled}}}});
        return;
    }
    if (request["type"] == "ping") {
        ++ping_count_;
        diagnostic("Native JSON bridge ping received");
    }
    const auto generation = web_->generation();
    const auto hwnd = window_;
    const bool status_request = request["type"] == "get_status";
    const bool test = selftest_;
    const auto directory = narrow(data_.wstring()), test_page = test_page_;
    try {
        core_->request(request, [hwnd, generation, status_request, test, directory, test_page](Json json) {
            if (status_request && json.value("ok", false)) {
                json["data"]["self_test"] = test;
                json["data"]["self_test_page"] = test_page;
                json["data"]["data_directory"] = directory;
                HKEY key{};
                bool enabled = false;
                if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0,
                                  KEY_QUERY_VALUE, &key) == ERROR_SUCCESS) {
                    enabled = RegQueryValueExW(key, L"PulseTrace", nullptr, nullptr, nullptr, nullptr) ==
                              ERROR_SUCCESS;
                    RegCloseKey(key);
                }
                json["data"]["autostart"] = enabled;
            }
            auto *response = new Response{generation, std::move(json)};
            if (!PostMessageW(hwnd, response_message, 0, reinterpret_cast<LPARAM>(response)))
                delete response;
        });
    } catch (const std::exception &e) {
        web_->post({{"type", "response"},
                    {"request_id", request["request_id"]},
                    {"ok", false},
                    {"error", {{"code", "CORE_UNAVAILABLE"}, {"message", e.what()}}}});
    }
}
void DesktopApplication::quit() {
    if (exiting_)
        return;
    exiting_ = true;
    save_window();
    diagnostic("Stopping capture and flushing SQLite");
    if (exit_thread_.joinable())
        exit_thread_.join();
    exit_thread_ = std::thread([this] {
        core_->stop();
        PostMessageW(window_, WM_APP + 52, 0, 0);
    });
}
LRESULT DesktopApplication::message(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (taskbar_message_ && m == taskbar_message_) {
        tray();
        return 0;
    }
    switch (m) {
    case WM_SIZE:
        if (window_ && w != SIZE_MINIMIZED)
            maximized_ = w == SIZE_MAXIMIZED;
        if (web_) {
            web_->set_visible(w != SIZE_MINIMIZED);
            web_->resize();
        }
        return 0;
    case WM_GETMINMAXINFO: {
        auto *info = reinterpret_cast<MINMAXINFO *>(l);
        UINT dpi = GetDpiForWindow(h);
        info->ptMinTrackSize = {MulDiv(880, dpi, 96), MulDiv(620, dpi, 96)};
        return 0;
    }
    case WM_DPICHANGED: {
        auto *r = reinterpret_cast<RECT *>(l);
        SetWindowPos(h, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        if (web_)
            web_->resize();
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT p{};
        auto dc = BeginPaint(h, &p);
        RECT r{};
        GetClientRect(h, &r);
        FillRect(dc, &r, GetSysColorBrush(COLOR_WINDOW));
        r.left += 24;
        r.top += 24;
        const auto text =
            widen("PulseTrace\n" + diagnostic_ + "\nWebView2 初始化中；若失败可通过托盘重新打开。");
        DrawTextW(dc, text.c_str(), -1, &r, DT_LEFT | DT_WORDBREAK);
        EndPaint(h, &p);
        return 0;
    }
    case tray_message:
        if (l == WM_LBUTTONDBLCLK || l == WM_LBUTTONUP)
            open();
        else if (l == WM_RBUTTONUP)
            menu();
        return 0;
    case response_message: {
        std::unique_ptr<Response> response(reinterpret_cast<Response *>(l));
        if (web_ && response->generation == web_->generation())
            web_->post(response->json);
        return 0;
    }
    case WM_APP + 30:
        web_->close();
        if (IsWindowVisible(h) && recoveries_++ < 3)
            pending_open_ = true;
        else
            diagnostic("Repeated WebView2 failures; automatic retries stopped. Use tray to retry manually.");
        return 0;
    case WM_APP + 51:
        open();
        return 0;
    case WM_APP + 52: {
        if (exit_thread_.joinable())
            exit_thread_.join();
        const auto status = core_->status();
        if (!status.value("storage_error", "").empty() && !selftest_) {
            const auto result = MessageBoxW(window_,
                                            L"数据库保存失败。选择“取消”保留未写入的聚合数据，排除磁盘问题后"
                                            L"再次退出以重试；或“确定”退出并丢弃未写入数据。",
                                            L"PulseTrace 保存失败", MB_OKCANCEL | MB_ICONERROR);
            if (result != IDOK) {
                exiting_ = false;
                return 0;
            }
        }
        web_->close();
        finalizing_ = true;
        final_tick_ = GetTickCount64();
        diagnostic("Core stopped and SQLite closed; awaiting WebView2 process-group exit");
        return 0;
    }
    case WM_TIMER: {
        if (web_)
            web_->release_closed_environment();
        if (finalizing_) {
            if (!web_->closing()) {
                if (tray_added_)
                    Shell_NotifyIconW(NIM_DELETE, &nid_);
                tray_added_ = false;
                diagnostic("WebView2 released; core stopped; SQLite closed; tray removed");
                DestroyWindow(window_);
            } else if (GetTickCount64() - final_tick_ > 30000) {
                shutdown_verified_ = false;
                diagnostic("WebView2 shutdown exceeded 30s; process release not verified");
                if (tray_added_)
                    Shell_NotifyIconW(NIM_DELETE, &nid_);
                tray_added_ = false;
                DestroyWindow(window_);
            }
            return 0;
        }
        if (exiting_)
            return 0;
        if (core_seconds_ > 0 &&
            GetTickCount64() - start_tick_ >= static_cast<std::uint64_t>(core_seconds_) * 1000) {
            diagnostic("Core-only timed run completed");
            quit();
            return 0;
        }
        if (tray_added_) {
            const auto state = core_->status();
            const auto label = state.value("session_locked", false) ? "PulseTrace · 锁屏输入已暂停"
                               : state.value("monitoring", false)   ? "PulseTrace · 正在监测（仅聚合）"
                                                                    : "PulseTrace · 监测已暂停";
            if (last_tray_state_ != label) {
                last_tray_state_ = label;
                const auto text = widen(label);
                wcsncpy_s(nid_.szTip, text.c_str(), _TRUNCATE);
                nid_.uFlags = NIF_TIP;
                Shell_NotifyIconW(NIM_MODIFY, &nid_);
            }
        }
        if (pending_open_ && !web_->closing())
            open();
        if (selftest_) {
            const auto now = GetTickCount64();
            if (expect_failure_ && failure_observed_) {
                diagnostic("EXPECTED UI FAILURE safely handled; stopping core");
                quit();
                return 0;
            }
            if (web_->ready() && self_stage_ == 0 && ping_count_ >= (ready_count_ + 1) * 2) {
                if (crash_test_ && !crash_done_) {
                    crash_done_ = true;
                    ping_count_ = 0;
                    diagnostic(web_->crash_browser_for_test()
                                   ? "Selftest: terminated only this test WebView2 browser PID"
                                   : "Selftest browser fault injection failed");
                    return 0;
                }
                if (!preview_started_) {
                    preview_started_ = true;
                    web_->capture_preview(data_ / "logs" / ("selftest-" + test_page_ + ".png"),
                                          [this](bool ok) {
                                              preview_ok_ = ok;
                                              preview_done_ = true;
                                              diagnostic(ok ? "Selftest WebView2 PNG capture succeeded"
                                                            : "Selftest WebView2 PNG capture failed");
                                          });
                    return 0;
                }
                if (!preview_done_)
                    return 0;
                ++ready_count_;
                if (ready_count_ >= cycles_) {
                    diagnostic(
                        "SELFTEST PASS: pages, JSON bridge, repeated close/browser exit/reopen cycles=" +
                        std::to_string(ready_count_));
                    quit();
                    return 0;
                }
                hide();
                self_stage_ = 1;
                stage_tick_ = now;
            } else if (self_stage_ == 1 && now - stage_tick_ > 1500 && !web_->closing()) {
                diagnostic("Selftest: core heartbeat while UI closed=" +
                           core_->status().value("heartbeat_ms", Json(0)).dump());
                open();
                self_stage_ = 0;
            }
            if (now - start_tick_ > static_cast<std::uint64_t>(cycles_ * 20000 + 30000)) {
                diagnostic("SELFTEST FAIL: timeout");
                quit();
            }
        }
        return 0;
    }
    case WM_CLOSE:
        if (core_ && core_->status()["config"].value("close_to_tray", true))
            hide();
        else
            quit();
        return 0;
    case WM_EXITSIZEMOVE: {
        save_window();
        return 0;
    }
    case WM_DESTROY:
        KillTimer(h, 1);
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(h, m, w, l);
    }
}
int DesktopApplication::run(HINSTANCE instance, const DesktopOptions &options) {
    instance_ = instance;
    selftest_ = options.selftest;
    cycles_ = options.cycles;
    core_seconds_ = options.core_seconds;
    test_page_ = options.page;
    expect_failure_ = options.expect_failure;
    crash_test_ = options.crash_test;
    start_tick_ = GetTickCount64();
    wchar_t executable[32768]{};
    if (!GetModuleFileNameW(nullptr, executable, 32768))
        throw std::runtime_error("Cannot resolve executable path");
    exe_ = std::filesystem::path(executable).parent_path();
    data_ = options.data.empty() ? exe_ / "data" : std::filesystem::absolute(options.data);
    try {
        std::filesystem::create_directories(data_ / "logs");
        std::ofstream probe(data_ / "logs" / "write-test.tmp");
        probe << "writable";
        probe.close();
        if (!probe)
            throw std::runtime_error("Data directory is not writable");
    } catch (const std::exception &) {
        if (selftest_)
            throw;
        const auto answer =
            MessageBoxW(nullptr, L"程序数据目录不可写。是否手动选择另一个目录？不会自动写入其他位置。",
                        L"PulseTrace 数据目录", MB_YESNO | MB_ICONWARNING);
        if (answer != IDYES)
            throw std::runtime_error("No writable data directory selected");
        ComPtr<IFileOpenDialog> dialog;
        if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_IFileOpenDialog,
                                    reinterpret_cast<void **>(dialog.put()))))
            throw std::runtime_error("Cannot open folder picker");
        dialog->SetOptions(FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
        if (FAILED(dialog->Show(nullptr)))
            throw std::runtime_error("Data selection cancelled");
        ComPtr<IShellItem> item;
        if (FAILED(dialog->GetResult(item.put())) || !item)
            throw std::runtime_error("Cannot read selected directory");
        LPWSTR path{};
        if (FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)))
            throw std::runtime_error("Cannot resolve selected directory");
        data_ = path;
        CoTaskMemFree(path);
        std::filesystem::create_directories(data_ / "logs");
    }
    std::uint64_t hash = 1469598103934665603ULL;
    for (auto c : std::filesystem::weakly_canonical(data_).wstring()) {
        hash ^= static_cast<std::uint64_t>(towlower(c));
        hash *= 1099511628211ULL;
    }
    const auto name = L"Local\\PulseTrace-" + std::to_wstring(hash);
    instance_mutex_ = CreateMutexW(nullptr, FALSE, name.c_str());
    if (!instance_mutex_)
        throw std::runtime_error("Cannot create single-instance lock");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (auto existing = FindWindowW(L"PulseTrace.Desktop", nullptr))
            PostMessageW(existing, WM_APP + 51, 0, 0);
        return 0;
    }
    const auto logfile = data_ / "logs" / "desktop.log";
    if (std::filesystem::exists(logfile) && std::filesystem::file_size(logfile) > 1024 * 1024)
        MoveFileExW(logfile.c_str(), (data_ / "logs" / "desktop.previous.log").c_str(),
                    MOVEFILE_REPLACE_EXISTING);
    log_.open(logfile, std::ios::app);
    if (!log_)
        throw std::runtime_error("Application data directory is not writable");
    core_ = std::make_unique<CoreService>(data_);
    core_->start();
    WNDCLASSW wc{};
    wc.hInstance = instance;
    wc.lpfnWndProc = proc;
    wc.lpszClassName = L"PulseTrace.Desktop";
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    if (!RegisterClassW(&wc))
        throw std::runtime_error("RegisterClass failed");
    int x = CW_USEDEFAULT, y = CW_USEDEFAULT, width = 1180, height = 820;
    try {
        std::ifstream in(data_ / "window.json");
        if (in) {
            Json p;
            in >> p;
            maximized_ = p.value("maximized", false);
            RECT work{};
            SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
            width = std::clamp(p.value("width", 1180), 880,
                               static_cast<int>(std::max<LONG>(880, work.right - work.left)));
            height = std::clamp(p.value("height", 820), 620,
                                static_cast<int>(std::max<LONG>(620, work.bottom - work.top)));
            x = std::clamp(p.value("left", work.left), work.left, std::max(work.left, work.right - width));
            y = std::clamp(p.value("top", work.top), work.top, std::max(work.top, work.bottom - height));
        }
    } catch (...) {
        diagnostic("Invalid saved window placement; using defaults");
    }
    window_ = CreateWindowExW(0, wc.lpszClassName, L"PulseTrace", WS_OVERLAPPEDWINDOW, x, y, width, height,
                              nullptr, nullptr, instance, this);
    if (!window_)
        throw std::runtime_error("CreateWindow failed");
    web_ = std::make_unique<WebViewHost>(
        window_, data_, exe_ / "web",
        [this](Json request) {
            try {
                command(request);
            } catch (const std::exception &e) {
                web_->post({{"type", "response"},
                            {"request_id", request.value("request_id", Json(nullptr))},
                            {"ok", false},
                            {"error", {{"code", "COMMAND_FAILED"}, {"message", e.what()}}}});
            }
        },
        [this](std::string message) { diagnostic(message); });
    taskbar_message_ = RegisterWindowMessageW(L"TaskbarCreated");
    tray();
    SetTimer(window_, 1, selftest_ ? 250 : 1000, nullptr);
    if (!options.background || !tray_added_)
        open();
    if (options.early_close) {
        hide();
        pending_open_ = true;
    }
    MSG msg{};
    int result{};
    while ((result = GetMessageW(&msg, nullptr, 0, 0)) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    // Worker has stopped before HWND destruction; drain already posted replies.
    while (PeekMessageW(&msg, nullptr, response_message, response_message, PM_REMOVE))
        delete reinterpret_cast<Response *>(msg.lParam);
    if (exit_thread_.joinable())
        exit_thread_.join();
    if (core_)
        core_->stop();
    web_.reset();
    core_.reset();
    UnregisterClassW(wc.lpszClassName, instance);
    if (!shutdown_verified_)
        return 3;
    if (expect_failure_)
        return failure_observed_ ? 0 : 2;
    return selftest_ ? (ready_count_ == cycles_ && ping_count_ >= cycles_ * 2 && preview_ok_ ? 0 : 2)
                     : (result == -1 ? 1 : 0);
}
} // namespace
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR command, int) {
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(com))
        return 1;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    (void)command;
    int argc{};
    auto argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    DesktopOptions options;
    for (int i = 1; i < argc; ++i) {
        std::wstring arg = argv[i];
        if (arg == L"--self-test")
            options.selftest = true;
        else if (arg == L"--background")
            options.background = true;
        else if (arg == L"--data-dir" && i + 1 < argc)
            options.data = argv[++i];
        else if (arg == L"--cycles" && i + 1 < argc)
            options.cycles = std::clamp(_wtoi(argv[++i]), 2, 100);
        else if (arg == L"--core-seconds" && i + 1 < argc) {
            options.core_seconds = std::clamp(_wtoi(argv[++i]), 1, 86400);
            options.background = true;
        } else if (arg == L"--self-test-page" && i + 1 < argc)
            options.page = narrow(argv[++i]);
        else if (arg == L"--expect-ui-failure")
            options.expect_failure = true;
        else if (arg == L"--self-test-crash")
            options.crash_test = true;
        else if (arg == L"--self-test-early-close")
            options.early_close = true;
    }
    LocalFree(argv);
    if (options.page != "dashboard" && options.page != "keyboard" && options.page != "mouse" &&
        options.page != "network" && options.page != "history" && options.page != "settings") {
        CoUninitialize();
        return 2;
    }
    int result = 1;
    try {
        DesktopApplication app;
        result = app.run(instance, options);
    } catch (const std::exception &e) {
        if (!options.selftest)
            MessageBoxW(nullptr, widen(e.what()).c_str(), L"PulseTrace 启动失败", MB_OK | MB_ICONERROR);
    }
    CoUninitialize();
    return result;
}
