#pragma once
#include "pulsetrace/desktop/webview_host.hpp"
#include <functional>
#include <memory>
namespace pulsetrace {
class CampusPortalHost {
  public:
    CampusPortalHost(HINSTANCE instance,std::filesystem::path data,std::filesystem::path assets,
                     std::function<void(Json)> event,std::function<void(std::string)> diagnostic);
    ~CampusPortalHost();
    void open(const std::string &url,unsigned refresh_minutes,const Json &rule,const std::vector<std::string> &allowed_origins,bool background=false);
    void close();
    void hide();
    bool matches_scope(const Json &context) const;
    void set_refresh(unsigned minutes);
    void rescan();
    void synchronize();
    void update_rule(const Json &rule){++generation_;rule_=rule;}
    void pick(const std::string &field);
    bool visible() const;
  private:
    static LRESULT CALLBACK proc(HWND,UINT,WPARAM,LPARAM);
    LRESULT message(HWND,UINT,WPARAM,LPARAM);
    void initialize_webview();
    void fail(HRESULT,const char *);
    void run_scan();
    void execute_engine(const std::wstring &expression,std::function<void(Json)> complete);
    void send_picker_ready();
    void present();
    void arm_picker();
    void cancel_picker();
    HINSTANCE instance_{};
    std::filesystem::path data_,assets_;
    std::function<void(Json)> event_;
    std::function<void(std::string)> diagnostic_;
    HWND window_{};
    HMODULE loader_{};
    ComPtr<ICoreWebView2Environment> environment_;
    ComPtr<ICoreWebView2Controller> controller_;
    ComPtr<ICoreWebView2> view_;
    std::shared_ptr<bool> alive_=std::make_shared<bool>(true);
    std::string url_,origin_,pending_picker_;
    std::vector<std::string> allowed_origins_;
    Json rule_=nullptr;
    unsigned refresh_minutes_{5},scan_round_{};
    unsigned generation_{},lifecycle_{},picker_request_{};
    bool creating_{},ready_{},show_requested_{},locked_{},suspended_{},process_failed_{},picker_armed_{},navigating_{};
};
}
