#pragma once
#include <rpc.h>
#include <rpcndr.h>
#include <windows.h>
#ifdef __MINGW32__
#undef __uuidof
#undef MIDL_INTERFACE
#define MIDL_INTERFACE(x) struct __declspec(uuid(x))
#endif
#include "pulsetrace/json.hpp"
#include <WebView2.h>
#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
namespace pulsetrace {
template <class T> class ComPtr {
  public:
    ComPtr() = default;
    ~ComPtr() { reset(); }
    ComPtr(const ComPtr &) = delete;
    ComPtr &operator=(const ComPtr &) = delete;
    T *get() const { return p_; }
    T *operator->() const { return p_; }
    explicit operator bool() const { return p_ != nullptr; }
    T **put() {
        reset();
        return &p_;
    }
    void reset() {
        if (p_) {
            p_->Release();
            p_ = nullptr;
        }
    }
    void set(T *p) {
        reset();
        p_ = p;
        if (p_)
            p_->AddRef();
    }

  private:
    T *p_{};
};
template <class I, class... Args> class ComCallback final : public I {
  public:
    explicit ComCallback(std::function<HRESULT(Args...)> fn) : fn_(std::move(fn)) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void **out) override {
        if (!out)
            return E_POINTER;
        *out = nullptr;
        if (id == IID_IUnknown || id == __uuidof(I)) {
            *out = static_cast<I *>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override {
        auto n = --refs_;
        if (!n)
            delete this;
        return n;
    }
    HRESULT STDMETHODCALLTYPE Invoke(Args... args) override {
        try {
            return fn_(args...);
        } catch (...) {
            return E_FAIL;
        }
    }

  private:
    std::atomic<ULONG> refs_{1};
    std::function<HRESULT(Args...)> fn_;
};
template <class I, class... Args> auto callback(std::function<HRESULT(Args...)> fn) {
    return new ComCallback<I, Args...>(std::move(fn));
}
class WebViewHost {
  public:
    WebViewHost(HWND window, std::filesystem::path data, std::filesystem::path assets,
                std::function<void(Json)> message, std::function<void(std::string)> diagnostic);
    ~WebViewHost();
    void open();
    void close();
    void release_closed_environment();
    void resize();
    void set_visible(bool visible);
    void set_background(COLORREF color);
    void verify_hidden_ui(std::function<void(bool)> complete);
    void post(const Json &json);
    void capture_preview(const std::filesystem::path &file, std::function<void(bool)> complete);
    bool crash_browser_for_test();
    bool ready() const { return ready_; }
    bool secure_forms() const { return secure_forms_; }
    bool closing() const { return closing_; }
    unsigned long browser_pid() const { return browser_pid_; }
    std::uint64_t generation() const { return generation_; }

  private:
    void configure();
    void fail(HRESULT hr, const char *operation);
    HWND window_{};
    std::filesystem::path data_, assets_;
    HMODULE loader_{};
    ComPtr<ICoreWebView2Environment> environment_;
    ComPtr<ICoreWebView2Environment5> environment5_;
    ComPtr<ICoreWebView2Controller> controller_;
    ComPtr<ICoreWebView2> view_;
    EventRegistrationToken exit_token_{};
    std::function<void(Json)> message_;
    std::function<void(std::string)> diagnostic_;
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
    std::uint64_t generation_{};
    bool creating_{}, ready_{}, closing_{}, browser_exited_{true}, secure_forms_{};
    UINT32 browser_pid_{};
    COLORREF background_{RGB(7,19,33)};
};
std::string narrow(const std::wstring &value);
std::wstring widen(const std::string &value);
} // namespace pulsetrace
