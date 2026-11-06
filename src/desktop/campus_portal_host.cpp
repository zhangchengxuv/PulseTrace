#include "pulsetrace/desktop/campus_portal_host.hpp"
#include "pulsetrace/campus.hpp"
#include "resource.h"
#include <wtsapi32.h>
#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <stdexcept>
namespace pulsetrace {
namespace {
constexpr wchar_t portal_class[]=L"PulseTrace.CampusPortal";
constexpr UINT scan_timer=1,refresh_timer=2;
std::string lower(const std::string &value){auto result=value;for(auto &c:result)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));return result;}
bool trusted_source(LPCWSTR source,const std::vector<std::string> &origins){
    if(!source)return false;
    const auto value=lower(narrow(source));
    return std::ranges::any_of(origins,[&](const auto&origin){return value==origin||value.starts_with(origin+"/");});
}
}
CampusPortalHost::CampusPortalHost(HINSTANCE instance,std::filesystem::path data,std::filesystem::path assets,
                                   std::function<void(Json)> event,std::function<void(std::string)> diagnostic)
    :instance_(instance),data_(std::move(data)),assets_(std::move(assets)),event_(std::move(event)),diagnostic_(std::move(diagnostic)){}
CampusPortalHost::~CampusPortalHost(){*alive_=false;close();}
bool CampusPortalHost::visible()const{return window_&&IsWindowVisible(window_);}
void CampusPortalHost::open(const std::string &url,unsigned refresh_minutes,const Json &rule,const std::vector<std::string> &allowed_origins,bool background){
    const auto normalized=normalize_campus_portal_url(url),origin=campus_origin(normalized);
    if(origin.empty())throw std::invalid_argument("Configure a campus portal URL first");
    refresh_minutes=std::clamp(refresh_minutes,5u,1440u);
    if(process_failed_){close();process_failed_=false;}
    if(window_&&origin_!=origin)close();
    show_requested_=!background;
    url_=normalized;origin_=origin;rule_=rule;refresh_minutes_=refresh_minutes;allowed_origins_=allowed_origins;
    if(allowed_origins_.empty())allowed_origins_.push_back(origin);
    if(!window_){
        WNDCLASSEXW wc{};wc.cbSize=sizeof(wc);wc.lpfnWndProc=proc;wc.hInstance=instance_;wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);
        wc.lpszClassName=portal_class;wc.hIcon=LoadIconW(instance_,MAKEINTRESOURCEW(IDI_PULSETRACE));wc.hIconSm=wc.hIcon;
        RegisterClassExW(&wc);
        window_=CreateWindowExW(0,portal_class,L"PulseTrace · Campus Portal",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,
            CW_USEDEFAULT,CW_USEDEFAULT,1000,760,nullptr,nullptr,instance_,this);
        if(!window_)throw std::runtime_error("Cannot create campus portal window");
    }else if(!background){
        SetWindowPos(window_,HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_SHOWWINDOW);
    }
    ShowWindow(window_,background?SW_HIDE:SW_SHOWNORMAL);if(!background)SetForegroundWindow(window_);
    if(controller_)controller_->put_IsVisible(!background);
    set_refresh(refresh_minutes_);
    if(view_){view_->Navigate(widen(url_).c_str());}
    else if(!creating_)initialize_webview();
}
void CampusPortalHost::hide(){cancel_picker();show_requested_=false;if(controller_)controller_->put_IsVisible(FALSE);if(window_)ShowWindow(window_,SW_HIDE);}
bool CampusPortalHost::matches_scope(const Json &context)const{
    return context.value("enabled",false)&&context.value("portal_url",std::string())==url_&&context.value("allowed_origins",Json::array())==Json(allowed_origins_);
}
void CampusPortalHost::set_refresh(unsigned minutes){refresh_minutes_=std::clamp(minutes,5u,1440u);if(window_&&!locked_&&!suspended_)SetTimer(window_,refresh_timer,refresh_minutes_*60000,nullptr);}
void CampusPortalHost::close(){
    ++picker_request_;pending_picker_.clear();picker_armed_=false;navigating_=false;
    ++generation_;++lifecycle_;
    if(window_&&IsWindow(window_))DestroyWindow(window_);
    window_=nullptr;ready_=false;creating_=false;scan_round_=0;
    if(controller_)controller_->Close();controller_.reset();view_.reset();environment_.reset();
}
void CampusPortalHost::rescan(){if(!ready_)throw std::runtime_error("Open and finish loading the campus portal first");run_scan();}
void CampusPortalHost::synchronize(){
    if(process_failed_){open(url_,refresh_minutes_,rule_,allowed_origins_,!show_requested_);return;}
    if(!view_){if(window_&&!creating_){initialize_webview();return;}if(creating_)return;throw std::runtime_error("Open the campus portal first");}
    event_({{"type","campus_syncing"}});
    const auto hr=view_->Navigate(widen(url_).c_str());if(FAILED(hr))fail(hr,"synchronize");
}
void CampusPortalHost::pick(const std::string &field){
    if(!window_||process_failed_)open(url_,refresh_minutes_,rule_,allowed_origins_);
    pending_picker_=field;++picker_request_;picker_armed_=false;present();
    if(ready_&&!locked_&&!suspended_)arm_picker();
    else{
        event_({{"type","campus_picker_pending"},{"field",field}});
        if(!creating_&&!locked_&&!suspended_&&!navigating_)synchronize();
    }
}
void CampusPortalHost::present(){
    show_requested_=true;
    ShowWindow(window_,IsIconic(window_)?SW_RESTORE:SW_SHOW);
    SetWindowPos(window_,HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_SHOWWINDOW);
    SetForegroundWindow(window_);
    if(controller_){controller_->put_IsVisible(TRUE);controller_->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);}
}
void CampusPortalHost::arm_picker(){
    if(pending_picker_.empty()||picker_armed_||!ready_||locked_||suspended_)return;
    const auto request=picker_request_,generation=generation_,lifecycle=lifecycle_;
    execute_engine(L";location.origin==="+widen(Json(origin_).dump())+L"?PulseCampusEngine.installPicker(document,"+widen(Json(pending_picker_).dump())+L",m=>chrome.webview.postMessage(m)):{picker:'login_required'}",[this,request,generation,lifecycle](Json result){
        if(request!=picker_request_||generation!=generation_||lifecycle!=lifecycle_||pending_picker_.empty())return;
        const auto state=result.is_object()?result.value("picker",std::string()):std::string();
        if(state=="armed"){picker_armed_=true;send_picker_ready();}
        else if(state=="login_required")event_({{"type","campus_picker_pending"},{"field",pending_picker_},{"auth_required",true}});
        else{event_({{"type","campus_picker_failed"},{"field",pending_picker_}});pending_picker_.clear();}
    });
}
void CampusPortalHost::cancel_picker(){
    if(pending_picker_.empty())return;
    const auto field=pending_picker_;pending_picker_.clear();++picker_request_;picker_armed_=false;
    if(view_)execute_engine(L";PulseCampusEngine.cancelPicker(document)",[](Json){});
    event_({{"type","campus_picker_cancelled"},{"field",field}});
}
void CampusPortalHost::send_picker_ready(){event_({{"type","campus_picker_ready"},{"field",pending_picker_}});}
void CampusPortalHost::fail(HRESULT hr,const char *operation){
    creating_=false;ready_=false;
    if(!pending_picker_.empty()){event_({{"type","campus_picker_failed"},{"field",pending_picker_}});pending_picker_.clear();picker_armed_=false;++picker_request_;}
    const auto text=std::string("Campus portal ")+operation+" failed (HRESULT "+std::to_string(static_cast<unsigned long>(hr))+")";
    diagnostic_(text);event_({{"type","campus_error"},{"message",text}});
}
void CampusPortalHost::initialize_webview(){
    creating_=true;
    const auto dll=assets_/L"WebView2Loader.dll";
    if(!loader_)loader_=LoadLibraryExW(dll.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);
    if(!loader_){fail(HRESULT_FROM_WIN32(GetLastError()),"load");return;}
    const auto create=reinterpret_cast<decltype(&CreateCoreWebView2EnvironmentWithOptions)>(GetProcAddress(loader_,"CreateCoreWebView2EnvironmentWithOptions"));
    if(!create){fail(E_NOINTERFACE,"resolve loader");return;}
    auto alive=alive_;const auto creation_generation=lifecycle_;
    auto *complete=new ComCallback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler,HRESULT,ICoreWebView2Environment *>(
        [this,alive,creation_generation](HRESULT hr,ICoreWebView2Environment *env){
            if(!*alive||!window_||lifecycle_!=creation_generation)return S_OK;
            if(FAILED(hr)||!env){fail(hr,"create environment");return S_OK;}
            environment_.set(env);
            auto *controller_complete=new ComCallback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler,HRESULT,ICoreWebView2Controller *>(
                [this,alive,creation_generation](HRESULT result,ICoreWebView2Controller *controller){
                    if(!*alive||!window_||lifecycle_!=creation_generation){if(controller)controller->Close();return S_OK;}
                    creating_=false;if(FAILED(result)||!controller){fail(result,"create controller");return S_OK;}
                    controller_.set(controller);
                    controller_->put_IsVisible(show_requested_);
                    RECT bounds{};GetClientRect(window_,&bounds);controller_->put_Bounds(bounds);
                    if(FAILED(controller_->get_CoreWebView2(view_.put()))||!view_){fail(E_FAIL,"get browser");return S_OK;}
                    try{
                        ComPtr<ICoreWebView2Settings> settings;view_->get_Settings(settings.put());
                        if(settings){settings->put_AreHostObjectsAllowed(FALSE);settings->put_AreDefaultScriptDialogsEnabled(FALSE);
                            settings->put_AreDevToolsEnabled(FALSE);settings->put_IsStatusBarEnabled(FALSE);
                            ComPtr<ICoreWebView2Settings4> private_settings;
                            if(SUCCEEDED(settings->QueryInterface(__uuidof(ICoreWebView2Settings4),reinterpret_cast<void **>(private_settings.put())))){
                                private_settings->put_IsPasswordAutosaveEnabled(FALSE);private_settings->put_IsGeneralAutofillEnabled(FALSE);
                            }}
                        EventRegistrationToken token{};
                        auto register_handler=[&](auto *cb,auto call){const auto rc=call(cb);cb->Release();if(FAILED(rc))throw std::runtime_error("Campus WebView2 handler registration failed");};
                        register_handler(new ComCallback<ICoreWebView2NavigationStartingEventHandler,ICoreWebView2*,ICoreWebView2NavigationStartingEventArgs*>(
                            [this,alive,creation_generation](auto*,auto*args){if(!*alive||lifecycle_!=creation_generation)return S_OK;LPWSTR uri{};args->get_Uri(&uri);if(!trusted_source(uri,allowed_origins_))args->put_Cancel(TRUE);
                                else{++generation_;ready_=false;navigating_=true;picker_armed_=false;KillTimer(window_,scan_timer);}CoTaskMemFree(uri);return S_OK;}),
                            [&](auto*cb){return view_->add_NavigationStarting(cb,&token);});
                        register_handler(new ComCallback<ICoreWebView2NavigationStartingEventHandler,ICoreWebView2*,ICoreWebView2NavigationStartingEventArgs*>(
                            [this,alive,creation_generation](auto*,auto*args){if(!*alive||lifecycle_!=creation_generation)return S_OK;LPWSTR uri{};args->get_Uri(&uri);if(!trusted_source(uri,allowed_origins_))args->put_Cancel(TRUE);CoTaskMemFree(uri);return S_OK;}),
                            [&](auto*cb){return view_->add_FrameNavigationStarting(cb,&token);});
                        register_handler(new ComCallback<ICoreWebView2NewWindowRequestedEventHandler,ICoreWebView2*,ICoreWebView2NewWindowRequestedEventArgs*>(
                            [this,alive,creation_generation](auto*,auto*args){if(!*alive||lifecycle_!=creation_generation)return S_OK;LPWSTR uri{};args->get_Uri(&uri);args->put_Handled(TRUE);if(trusted_source(uri,allowed_origins_))view_->Navigate(uri);CoTaskMemFree(uri);return S_OK;}),
                            [&](auto*cb){return view_->add_NewWindowRequested(cb,&token);});
                        register_handler(new ComCallback<ICoreWebView2PermissionRequestedEventHandler,ICoreWebView2*,ICoreWebView2PermissionRequestedEventArgs*>(
                            [](auto*,auto*args){args->put_State(COREWEBVIEW2_PERMISSION_STATE_DENY);return S_OK;}),
                            [&](auto*cb){return view_->add_PermissionRequested(cb,&token);});
                        register_handler(new ComCallback<ICoreWebView2WebMessageReceivedEventHandler,ICoreWebView2*,ICoreWebView2WebMessageReceivedEventArgs*>(
                            [this,alive,creation_generation](auto*,auto*args){
                                if(!*alive||lifecycle_!=creation_generation)return S_OK;LPWSTR source{};args->get_Source(&source);const bool trusted=trusted_source(source,allowed_origins_);CoTaskMemFree(source);if(!trusted)return S_OK;
                                LPWSTR raw{};if(FAILED(args->get_WebMessageAsJson(&raw)))return S_OK;
                                try{
                                    const std::wstring value(raw);if(value.size()>65536)throw std::runtime_error("Campus message too large");
                                    auto message=Json::parse(narrow(value));
                                    // Portal pages may only publish a manual candidate. Snapshots are
                                    // generated from the injected scanner and never accepted from page JS.
                                    if(message.value("type","")!="campus_candidate")throw std::runtime_error("Campus page message type rejected");
                                    if(!picker_armed_||message.value("field",std::string())!=pending_picker_){CoTaskMemFree(raw);return S_OK;}
                                    pending_picker_.clear();picker_armed_=false;++picker_request_;
                                    message["portal_origin"]=origin_;event_(std::move(message));
                                }
                                catch(const std::exception &e){event_({{"type","campus_error"},{"message",e.what()}});}CoTaskMemFree(raw);return S_OK;
                            }),[&](auto*cb){return view_->add_WebMessageReceived(cb,&token);});
                        register_handler(new ComCallback<ICoreWebView2NavigationCompletedEventHandler,ICoreWebView2*,ICoreWebView2NavigationCompletedEventArgs*>(
                            [this,alive,creation_generation](auto*,auto*args){if(!*alive||lifecycle_!=creation_generation)return S_OK;BOOL ok{};args->get_IsSuccess(&ok);ready_=ok;navigating_=false;
                                if(ok){diagnostic_("Campus portal loaded");scan_round_=0;SetTimer(window_,scan_timer,800,nullptr);SetTimer(window_,refresh_timer,refresh_minutes_*60000,nullptr);WTSRegisterSessionNotification(window_,NOTIFY_FOR_THIS_SESSION);arm_picker();}
                                else fail(E_FAIL,"navigate");return S_OK;}),
                            [&](auto*cb){return view_->add_NavigationCompleted(cb,&token);});
                        register_handler(new ComCallback<ICoreWebView2ProcessFailedEventHandler,ICoreWebView2*,ICoreWebView2ProcessFailedEventArgs*>(
                            [this,alive,creation_generation](auto*,auto*){if(!*alive||lifecycle_!=creation_generation)return S_OK;++generation_;ready_=false;process_failed_=true;event_({{"type","campus_error"},{"message","Campus portal WebView2 process failed; retrying at the next sync"}});return S_OK;}),
                            [&](auto*cb){return view_->add_ProcessFailed(cb,&token);});
                    }catch(const std::exception &e){fail(E_FAIL,e.what());return S_OK;}
                    if(!locked_&&!suspended_)view_->Navigate(widen(url_).c_str());return S_OK;
                });
            const auto result=env->CreateCoreWebView2Controller(window_,controller_complete);controller_complete->Release();
            if(FAILED(result))fail(result,"start controller");return S_OK;
        });
    const auto result=create(nullptr,(data_/L"campus-webview2").c_str(),nullptr,complete);complete->Release();
    if(FAILED(result))fail(result,"start environment");
}
void CampusPortalHost::execute_engine(const std::wstring &expression,std::function<void(Json)> complete){
    if(!view_){complete(Json::object());return;}
    std::ifstream input(assets_/L"web"/L"desktop"/L"campus-engine.js",std::ios::binary);std::ostringstream source;source<<input.rdbuf();
    if(!input.good()&&!input.eof()){complete({{"error","Cannot read campus engine"}});return;}
    const auto script=widen(source.str())+expression;
    auto alive=alive_;
    auto *callback=new ComCallback<ICoreWebView2ExecuteScriptCompletedHandler,HRESULT,LPCWSTR>(
        [alive,complete](HRESULT hr,LPCWSTR text){if(!*alive)return S_OK;try{complete(SUCCEEDED(hr)?Json::parse(narrow(text?text:L"null")):Json{{"error","Campus script failed"}});}catch(const std::exception &e){complete({{"error",e.what()}});}return S_OK;});
    const auto hr=view_->ExecuteScript(script.c_str(),callback);if(FAILED(hr))callback->Invoke(hr,L"{\"error\":\"Campus script failed\"}");callback->Release();
}
void CampusPortalHost::run_scan(){
    if(!ready_||!view_||locked_||suspended_)return;
    const auto generation=generation_;
    execute_engine(L";PulseCampusEngine.scan(document,"+widen((rule_.is_null()?Json(nullptr):rule_).dump())+L")",[this,generation](Json result){
        if(!ready_||locked_||suspended_||generation!=generation_)return;
        if(!result.is_object()){event_({{"type","campus_error"},{"message","Campus page is not ready for recognition"}});return;}
        if(result.contains("error")){event_({{"type","campus_error"},{"message",result["error"]}});return;}
        if(campus_origin(result.value("portal_url",std::string()))!=origin_){event_({{"type","campus_rule_stale"}});return;}
        if(result.value("auth_required",false)){event_({{"type","campus_auth_required"}});return;}
        arm_picker();
        event_({{"type","campus_candidates"},{"portal_origin",origin_},{"candidates",result.value("candidates",Json::object())},
                {"ambiguous",result.value("candidates",Json::object())}});
        if(result.value("rule_valid",false)&&result.contains("snapshot")&&!result["snapshot"].is_null()){
            auto snapshot=result["snapshot"];snapshot["portal_url"]=url_;event_({{"type","campus_snapshot"},{"snapshot",snapshot}});
        }else if(!rule_.is_null()&&result.contains("missing")&&!result["missing"].empty()){
            event_({{"type","campus_rule_stale"},{"missing",result["missing"]}});
        }
    });
}
LRESULT CALLBACK CampusPortalHost::proc(HWND h,UINT m,WPARAM w,LPARAM l){
    auto *self=reinterpret_cast<CampusPortalHost*>(GetWindowLongPtrW(h,GWLP_USERDATA));
    if(m==WM_NCCREATE){self=static_cast<CampusPortalHost*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);SetWindowLongPtrW(h,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
    if(self)try{return self->message(h,m,w,l);}catch(const std::exception &e){self->fail(E_FAIL,e.what());return 0;}return DefWindowProcW(h,m,w,l);
}
LRESULT CampusPortalHost::message(HWND h,UINT m,WPARAM w,LPARAM l){
    switch(m){
    case WM_SIZE:if(controller_){RECT r{};GetClientRect(h,&r);controller_->put_Bounds(r);}return 0;
    case WM_TIMER:
        if(w==scan_timer){if(ready_)run_scan();if(++scan_round_>=3)KillTimer(h,scan_timer);else SetTimer(h,scan_timer,2200,nullptr);return 0;}
        // A DOM rescan alone repeats cached server-rendered values indefinitely.
        // Navigate to the configured, query-free GET URL; never replay a login POST.
        if(w==refresh_timer){if(!locked_&&!suspended_&&pending_picker_.empty())synchronize();return 0;}
    case WM_WTSSESSION_CHANGE:
        if(w==WTS_SESSION_LOCK){locked_=true;++generation_;KillTimer(h,scan_timer);KillTimer(h,refresh_timer);ready_=false;diagnostic_("Campus portal paused while locked");}
        else if(w==WTS_SESSION_UNLOCK){locked_=false;if(!suspended_){set_refresh(refresh_minutes_);synchronize();}}
        return 0;
    case WM_POWERBROADCAST:
        if(w==PBT_APMSUSPEND){suspended_=true;++generation_;KillTimer(h,scan_timer);KillTimer(h,refresh_timer);ready_=false;}
        else if(w==PBT_APMRESUMEAUTOMATIC||w==PBT_APMRESUMESUSPEND){suspended_=false;if(!locked_){set_refresh(refresh_minutes_);synchronize();}}
        return TRUE;
    case WM_CLOSE:hide();return 0;
    case WM_DESTROY:++generation_;++lifecycle_;KillTimer(h,scan_timer);KillTimer(h,refresh_timer);WTSUnRegisterSessionNotification(h);if(controller_)controller_->Close();controller_.reset();view_.reset();environment_.reset();window_=nullptr;ready_=false;return 0;
    default:return DefWindowProcW(h,m,w,l);
    }
}
}
