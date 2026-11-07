#include <atomic>
#include "pulsetrace/core_service.hpp"
#include "pulsetrace/desktop/webview_host.hpp"
#include "pulsetrace/desktop/campus_portal_host.hpp"
#include "pulsetrace/desktop/taskbar_host.hpp"
#include "pulsetrace/desktop/caption_bar.hpp"
#include "pulsetrace/probe_report.hpp"
#include "pulsetrace/protocol.hpp"
#include "pulsetrace/update.hpp"
#include "pulsetrace/cloud_backup.hpp"
#include <future>
#include "pulsetrace/installation.hpp"
#include "pulsetrace/localization.hpp"
#include "resource.h"
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
    bool selftest{}, background{}, expect_failure{}, crash_test{}, early_close{}, picker_test{}, widget_test{};
    int cycles{2}, core_seconds{}, test_width{}, test_height{};
    std::filesystem::path data;
    std::string page{"dashboard"};
};
class DesktopApplication {
  public:
    int run(HINSTANCE instance, const DesktopOptions &options);
    ~DesktopApplication() {
        if (exit_thread_.joinable())
            exit_thread_.join();
        if(cloud_)cloud_->stop();
        if (core_)
            core_->stop();
        if(portal_)
            portal_->close();
        if (tray_added_)
            Shell_NotifyIconW(NIM_DELETE, &nid_);
        if (instance_mutex_)
            CloseHandle(instance_mutex_);
        if(data_lock_!=INVALID_HANDLE_VALUE)CloseHandle(data_lock_);
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
                app->diagnostic((app->selftest_?"SELFTEST FAIL: ":"")+std::string(e.what()));
                if(app->selftest_)app->quit();
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
    void campus_event(Json event);
    void open_campus(bool background=false);
    void diagnostic(const std::string &text);
    void save_window();
    Json appearance() const{return {{"transparency",caption_.transparency()},{"reduced_motion",caption_.reduced_motion()},{"active",caption_.active()},{"dark",caption_.dark()}};}
    HWND window_{};
    HINSTANCE instance_{};
    std::filesystem::path data_, exe_;
    std::unique_ptr<CoreService> core_;
    std::unique_ptr<WebViewHost> web_;
    std::unique_ptr<CampusPortalHost> portal_;
    std::string campus_runtime_status_;
    std::unique_ptr<TaskbarWidgetHost> widget_;
    std::unique_ptr<UpdateService> updates_;
    std::unique_ptr<CloudBackupService> cloud_;
    CaptionBar caption_;
    std::thread exit_thread_;
    HANDLE instance_mutex_{},data_lock_{INVALID_HANDLE_VALUE};
    std::atomic<bool> ending_session_{false};
    bool maximized_{};
    bool expect_failure_{}, failure_observed_{}, shutdown_verified_{true}, crash_test_{}, crash_done_{};
    bool tray_added_{}, exiting_{}, finalizing_{}, selftest_{}, pending_open_{}, preview_started_{},
        preview_done_{}, preview_ok_{}, frame_captured_{};
    int self_stage_{}, ready_count_{}, ping_count_{}, cycles_{2}, recoveries_{}, core_seconds_{};
    std::uint64_t start_tick_{}, stage_tick_{}, final_tick_{}, last_tray_attempt_{};
    bool tray_recovery_checked_{};
    std::string diagnostic_, last_tray_state_, test_page_,pending_page_;
    UINT taskbar_message_{};
    bool minimize_test_done_{},hidden_check_started_{},hidden_check_done_{},hidden_ui_ok_{},picker_test_{},widget_test_{};
    HWND widget_test_handle_{};
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
    last_tray_attempt_=GetTickCount64();
    nid_ = {};
    nid_.cbSize = sizeof(nid_);
    nid_.hWnd = window_;
    nid_.uID = 1;
    nid_.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    nid_.uCallbackMessage = tray_message;
    nid_.hIcon = LoadIconW(instance_, MAKEINTRESOURCEW(IDI_PULSETRACE));
    wcscpy_s(nid_.szTip, ui_text("tray.paused"));
    // TaskbarCreated may be duplicated, or Explorer may not be ready on the first attempt.
    // An existing icon is success as well; a failed duplicate ADD must not erase that state.
    tray_added_ = Shell_NotifyIconW(NIM_MODIFY, &nid_) != FALSE ||
                  Shell_NotifyIconW(NIM_ADD, &nid_) != FALSE;
    last_tray_state_.clear();
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
    if(exiting_)return;
    if (!tray_added_)
        tray();
    if (!tray_added_) {
        diagnostic("Tray unavailable; keeping window visible");
        return;
    }
    save_window();
    if(portal_)portal_->hide();
    web_->close();
    ShowWindow(window_, SW_HIDE);
    diagnostic("Window hidden; WebView2 controller closed; core retained");
}
void DesktopApplication::campus_event(Json event){
    if(!web_)return;
    if(event.contains("portal_origin")&&event["portal_origin"]!=core_->campus_portal_context()["origin"])return;
    campus_runtime_status_=event.value("type",std::string());
    if(event.value("type","")=="campus_snapshot"){
        const auto generation=web_->generation();
        const auto hwnd=window_;
        try{
            core_->save_campus_snapshot(event["snapshot"],[hwnd,generation](Json response){
                Json payload={{"type","campus_event"},{"ok",response.value("ok",false)},
                              {"data",{{"type",response.value("ok",false)?"snapshot_saved":"snapshot_failed"}}}};
                if(response.value("ok",false))payload["data"]["snapshot"]=response["data"]["snapshot"];
                else payload["data"]["error"]=response["error"]["message"];
                auto *message=new Response{generation,std::move(payload)};
                if(!PostMessageW(hwnd,response_message,0,reinterpret_cast<LPARAM>(message)))delete message;
            });
        }catch(const std::exception &e){
            web_->post({{"type","campus_event"},{"ok",false},{"data",{{"kind","snapshot_failed"},{"error",e.what()}}}});
        }
        return;
    }
    web_->post({{"type","campus_event"},{"ok",true},{"data",std::move(event)}});
}
void DesktopApplication::open_campus(bool background){
    const auto context=core_->campus_portal_context();
    if(!context.value("enabled",false))throw std::runtime_error("Enable campus monitoring and save the portal URL first");
    if(context.value("portal_url",std::string()).empty())throw std::runtime_error("Configure a campus portal URL first");
    if(!portal_)portal_=std::make_unique<CampusPortalHost>(instance_,data_,exe_,
        [this](Json event){campus_event(std::move(event));},[this](std::string text){diagnostic("Campus: "+text);});
    std::vector<std::string> origins;for(const auto &origin:context.value("allowed_origins",Json::array()))origins.push_back(origin.get<std::string>());
    portal_->open(context.value("portal_url",std::string()),context.value("refresh_minutes",5u),context.value("rule",Json(nullptr)),origins,background);
}
void DesktopApplication::menu() {
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, 1, ui_text("tray.open"));
    AppendMenuW(m, MF_STRING, 2, ui_text("tray.start"));
    AppendMenuW(m, MF_STRING, 3, ui_text("tray.pause"));
    AppendMenuW(m, MF_STRING, 4, ui_text("tray.today"));
    AppendMenuW(m, MF_STRING, 5, ui_text("tray.settings"));
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, 6, ui_text("tray.exit"));
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
    const auto kind=request["type"].get<std::string>();
    if(kind.starts_with("cloud_")){
        try{if(!cloud_)throw std::runtime_error("CLOUD_UNAVAILABLE");
            if(kind=="cloud_configure"&&!web_->secure_forms())throw std::runtime_error("CLOUD_WEBVIEW_RUNTIME_REQUIRED");
            if(kind=="cloud_confirm_restore"){
                const auto status=cloud_->status();if(selftest_||status.value("restore_token",std::string())!=request["params"].value("token",std::string()))throw std::runtime_error("CLOUD_RESTORE_CONFIRM_REQUIRED");
                set_ui_language(core_->status()["config"].value("language","system"));
                if(MessageBoxW(window_,ui_text("cloud.confirm_notice"),L"PulseTrace",MB_YESNO|MB_ICONWARNING|MB_DEFBUTTON2)!=IDYES){web_->post({{"type","response"},{"request_id",request["request_id"]},{"ok",true},{"data",{{"cancelled",true}}}});return;}
            }
            auto result=cloud_->command(kind,request["params"]);if(kind=="cloud_status")result["secure_forms"]=web_->secure_forms();web_->post({{"type","response"},{"request_id",request["request_id"]},{"ok",true},{"data",std::move(result)}});}
        catch(const std::exception &e){std::string code=e.what();if(!code.starts_with("CLOUD_")||code.size()>80||!std::all_of(code.begin(),code.end(),[](char c){return c=='_'||(c>='A'&&c<='Z');}))code="CLOUD_REQUEST_FAILED";web_->post({{"type","response"},{"request_id",request["request_id"]},{"ok",false},{"error",{{"code",code},{"message","Cloud operation rejected"}}}});}return;
    }
    if(kind=="get_update_status"||kind=="check_updates"||kind=="download_update"||kind=="cancel_update"||kind=="open_update_folder"||kind=="open_release_page"||kind=="install_update"){
        try{
            if(kind=="check_updates"){const auto cfg=core_->status()["config"];updates_->check(cfg.value("update_channel","stable"),cfg.value("update_source","auto"));}
            else if(kind=="download_update")updates_->download();
            else if(kind=="cancel_update")updates_->cancel();
            else if(kind=="install_update")throw std::runtime_error("Automatic installation disabled: PulseTrace publisher signing identity is not configured. Use the verified download folder for manual installation.");
            else if(kind=="open_update_folder"||kind=="open_release_page"){
                const auto folder=data_/"updates";std::filesystem::create_directories(folder);
                const auto target=kind=="open_update_folder"?folder.wstring():widen(updates_->release_page());
                if(reinterpret_cast<INT_PTR>(ShellExecuteW(window_,L"open",target.c_str(),nullptr,nullptr,SW_SHOWNORMAL))<=32)throw std::runtime_error("Cannot open update location");
            }
            web_->post({{"type","response"},{"request_id",request["request_id"]},{"ok",true},{"data",updates_->status()}});
        }catch(const std::exception &e){web_->post({{"type","response"},{"request_id",request["request_id"]},{"ok",false},{"error",{{"code","UPDATE_ERROR"},{"message",e.what()}}}});}return;
    }
    if(request["type"]=="selftest_failure"){
        if(!selftest_)throw std::runtime_error("Selftest diagnostics disabled");
        diagnostic("SELFTEST FAIL: "+request["params"]["message"].get<std::string>());quit();return;
    }
    if(request["type"]=="selftest_snapshot"){
        if(!selftest_)throw std::runtime_error("Selftest capture is disabled in normal operation");
        const auto phase=request["params"]["phase"].get<std::string>();
        if(request["params"].contains("metrics")){
            std::ofstream out(data_/"logs"/(phase=="navigation"?"navigation-metrics.json":"scroll-metrics.json"));out<<request["params"]["metrics"].dump(2);
            if(!out)throw std::runtime_error("Cannot save selftest metrics");
        }
        const auto id=request["request_id"];const auto generation=web_->generation();
        web_->capture_preview(data_/"logs"/("selftest-toolbar-"+phase+".png"),[this,id,generation](bool ok){
            if(web_&&web_->generation()==generation)web_->post({{"type","response"},{"request_id",id},{"ok",ok},{"data",{{"captured",ok}}},{"error",{{"message","Selftest capture failed"}}}});
        });return;
    }
    if(request["type"]=="open_campus_portal"||request["type"]=="campus_rescan"||request["type"]=="campus_pick_field"){
        try{
            if(request["type"]=="open_campus_portal"){
                open_campus();
            }else if(request["type"]=="campus_rescan"){
                if(!portal_)throw std::runtime_error("Open the campus portal first");
                portal_->update_rule(core_->campus_portal_context().value("rule",Json(nullptr)));
                portal_->rescan();
            }else{
                const auto context=core_->campus_portal_context();
                if(!portal_||!portal_->matches_scope(context))open_campus();
                portal_->pick(request["params"].at("field").get<std::string>());
            }
            web_->post({{"type","response"},{"request_id",request["request_id"]},{"ok",true},{"data",{{"opened",true}}}});
        }catch(const std::exception &e){
            web_->post({{"type","response"},{"request_id",request["request_id"]},{"ok",false},{"error",{{"code","CAMPUS_PORTAL_FAILED"},{"message",e.what()}}}});
        }
        return;
    }
    if (request["type"] == "exit") {
        quit();
        return;
    }
    if (request["type"] == "hide") {
        hide();
        return;
    }
    if (request["type"] == "open_monthly_folder"||request["type"]=="open_data_folder") {
        const auto path=request["type"]=="open_monthly_folder"?data_/"network-months":data_;
        std::filesystem::create_directories(path);
        if(reinterpret_cast<INT_PTR>(ShellExecuteW(window_,L"open",path.c_str(),nullptr,nullptr,SW_SHOWNORMAL))<=32)
            throw std::runtime_error("Cannot open monthly archive folder");
        web_->post({{"type","response"},{"request_id",request["request_id"]},{"ok",true},{"data",{{"opened",true}}}});
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
        if(!pending_page_.empty()){web_->post({{"type","navigate"},{"page",pending_page_}});pending_page_.clear();}
    }
    const auto generation = web_->generation();
    const auto hwnd = window_;
    const bool status_request = request["type"] == "get_status";
    const bool test = selftest_, picker_test=picker_test_&&ready_count_==0;
    const auto directory = narrow(data_.wstring()), test_page = test_page_;
    const bool installed=std::filesystem::exists(exe_/L"installed.ini");
    try {
        core_->request(request, [hwnd, generation, status_request, test, picker_test, directory, test_page,installed](Json json) {
            if (status_request && json.value("ok", false)) {
                json["data"]["self_test"] = test;
                json["data"]["self_test_picker"] = test&&picker_test;
                json["data"]["self_test_page"] = test_page;
                json["data"]["data_directory"] = directory;
                json["data"]["installation_mode"]=installed?"installed":"portable";
                json["data"]["effective_language"]=effective_language(json["data"]["config"].value("language","system"));
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
    if(portal_)portal_->close();
    save_window();
    diagnostic("Stopping capture and flushing SQLite");
    if (exit_thread_.joinable())
        exit_thread_.join();
    exit_thread_ = std::thread([this] {
        if(cloud_)cloud_->stop();
        const bool ready=core_->prepare_stop();
        if(ready||ending_session_)core_->stop();
        PostMessageW(window_, WM_APP + 52, ready||ending_session_?0:1, 0);
    });
}
LRESULT DesktopApplication::message(HWND h, UINT m, WPARAM w, LPARAM l) {
    if(m==maintenance_message()){quit();return 0;}
    if(m==widget_message()){
        if(exiting_)return 0;
        if(w==1||w==6){pending_page_=w==1?"network":"settings";open();if(web_->ready()){web_->post({{"type","navigate"},{"page",pending_page_}});pending_page_.clear();}}
        else if(w==7)quit();
        else if(w>=2&&w<=5)command({{"type","set_config"},{"request_id",0u},{"params",{{"taskbar_mode",w==3?"embedded":w==4?"floating":"tray"}}}});
        return 0;
    }
    LRESULT caption_result{};
    if(caption_.message(m,w,l,caption_result))return caption_result;
    if(web_&&(m==WM_ACTIVATE||m==WM_SETTINGCHANGE)){
        web_->set_background(web_->ready()?caption_.background():RGB(7,19,33));web_->post({{"type","appearance"},{"data",appearance()}});
    }
    if (taskbar_message_ && m == taskbar_message_) {
        if(!exiting_){tray_added_=false;tray();}
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
        info->ptMinTrackSize = {MulDiv(400, dpi, 96), MulDiv(400, dpi, 96)};
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
        const bool starting=!web_||!web_->ready();
        auto brush=CreateSolidBrush(starting?RGB(7,19,33):caption_.background());
        FillRect(dc, &r, brush);DeleteObject(brush);
        caption_.paint(dc);
        SetTextColor(dc,caption_.foreground());SetBkMode(dc,TRANSPARENT);
        r.left += 24;
        r.top += caption_.height()+24;
        if(failure_observed_&&starting){
            SetTextColor(dc,RGB(239,247,252));const auto text=std::wstring(ui_text("native.ui_failed"))+L"\n"+widen(diagnostic_);
            DrawTextW(dc,text.c_str(),-1,&r,DT_LEFT|DT_WORDBREAK);
        }else if(starting){
            GetClientRect(h,&r);SetTextColor(dc,RGB(239,247,252));
            const auto font=CreateFontW(-MulDiv(28,GetDpiForWindow(h),96),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,L"Segoe UI Variable");
            const auto previous=font?SelectObject(dc,font):nullptr;
            DrawTextW(dc,L"PulseTrace",-1,&r,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
            if(font){SelectObject(dc,previous);DeleteObject(font);}
        }
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
        if(response->json.value("type",std::string())=="campus_event"&&response->json.contains("data"))campus_runtime_status_=response->json["data"].value("type",std::string());
        if(response->json.value("ok",false)&&response->json.contains("data")&&response->json["data"].is_object()&&response->json["data"].contains("latest")&&response->json["data"].contains("rule"))response->json["data"]["runtime_status"]=campus_runtime_status_;
        if(response->json.value("ok",false)&&response->json.contains("data")&&response->json["data"].is_object()&&response->json["data"].contains("language")){set_ui_language(response->json["data"].value("language","system"));if(widget_)widget_->update(core_->status());}
        if(response->json.value("ok",false)&&response->json.contains("data")&&response->json["data"].is_object()&&response->json["data"].contains("config")){
            response->json["data"]["appearance"]=appearance();
            response->json["data"]["taskbar_widget"]=widget_?widget_->status():Json{{"mode","tray"},{"reason","Disabled in ordinary selftest"}};
        }
        if(portal_&&response->json.value("ok",false)&&response->json.contains("data")&&response->json["data"].is_object()&&response->json["data"].contains("campus_enabled")){
            const auto context=core_->campus_portal_context();
            if(!portal_->matches_scope(context)){portal_->close();portal_.reset();campus_runtime_status_.clear();}
            else{portal_->set_refresh(context.value("refresh_minutes",5u));portal_->update_rule(context.value("rule",Json(nullptr)));}
        }
        if(!portal_&&response->json.value("ok",false)&&response->json.contains("data")&&response->json["data"].is_object()&&response->json["data"].value("campus_enabled",false)){
            const auto context=core_->campus_portal_context();if(!context.value("rule",Json(nullptr)).is_null())try{open_campus(true);}catch(const std::exception &e){campus_event({{"type","campus_error"},{"message",e.what()}});}
        }
        if(portal_&&response->json.value("ok",false)&&response->json.contains("data")&&response->json["data"].is_object()&&response->json["data"].value("deleted",false))portal_->update_rule(Json(nullptr));
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
        if(w==1){
            if(selftest_){core_->cancel_stop();diagnostic("SELFTEST FAIL: exit persistence preparation failed");core_->stop();}
            else if(!ending_session_){
                const auto result=MessageBoxW(window_,ui_text("native.save_prompt"),ui_text("native.save_failed"),MB_OKCANCEL|MB_ICONERROR);
                if(result!=IDOK){core_->cancel_stop();exiting_=false;diagnostic("Exit cancelled; monitoring core retained for automatic persistence recovery");return 0;}
                exit_thread_=std::thread([this]{core_->stop();PostMessageW(window_,WM_APP+52,0,0);});return 0;
            }
        }
        if(widget_){widget_->stop();if(widget_test_handle_&&IsWindow(widget_test_handle_))throw std::runtime_error("Widget did not exit");diagnostic("Taskbar widget stopped; native helper released");}
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
        if (!tray_added_ && GetTickCount64()-last_tray_attempt_>=5000)
            tray();
        if(widget_)widget_->update(core_->status());
        set_ui_language(core_->status()["config"].value("language","system"));
        caption_.theme(core_->status()["config"].value("theme","system"));
        if(web_&&caption_.consume_changed()){
            web_->set_background(web_->ready()?caption_.background():RGB(7,19,33));web_->post({{"type","appearance"},{"data",appearance()}});
        }
        if (core_seconds_ > 0 &&
            GetTickCount64() - start_tick_ >= static_cast<std::uint64_t>(core_seconds_) * 1000) {
            diagnostic("Core-only timed run completed");
            quit();
            return 0;
        }
        if (tray_added_) {
            const auto state = core_->status();
            const auto label=narrow(ui_text(state.value("session_locked",false)?"tray.locked":state.value("monitoring",false)?"tray.active":"tray.paused"));
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
                if(!minimize_test_done_){
                    minimize_test_done_=true;self_stage_=2;stage_tick_=now;
                    SendMessageW(window_,WM_SYSCOMMAND,SC_MINIMIZE,0);return 0;
                }
                if (crash_test_ && !crash_done_) {
                    crash_done_ = true;
                    ping_count_ = 0;
                    diagnostic(web_->crash_browser_for_test()
                                   ? "Selftest: terminated only this test WebView2 browser PID"
                                   : "Selftest browser fault injection failed");
                    return 0;
                }
                if (!preview_started_) {
                    if(widget_test_){const auto value=widget_->status();widget_test_handle_=reinterpret_cast<HWND>(static_cast<UINT_PTR>(value.value("hwnd",0ULL)));
                        std::ofstream out(data_/"logs"/"widget-validation.json");out<<value.dump(2);
                        if(value.value("mode","")!="embedded"||value.value("visibility",0)!=1||value.value("paint_count",0)<1||GetParent(widget_test_handle_)!=FindWindowW(L"Shell_TrayWnd",nullptr)||(GetWindowLongPtrW(widget_test_handle_,GWL_STYLE)&WS_CHILD)==0||(GetWindowLongPtrW(widget_test_handle_,GWL_EXSTYLE)&WS_EX_NOACTIVATE)==0)throw std::runtime_error("Visible taskbar embedding not verified: "+value.dump());
                        diagnostic("Taskbar widget verified: WS_CHILD, GetParent Shell_TrayWnd, no-activate, independent helper");}
                    if(!IsZoomed(window_)){
                        RECT outer{},client{};GetWindowRect(window_,&outer);GetClientRect(window_,&client);
                        POINT origin{0,0};ClientToScreen(window_,&origin);
                        POINT edge{outer.right-2,origin.y+client.bottom/2};
                        if(SendMessageW(window_,WM_NCHITTEST,0,MAKELPARAM(edge.x,edge.y))!=HTRIGHT)
                            throw std::runtime_error("Native resize edge hit-test failed");
                        diagnostic("System resize frame hit-test passed; native non-client area retained");
                    }
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
                // CapturePreview releases its output stream after the completion callback returns.
                if(preview_ok_&&!frame_captured_){
                    frame_captured_=true;
                    preview_ok_=caption_.capture_frame(data_/"logs"/("selftest-"+test_page_+".png"),data_/"logs"/("selftest-window-"+test_page_+".png"));
                    diagnostic(preview_ok_?"Native caption + WebView preview image saved":"SELFTEST FAIL: native frame image capture failed");
                    diagnostic(caption_.capture_diagnostic());
                }
                ++ready_count_;
                if (ready_count_ >= cycles_) {
                    diagnostic(
                        "SELFTEST PASS: pages, JSON bridge, repeated close/browser exit/reopen cycles=" +
                        std::to_string(ready_count_));
                    quit();
                    return 0;
                }
                if(core_->status()["config"].value("close_to_tray",true)){
                    if(!tray_recovery_checked_){
                        tray_recovery_checked_=true;
                        Shell_NotifyIconW(NIM_DELETE,&nid_);tray_added_=false;
                        tray();tray(); // Actual missing-icon recovery and idempotent registration.
                        if(!tray_added_)throw std::runtime_error("Tray recovery unavailable");
                        diagnostic("Tray registration recovered after own icon removal; duplicate registration preserved it");
                    }
                    SendMessageW(window_,WM_SYSCOMMAND,SC_CLOSE,0);
                    if(IsWindowVisible(window_))throw std::runtime_error("Caption close did not hide to tray");
                    diagnostic("Caption close button: hidden to tray; monitoring core retained");
                }else hide();
                self_stage_ = 1;
                stage_tick_ = now;
            } else if(self_stage_==2){
                if(!hidden_check_started_&&now-stage_tick_>250){hidden_check_started_=true;
                    web_->verify_hidden_ui([this](bool ok){hidden_ui_ok_=ok;hidden_check_done_=true;diagnostic(ok?"Minimized UI: native visibility false and refresh timer stopped":"SELFTEST FAIL: minimized UI refresh did not stop");});}
                if(hidden_check_done_&&now-stage_tick_>1000){
                    if(!hidden_ui_ok_){quit();return 0;}
                    SendMessageW(window_,WM_SYSCOMMAND,SC_RESTORE,0);self_stage_=0;
                }else if(now-stage_tick_>5000){diagnostic("SELFTEST FAIL: hidden UI verification timed out");quit();}
            } else if (self_stage_ == 1 && now - stage_tick_ > 1500 && !web_->closing()) {
                if(widget_test_){const auto widget=widget_->status();widget_test_handle_=reinterpret_cast<HWND>(static_cast<UINT_PTR>(widget.value("hwnd",0ULL)));if(!IsWindow(widget_test_handle_)||!IsWindowVisible(widget_test_handle_)){if(now-stage_tick_<8000)return 0;throw std::runtime_error("Widget did not survive close to tray: "+widget.dump());}}
                diagnostic("Selftest: core heartbeat while UI closed=" +
                           core_->status().value("heartbeat_ms", Json(0)).dump());
                open();
                self_stage_ = 0;
            }
            if (now - start_tick_ > static_cast<std::uint64_t>(cycles_ * 20000 + 30000+(picker_test_?90000:0))) {
                diagnostic("SELFTEST FAIL: timeout");
                quit();
            }
        }
        return 0;
    }
    case WM_QUERYENDSESSION:
        diagnostic((l&ENDSESSION_LOGOFF)?"Windows logoff requested; scheduling checkpoint":"Windows shutdown/restart requested; scheduling checkpoint");
        if(core_)core_->on_flush_requested();return TRUE;
    case WM_ENDSESSION:
        if(w){ending_session_=true;if(core_)core_->flush_now(std::chrono::milliseconds(1500));quit();}
        else if(core_)core_->cancel_stop();return 0;
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
    picker_test_ = options.picker_test;
    widget_test_ = options.widget_test;
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
    data_ = DataDirectoryManager::resolve(exe_,options.data);
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
        if(std::filesystem::exists(exe_/L"installed.ini"))throw std::runtime_error("Installed data directory is not writable. Repair permissions or choose another installation directory. Data is preserved.");
        const auto answer =
            MessageBoxW(nullptr, ui_text("native.data_prompt"),
                        ui_text("native.data_title"), MB_YESNO | MB_ICONWARNING);
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
    // Filesystem lock also covers other logon sessions and executable copies.
    data_lock_=CreateFileW((data_/L"data.lock").c_str(),GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(data_lock_==INVALID_HANDLE_VALUE)throw std::runtime_error("PulseTrace data directory is already in use or cannot be locked; existing data preserved");
    const auto logfile = data_ / "logs" / "desktop.log";
    if (std::filesystem::exists(logfile) && std::filesystem::file_size(logfile) > 1024 * 1024)
        MoveFileExW(logfile.c_str(), (data_ / "logs" / "desktop.previous.log").c_str(),
                    MOVEFILE_REPLACE_EXISTING);
    log_.open(logfile, std::ios::app);
    if (!log_)
        throw std::runtime_error("Application data directory is not writable");
    if(!selftest_)cloud_apply_pending_restore(data_);
    core_ = std::make_unique<CoreService>(data_);
    set_ui_language(core_->status()["config"].value("language","system"));
    core_->start();
    cloud_=std::make_unique<CloudBackupService>(data_,[this]{
        if(!core_->flush_now(std::chrono::seconds(10)))throw std::runtime_error("CLOUD_SNAPSHOT_FLUSH_FAILED");
        auto promise=std::make_shared<std::promise<Json>>();auto future=promise->get_future();
        core_->request({{"type","cloud_snapshot"},{"request_id",0},{"params",Json::object()}},[promise](Json response){promise->set_value(std::move(response));});
        if(future.wait_for(std::chrono::seconds(20))!=std::future_status::ready)throw std::runtime_error("CLOUD_SNAPSHOT_TIMEOUT");auto response=future.get();
        if(!response.value("ok",false))throw std::runtime_error("CLOUD_SNAPSHOT_FAILED");return std::filesystem::path(widen(response["data"]["directory"].get<std::string>()));
    },cloud_https_request,!selftest_);
    updates_ = std::make_unique<UpdateService>(data_);
    if(!selftest_&&core_->status()["config"].value("check_updates_on_start",false)){
        const auto cfg=core_->status()["config"];updates_->check(cfg.value("update_channel","stable"),cfg.value("update_source","auto"));
    }
    WNDCLASSW wc{};
    wc.hInstance = instance;
    wc.style=CS_DBLCLKS;
    wc.lpfnWndProc = proc;
    wc.lpszClassName = L"PulseTrace.Desktop";
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(instance_, MAKEINTRESOURCEW(IDI_PULSETRACE));
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
            width = std::clamp(p.value("width", 1180), 800,
                               static_cast<int>(std::max<LONG>(800, work.right - work.left)));
            height = std::clamp(p.value("height", 820), 600,
                                static_cast<int>(std::max<LONG>(600, work.bottom - work.top)));
            x = std::clamp(p.value("left", work.left), work.left, std::max(work.left, work.right - width));
            y = std::clamp(p.value("top", work.top), work.top, std::max(work.top, work.bottom - height));
        }
    } catch (...) {
        diagnostic("Invalid saved window placement; using defaults");
    }
    if (selftest_ && options.test_width && options.test_height) {
        const auto dpi = GetDpiForSystem();
        RECT client{0, 0, MulDiv(options.test_width, dpi, 96), MulDiv(options.test_height, dpi, 96)};
        AdjustWindowRectExForDpi(&client,WS_OVERLAPPEDWINDOW,FALSE,0,dpi);
        width=client.right-client.left;height=client.bottom-client.top;
        maximized_ = false;
    }
    window_ = CreateWindowExW(0, wc.lpszClassName, L"PulseTrace", WS_OVERLAPPEDWINDOW, x, y, width, height,
                              nullptr, nullptr, instance, this);
    if (!window_)
        throw std::runtime_error("CreateWindow failed");
    caption_.create(window_,instance_);
    caption_.theme(core_->status()["config"].value("theme","system"));
    if(selftest_){
        ShowWindow(window_,SW_SHOWNOACTIVATE);
        POINT p=caption_.caption_point();
        if(SendMessageW(window_,WM_NCHITTEST,0,MAKELPARAM(p.x,p.y))!=HTCAPTION)throw std::runtime_error("Caption drag hit-test failed");
        const auto maximize=caption_.maximize_point();
        diagnostic("Native maximize point="+std::to_string(maximize.x)+","+std::to_string(maximize.y)+" hit="+std::to_string(SendMessageW(window_,WM_NCHITTEST,0,MAKELPARAM(maximize.x,maximize.y))));
        if(SendMessageW(window_,WM_NCHITTEST,0,MAKELPARAM(maximize.x,maximize.y))!=HTMAXBUTTON)
            throw std::runtime_error("Native maximize hit-test did not return HTMAXBUTTON");
        diagnostic("Native HTMAXBUTTON verified; system Snap Layout handling retained");
        SendMessageW(window_,WM_SYSCOMMAND,SC_MAXIMIZE,0);if(!IsZoomed(window_))throw std::runtime_error("Caption maximize failed");
        SendMessageW(window_,WM_SYSCOMMAND,SC_RESTORE,0);if(IsZoomed(window_))throw std::runtime_error("Caption restore failed");
        SendMessageW(window_,WM_NCLBUTTONDBLCLK,HTCAPTION,MAKELPARAM(p.x,p.y));
        if(!IsZoomed(window_))throw std::runtime_error("Native caption double-click failed");
        SendMessageW(window_,WM_SYSCOMMAND,SC_RESTORE,0);
        SendMessageW(window_,WM_SYSCOMMAND,SC_MINIMIZE,0);if(!IsIconic(window_))throw std::runtime_error("Caption minimize failed");
        SendMessageW(window_,WM_SYSCOMMAND,SC_RESTORE,0);
        diagnostic("Caption native minimize/maximize/restore and drag hit-test passed");
    }
    const auto dpi = GetDpiForWindow(window_);
    for (const auto kind : {ICON_SMALL, ICON_BIG}) {
        const int size = GetSystemMetricsForDpi(kind == ICON_SMALL ? SM_CXSMICON : SM_CXICON, dpi);
        const auto icon =
            LoadImageW(instance_, MAKEINTRESOURCEW(IDI_PULSETRACE), IMAGE_ICON, size, size, LR_SHARED);
        if (!icon)
            throw std::runtime_error("PulseTrace application icon resource missing");
        SendMessageW(window_, WM_SETICON, kind, reinterpret_cast<LPARAM>(icon));
    }
    RECT bounds{};
    GetClientRect(window_, &bounds);
    diagnostic("Window client pixels=" + std::to_string(bounds.right) + "x" + std::to_string(bounds.bottom) +
               "; DPI=" + std::to_string(dpi) + "; custom window/tray icon resource=101");
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
    if(!selftest_){const auto context=core_->campus_portal_context();if(context.value("enabled",false)&&!context.value("rule",Json(nullptr)).is_null()){try{open_campus(true);}catch(const std::exception &e){diagnostic(std::string("Campus background restore failed: ")+e.what());}}}
    taskbar_message_ = RegisterWindowMessageW(L"TaskbarCreated");
    web_->set_background(web_->ready()?caption_.background():RGB(7,19,33));
    tray();
    if(!selftest_||widget_test_){widget_=std::make_unique<TaskbarWidgetHost>(window_,exe_);widget_->update(core_->status());}
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
        else if(arg==L"--self-test-picker") {options.selftest=true;options.picker_test=true;}
        else if(arg==L"--self-test-widget") {options.selftest=true;options.widget_test=true;}
        else if (arg == L"--background")
            options.background = true;
        else if (arg == L"--data-dir" && i + 1 < argc)
            options.data = argv[++i];
        else if (arg == L"--cycles" && i + 1 < argc)
            options.cycles = std::clamp(_wtoi(argv[++i]), 2, 100);
        else if (arg == L"--core-seconds" && i + 1 < argc) {
            options.core_seconds = std::clamp(_wtoi(argv[++i]), 1, 86400);
            options.background = true;
        } else if (arg == L"--self-test-size" && i + 1 < argc) {
            const std::wstring size = argv[++i];
            const auto split = size.find(L'x');
            if (split != std::wstring::npos) {
                options.test_width = std::clamp(_wtoi(size.substr(0, split).c_str()), 320, 3840);
                options.test_height = std::clamp(_wtoi(size.substr(split + 1).c_str()), 300, 2160);
            }
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
        options.page != "network" && options.page != "history" && options.page != "settings" && options.page != "usage") {
        CoUninitialize();
        return 2;
    }
    int result = 1;
    try {
        DesktopApplication app;
        result = app.run(instance, options);
    } catch (const std::exception &e) {
        if(options.selftest){
            const auto directory=options.data.empty()?executable_data_directory():options.data;
            std::ofstream out(directory/"logs"/"desktop.log",std::ios::app);out<<"SELFTEST FAIL: "<<e.what()<<'\n';
        }
        if (!options.selftest)
            MessageBoxW(nullptr, (std::wstring(ui_text("native.ui_failed"))+L"\n\n"+widen(e.what())).c_str(), ui_text("native.start_failed"), MB_OK | MB_ICONERROR);
    }
    CoUninitialize();
    return result;
}
