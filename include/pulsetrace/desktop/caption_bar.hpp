#pragma once
#include <windows.h>
#include <string>
#include <filesystem>
namespace pulsetrace {
class CaptionBar {
 public:
    static constexpr int height_dip=36;
    static constexpr int resize_border_dip=6;
    static int resize_border(HWND window){return IsZoomed(window)?0:MulDiv(resize_border_dip,GetDpiForWindow(window),96);}
    void create(HWND window,HINSTANCE instance);
    void theme(const std::string &setting);
    bool message(UINT message,WPARAM w,LPARAM l,LRESULT &result);
    int height() const;
    void layout();
    void paint(HDC dc);
    bool capture_frame(const std::filesystem::path &page, const std::filesystem::path &output);
    const std::string &capture_diagnostic() const{return capture_diagnostic_;}
    COLORREF background() const{return dark_?RGB(10,38,57):RGB(212,233,247);}
    COLORREF foreground() const{return dark_?RGB(239,247,252):RGB(10,38,57);}
 private:
    void draw_button(const DRAWITEMSTRUCT &item);
    HWND window_{},buttons_[3]{};
    HINSTANCE instance_{};
    bool dark_{true};
    std::string setting_;
    std::string capture_diagnostic_;
};
}
