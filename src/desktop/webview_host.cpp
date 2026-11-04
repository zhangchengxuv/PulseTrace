#include "pulsetrace/desktop/webview_host.hpp"
#include "pulsetrace/protocol.hpp"
#include "pulsetrace/desktop/caption_bar.hpp"
#include <shlwapi.h>
#include <stdexcept>
namespace pulsetrace {
std::string narrow(const std::wstring &w) {
    if (w.empty())
        return {};
    const int n = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, w.data(), static_cast<int>(w.size()),
                                      nullptr, 0, nullptr, nullptr);
    if (!n)
        throw std::runtime_error("Invalid UTF-16");
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, w.data(), static_cast<int>(w.size()), s.data(), n,
                        nullptr, nullptr);
    return s;
}
std::wstring widen(const std::string &s) {
    if (s.empty())
        return {};
    const int n =
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), nullptr, 0);
    if (!n)
        throw std::runtime_error("Invalid UTF-8");
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}
namespace {
constexpr wchar_t origin[] = L"https://pulsetrace.example/";
bool trusted(LPCWSTR source) {
    if (!source)
        return false;
    const std::wstring s(source);
    return s.starts_with(origin);
}
template <class I, class... Args, class F> void attach(F &&fn, auto &&call) {
    auto *cb = new ComCallback<I, Args...>(std::function<HRESULT(Args...)>(std::forward<F>(fn)));
    const HRESULT result = call(cb);
    cb->Release();
    if (FAILED(result))
        throw std::runtime_error("WebView2 handler registration failed");
}
} // namespace
WebViewHost::WebViewHost(HWND window, std::filesystem::path data, std::filesystem::path assets,
                         std::function<void(Json)> message, std::function<void(std::string)> diagnostic)
    : window_(window), data_(std::move(data)), assets_(std::move(assets)), message_(std::move(message)),
      diagnostic_(std::move(diagnostic)) {}
WebViewHost::~WebViewHost() {
    *alive_ = false;
    close();
    if (environment5_)
        environment5_->remove_BrowserProcessExited(exit_token_);
    environment5_.reset();
    environment_
        .reset(); /* Loader kept loaded until process exit: asynchronous COM callbacks may still unwind. */
}
void WebViewHost::fail(HRESULT hr, const char *op) {
    creating_ = false;
    ready_ = false;
    diagnostic_(std::string(op) + " failed (HRESULT " + std::to_string(static_cast<unsigned long>(hr)) +
                "). Use tray > Open to retry.");
    InvalidateRect(window_, nullptr, TRUE);
    close();
}
void WebViewHost::open() {
    if (ready_ || creating_ || closing_)
        return;
    if(controller_){close();return;}
    if (environment5_) {
        environment5_->remove_BrowserProcessExited(exit_token_);
        environment5_.reset();
    }
    environment_.reset();
    ++generation_;
    const auto g = generation_;
    auto alive = alive_;
    creating_ = true;
    if (!loader_) {
        const auto dll = assets_.parent_path() / L"WebView2Loader.dll";
        loader_ = LoadLibraryExW(dll.c_str(), nullptr,
                                 LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    }
    if (!loader_) {
        fail(HRESULT_FROM_WIN32(GetLastError()), "Load WebView2Loader.dll");
        return;
    }
    const auto available = reinterpret_cast<decltype(&GetAvailableCoreWebView2BrowserVersionString)>(
        GetProcAddress(loader_, "GetAvailableCoreWebView2BrowserVersionString"));
    const auto create = reinterpret_cast<decltype(&CreateCoreWebView2EnvironmentWithOptions)>(
        GetProcAddress(loader_, "CreateCoreWebView2EnvironmentWithOptions"));
    if (!available || !create) {
        fail(E_NOINTERFACE, "Resolve WebView2 Loader exports");
        return;
    }
    LPWSTR version{};
    const auto detected = available(nullptr, &version);
    if (version)
        CoTaskMemFree(version);
    if (FAILED(detected)) {
        fail(detected, "Evergreen WebView2 Runtime missing");
        return;
    }
    auto *complete = new ComCallback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler, HRESULT,
                                     ICoreWebView2Environment *>(
        [this, alive, g](HRESULT hr, ICoreWebView2Environment *env) {
            if (!*alive || g != generation_)
                return S_OK;
            if (FAILED(hr) || !env) {
                fail(hr, "Create environment");
                return S_OK;
            }
            environment_.set(env);
            browser_exited_=false;
            env->QueryInterface(__uuidof(ICoreWebView2Environment5),
                                reinterpret_cast<void **>(environment5_.put()));
            if (environment5_)
                attach<ICoreWebView2BrowserProcessExitedEventHandler, ICoreWebView2Environment *,
                       ICoreWebView2BrowserProcessExitedEventArgs *>(
                    [this, alive](auto *sender, auto *) {
                        if (*alive && sender==environment_.get()) {
                            closing_ = false;
                            browser_exited_=true;
                            browser_pid_ = 0;
                            diagnostic_("WebView2 browser process group exited");
                        }
                        return S_OK;
                    },
                    [this](auto *cb) { return environment5_->add_BrowserProcessExited(cb, &exit_token_); });
            auto *controller_complete =
                new ComCallback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler, HRESULT,
                                ICoreWebView2Controller *>(
                    [this, alive, g](HRESULT result, ICoreWebView2Controller *controller) {
                        if (!*alive || g != generation_) {
                            if (controller)
                                controller->Close();
                            return S_OK;
                        }
                        creating_ = false;
                        if (FAILED(result) || !controller) {
                            fail(result, "Create controller");
                            return S_OK;
                        }
                        controller_.set(controller);
                        set_background(background_);
                        const auto core_result=controller->get_CoreWebView2(view_.put());
                        if(FAILED(core_result)||!view_){fail(core_result,"Get CoreWebView2");return S_OK;}
                        try{configure();resize();}catch(const std::exception& e){diagnostic_(e.what());fail(E_FAIL,"Configure WebView2");}
                        return S_OK;
                    });
            HRESULT result=E_NOINTERFACE;
            ComPtr<ICoreWebView2Environment10> env10;
            ComPtr<ICoreWebView2ControllerOptions> options;
            if(SUCCEEDED(env->QueryInterface(__uuidof(ICoreWebView2Environment10),reinterpret_cast<void **>(env10.put())))&&
               SUCCEEDED(env10->CreateCoreWebView2ControllerOptions(options.put()))){
                ComPtr<ICoreWebView2ControllerOptions3> color;
                if(SUCCEEDED(options->QueryInterface(__uuidof(ICoreWebView2ControllerOptions3),reinterpret_cast<void **>(color.put()))))
                    color->put_DefaultBackgroundColor({255,GetRValue(background_),GetGValue(background_),GetBValue(background_)});
                result=env10->CreateCoreWebView2ControllerWithOptions(window_,options.get(),controller_complete);
            }
            if(FAILED(result))result=env->CreateCoreWebView2Controller(window_,controller_complete);
            controller_complete->Release();
            if (FAILED(result))
                fail(result, "Start controller creation");
            return S_OK;
        });
    const auto result = create(nullptr, (data_ / L"webview2").c_str(), nullptr, complete);
    complete->Release();
    if (FAILED(result))
        fail(result, "Start environment creation");
}
void WebViewHost::configure() {
    auto alive = alive_;
    const auto g = generation_;
    ComPtr<ICoreWebView2_3> v3;
    if (FAILED(view_->QueryInterface(__uuidof(ICoreWebView2_3), reinterpret_cast<void **>(v3.put())))) {
        fail(E_NOINTERFACE, "Virtual hostname mapping");
        return;
    }
    auto hr = v3->SetVirtualHostNameToFolderMapping(L"pulsetrace.example", assets_.c_str(),
                                                    COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_DENY);
    if (FAILED(hr)) {
        fail(hr, "Map local assets");
        return;
    }
    ComPtr<ICoreWebView2Settings> settings;
    view_->get_Settings(settings.put());
    if (settings) {
        settings->put_AreHostObjectsAllowed(FALSE);
        settings->put_AreDefaultScriptDialogsEnabled(FALSE);
        settings->put_AreDevToolsEnabled(FALSE);
        settings->put_IsStatusBarEnabled(FALSE);
    }
    EventRegistrationToken token{};
    attach<ICoreWebView2NavigationStartingEventHandler, ICoreWebView2 *,
           ICoreWebView2NavigationStartingEventArgs *>(
        [](auto *, auto *args) {
            LPWSTR uri{};
            args->get_Uri(&uri);
            const bool ok = trusted(uri);
            CoTaskMemFree(uri);
            if (!ok)
                args->put_Cancel(TRUE);
            return S_OK;
        },
        [&](auto *cb) { return view_->add_NavigationStarting(cb, &token); });
    attach<ICoreWebView2NavigationStartingEventHandler, ICoreWebView2 *,
           ICoreWebView2NavigationStartingEventArgs *>(
        [](auto *, auto *args) {
            args->put_Cancel(TRUE);
            return S_OK;
        },
        [&](auto *cb) { return view_->add_FrameNavigationStarting(cb, &token); });
    attach<ICoreWebView2NewWindowRequestedEventHandler, ICoreWebView2 *,
           ICoreWebView2NewWindowRequestedEventArgs *>(
        [](auto *, auto *args) {
            args->put_Handled(TRUE);
            return S_OK;
        },
        [&](auto *cb) { return view_->add_NewWindowRequested(cb, &token); });
    attach<ICoreWebView2PermissionRequestedEventHandler, ICoreWebView2 *,
           ICoreWebView2PermissionRequestedEventArgs *>(
        [](auto *, auto *args) {
            args->put_State(COREWEBVIEW2_PERMISSION_STATE_DENY);
            return S_OK;
        },
        [&](auto *cb) { return view_->add_PermissionRequested(cb, &token); });
    attach<ICoreWebView2WebMessageReceivedEventHandler, ICoreWebView2 *,
           ICoreWebView2WebMessageReceivedEventArgs *>(
        [this, alive, g](auto *, auto *args) {
            if (!*alive || g != generation_)
                return S_OK;
            LPWSTR source{};
            args->get_Source(&source);
            bool ok = trusted(source);
            CoTaskMemFree(source);
            if (!ok)
                return S_OK;
            LPWSTR raw{};
            if (FAILED(args->get_WebMessageAsJson(&raw)))
                return S_OK;
            try {
                const std::wstring w(raw);
                CoTaskMemFree(raw);
                raw = nullptr;
                if (w.size() > 65536)
                    throw std::invalid_argument("Message too large");
                auto json = parse_message(narrow(w));
                message_(std::move(json));
            } catch (const std::exception &e) {
                if (raw)
                    CoTaskMemFree(raw);
                post({{"type", "response"},
                      {"request_id", nullptr},
                      {"ok", false},
                      {"error", {{"code", "INVALID_MESSAGE"}, {"message", e.what()}}}});
            }
            return S_OK;
        },
        [&](auto *cb) { return view_->add_WebMessageReceived(cb, &token); });
    attach<ICoreWebView2NavigationCompletedEventHandler, ICoreWebView2 *,
           ICoreWebView2NavigationCompletedEventArgs *>(
        [this, alive, g](auto *, auto *args) {
            if (!*alive || g != generation_)
                return S_OK;
            BOOL ok{};
            args->get_IsSuccess(&ok);
            ready_ = ok;
            if (ok) {
                diagnostic_("WebView2 local page loaded");
                view_->get_BrowserProcessId(&browser_pid_);
            } else
                fail(E_FAIL, "Load local page");
            return S_OK;
        },
        [&](auto *cb) { return view_->add_NavigationCompleted(cb, &token); });
    attach<ICoreWebView2ProcessFailedEventHandler, ICoreWebView2 *, ICoreWebView2ProcessFailedEventArgs *>(
        [this, alive, g](auto *, auto *) {
            if (*alive && g == generation_) {
                ready_ = false;
                diagnostic_(
                    "WebView2 process failed. Tray > Open retries; capture core remains independent.");
                PostMessageW(window_, WM_APP + 30, 0, 0);
            }
            return S_OK;
        },
        [&](auto *cb) { return view_->add_ProcessFailed(cb, &token); });
    view_->Navigate(L"https://pulsetrace.example/desktop/index.html");
}
void WebViewHost::close() {
    ++generation_;
    creating_ = false;
    ready_ = false;
    if (controller_) {
        closing_ = environment5_.get() != nullptr && !browser_exited_;
        controller_->Close();
    }
    view_.reset();
    controller_.reset();
}
void WebViewHost::resize() {
    if (controller_) {
        RECT bounds{};
        GetClientRect(window_, &bounds);
        bounds.top=std::min(bounds.bottom,static_cast<LONG>(MulDiv(CaptionBar::height_dip,GetDpiForWindow(window_),96)));
        const int edge=CaptionBar::resize_border(window_);
        bounds.left+=edge;bounds.right=std::max(bounds.left,bounds.right-edge);bounds.bottom=std::max(bounds.top,bounds.bottom-edge);
        controller_->put_Bounds(bounds);
    }
}
void WebViewHost::set_background(COLORREF color){
    background_=color;
    if(controller_){ComPtr<ICoreWebView2Controller2> c;
        if(SUCCEEDED(controller_->QueryInterface(__uuidof(ICoreWebView2Controller2),reinterpret_cast<void **>(c.put()))))
            c->put_DefaultBackgroundColor({255,GetRValue(color),GetGValue(color),GetBValue(color)});
    }
}
void WebViewHost::set_visible(bool visible){
    post({{"type","visibility"},{"visible",visible}});
    if(controller_)controller_->put_IsVisible(visible?TRUE:FALSE);
}
void WebViewHost::verify_hidden_ui(std::function<void(bool)> complete){
    if(!view_){complete(false);return;}
    auto alive=alive_;const auto generation=generation_;
    auto *cb=new ComCallback<ICoreWebView2ExecuteScriptCompletedHandler,HRESULT,LPCWSTR>(
        [this,alive,generation,complete](HRESULT hr,LPCWSTR text){
            if(!*alive||generation!=generation_)return S_OK;
            bool ok=false;
            try{const auto value=Json::parse(narrow(text?text:L"null"));ok=SUCCEEDED(hr)&&value.value("hidden",false)&&value.value("timer",-1)==0;}catch(...){}
            complete(ok);return S_OK;
        });
    const auto hr=view_->ExecuteScript(L"({hidden: !nativeVisible, timer: refreshTimer})",cb);
    if(FAILED(hr))cb->Invoke(hr,L"null");cb->Release();
}
void WebViewHost::post(const Json &json) {
    if (view_) {
        const auto w = widen(json.dump());
        view_->PostWebMessageAsJson(w.c_str());
    }
}
void WebViewHost::release_closed_environment(){
    if(!controller_&&!creating_&&browser_exited_){if(environment5_)environment5_->remove_BrowserProcessExited(exit_token_);environment5_.reset();environment_.reset();}
}
void WebViewHost::capture_preview(const std::filesystem::path &file, std::function<void(bool)> complete) {
    if (!view_) {
        complete(false);
        return;
    }
    ComPtr<IStream> stream;
    if (FAILED(SHCreateStreamOnFileEx(file.c_str(), STGM_CREATE | STGM_WRITE | STGM_SHARE_DENY_WRITE,
                                      FILE_ATTRIBUTE_NORMAL, TRUE, nullptr, stream.put()))) {
        complete(false);
        return;
    }
    auto *cb = new ComCallback<ICoreWebView2CapturePreviewCompletedHandler, HRESULT>(
        [complete = std::move(complete)](HRESULT hr) {
            complete(SUCCEEDED(hr));
            return S_OK;
        });
    const auto hr = view_->CapturePreview(COREWEBVIEW2_CAPTURE_PREVIEW_IMAGE_FORMAT_PNG, stream.get(), cb);
    if (FAILED(hr))
        cb->Invoke(hr);
    cb->Release();
}
bool WebViewHost::crash_browser_for_test(){
    if(!browser_pid_||!ready_)return false;
    HANDLE process=OpenProcess(PROCESS_TERMINATE|SYNCHRONIZE,FALSE,browser_pid_);
    if(!process)return false;
    const bool result=TerminateProcess(process,77)!=FALSE;CloseHandle(process);return result;
}
} // namespace pulsetrace
