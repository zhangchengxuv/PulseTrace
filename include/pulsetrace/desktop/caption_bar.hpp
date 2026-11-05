#pragma once
#include <windows.h>
#include <string>
#include <filesystem>
namespace pulsetrace {
class CaptionBar {
 public:
    // Native non-client controls own the caption and resize frame.
    static constexpr int height_dip=0;
    static constexpr int resize_border_dip=0;
    static int resize_border(HWND){return 0;}
    void create(HWND window,HINSTANCE instance);
    void theme(const std::string &setting);
    bool message(UINT message,WPARAM w,LPARAM l,LRESULT &result);
    int height() const;
    void layout();
    void paint(HDC dc);
    POINT caption_point() const;
    POINT maximize_point() const;
    bool consume_changed(){bool value=changed_;changed_=false;return value;}
    bool transparency() const{return transparent_;}
    bool reduced_motion() const{return reduced_motion_;}
    bool active() const{return active_;}
    bool dark() const{return dark_;}
    bool capture_frame(const std::filesystem::path &page, const std::filesystem::path &output);
    const std::string &capture_diagnostic() const{return capture_diagnostic_;}
    COLORREF background() const{return high_contrast_?GetSysColor(COLOR_WINDOW):dark_?(active_?RGB(7,19,33):RGB(9,22,37)):RGB(248,247,252);}
    COLORREF foreground() const{return high_contrast_?GetSysColor(COLOR_WINDOWTEXT):dark_?RGB(239,247,252):RGB(48,53,79);}
 private:
    void apply_attributes();
    HWND window_{};
    HINSTANCE instance_{};
    bool dark_{true};
    bool active_{true},transparent_{true},reduced_motion_{},high_contrast_{},changed_{true};
    bool preferences_dirty_{true};
    std::string setting_;
    std::string capture_diagnostic_;
};
}
