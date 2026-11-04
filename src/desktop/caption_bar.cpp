#include "pulsetrace/desktop/caption_bar.hpp"
#include <dwmapi.h>
#include <gdiplus.h>
#include <windowsx.h>
#include <algorithm>
#include <vector>
namespace pulsetrace {
int CaptionBar::height() const { return 0; }
void CaptionBar::create(HWND window,HINSTANCE instance){
    window_=window;instance_=instance;
    // Keep WS_CAPTION/THICKFRAME/MAXIMIZEBOX. Windows draws and hits the real buttons.
    const MARGINS margin{0,0,1,0};
    DwmExtendFrameIntoClientArea(window_,&margin);
    theme("system");apply_attributes();
}
void CaptionBar::apply_attributes(){
    if(!window_)return;
    BOOL dark=dark_&&!high_contrast_;
    DwmSetWindowAttribute(window_,static_cast<DWMWINDOWATTRIBUTE>(20),&dark,sizeof(dark));
    const DWORD rounded=2; // DWMWCP_ROUND; unsupported versions return E_INVALIDARG.
    DwmSetWindowAttribute(window_,static_cast<DWMWINDOWATTRIBUTE>(33),&rounded,sizeof(rounded));
    const COLORREF bg=background(),fg=high_contrast_?GetSysColor(COLOR_WINDOWTEXT):active_?foreground():dark_?RGB(151,177,199):RGB(65,91,112);
    DwmSetWindowAttribute(window_,static_cast<DWMWINDOWATTRIBUTE>(35),&bg,sizeof(bg));
    DwmSetWindowAttribute(window_,static_cast<DWMWINDOWATTRIBUTE>(36),&fg,sizeof(fg));
    const COLORREF border=high_contrast_?GetSysColor(COLOR_WINDOWFRAME):0xFFFFFFFE;
    DwmSetWindowAttribute(window_,static_cast<DWMWINDOWATTRIBUTE>(34),&border,sizeof(border));
    // This requests a system backdrop; opaque WebView content does not expose Mica.
    const DWORD backdrop=transparent_&&!high_contrast_?2:1;
    const auto hr=DwmSetWindowAttribute(window_,static_cast<DWMWINDOWATTRIBUTE>(38),&backdrop,sizeof(backdrop));
    capture_diagnostic_="Native DWM caption; round requested; backdrop HRESULT="+std::to_string(static_cast<unsigned long>(hr));
    changed_=true;InvalidateRect(window_,nullptr,TRUE);
}
void CaptionBar::theme(const std::string &setting){
    if(setting_==setting&&!preferences_dirty_)return;
    preferences_dirty_=false;
    DWORD light=1,transparent=1,size=sizeof(DWORD);
    const wchar_t *key=L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize";
    RegGetValueW(HKEY_CURRENT_USER,key,L"AppsUseLightTheme",RRF_RT_REG_DWORD,nullptr,&light,&size);
    size=sizeof(DWORD);RegGetValueW(HKEY_CURRENT_USER,key,L"EnableTransparency",RRF_RT_REG_DWORD,nullptr,&transparent,&size);
    HIGHCONTRASTW contrast{};contrast.cbSize=sizeof(contrast);
    SystemParametersInfoW(SPI_GETHIGHCONTRAST,sizeof(contrast),&contrast,0);
    BOOL animation=TRUE;SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION,0,&animation,0);
    const bool dark=setting=="dark"||(setting=="system"&&!light),hc=(contrast.dwFlags&HCF_HIGHCONTRASTON)!=0;
    const bool glass=transparent&&!hc,motion=!animation;
    if(setting_==setting&&dark_==dark&&transparent_==glass&&reduced_motion_==motion&&high_contrast_==hc)return;
    setting_=setting;dark_=dark;transparent_=glass;reduced_motion_=motion;high_contrast_=hc;apply_attributes();
}
void CaptionBar::layout(){}
void CaptionBar::paint(HDC){}
POINT CaptionBar::caption_point() const{
    RECT r{};GetWindowRect(window_,&r);const auto dpi=GetDpiForWindow(window_);
    return {r.left+MulDiv(110,dpi,96),r.top+GetSystemMetricsForDpi(SM_CYSIZEFRAME,dpi)+GetSystemMetricsForDpi(SM_CYCAPTION,dpi)/2};
}
POINT CaptionBar::maximize_point() const{
    RECT buttons{},window{};GetWindowRect(window_,&window);
    if(SUCCEEDED(DwmGetWindowAttribute(window_,static_cast<DWMWINDOWATTRIBUTE>(5),&buttons,sizeof(buttons)))&&buttons.right>buttons.left){
        // Probe the actual native strip: DWM may include reserved spacing in this rectangle.
        const int y=window.top+(buttons.top+buttons.bottom)/2;
        int left=-1,right=-1;
        for(int x=window.left+buttons.left;x<window.left+buttons.right;++x)
            if(SendMessageW(window_,WM_NCHITTEST,0,MAKELPARAM(x,y))==HTMAXBUTTON){if(left<0)left=x;right=x;}
        if(left>=0)return {(left+right)/2,y};
    }
    // TITLEBARINFOEX provides the actual native maximize rectangle, also at non-100% DPI.
    TITLEBARINFOEX info{};info.cbSize=sizeof(info);SendMessageW(window_,WM_GETTITLEBARINFOEX,0,reinterpret_cast<LPARAM>(&info));
    const auto &r=info.rgrect[3];return {(r.left+r.right)/2,(r.top+r.bottom)/2};
}
bool CaptionBar::message(UINT m,WPARAM w,LPARAM l,LRESULT &result){
    if(!window_)return false;
    if(m==WM_ACTIVATE){active_=LOWORD(w)!=WA_INACTIVE;apply_attributes();}
    if(m==WM_SETTINGCHANGE||m==WM_THEMECHANGED){preferences_dirty_=true;theme(setting_);}
    if(m==WM_DPICHANGED)apply_attributes();
    if(m==WM_NCHITTEST){
        LRESULT hit{};
        if(DwmDefWindowProc(window_,m,w,l,&hit)){result=hit;return true;}
        // DefWindowProc performs the native caption/maximize/frame hit test if DWM declines.
    }
    return false;
}
bool CaptionBar::capture_frame(const std::filesystem::path &page,const std::filesystem::path &output){
    Gdiplus::GdiplusStartupInput input;ULONG_PTR token{};
    if(Gdiplus::GdiplusStartup(&token,&input,nullptr)!=Gdiplus::Ok)return false;
    bool ok=false;
    {
        RECT outer{},client{};GetWindowRect(window_,&outer);GetClientRect(window_,&client);
        POINT origin{0,0};ClientToScreen(window_,&origin);
        auto dc=GetWindowDC(window_),memory=CreateCompatibleDC(dc);
        auto bitmap=CreateCompatibleBitmap(dc,outer.right-outer.left,outer.bottom-outer.top);auto old=SelectObject(memory,bitmap);
        const bool printed=PrintWindow(window_,memory,2)!=FALSE;
        SelectObject(memory,old);DeleteDC(memory);ReleaseDC(window_,dc);
        if(printed){
            Gdiplus::Bitmap native(bitmap,nullptr),web(page.c_str());bool drawn=false;
            {Gdiplus::Graphics g(&native);drawn=web.GetLastStatus()==Gdiplus::Ok&&g.DrawImage(&web,static_cast<int>(origin.x-outer.left),static_cast<int>(origin.y-outer.top),static_cast<int>(web.GetWidth()),static_cast<int>(web.GetHeight()))==Gdiplus::Ok;}
            if(drawn){UINT n=0,bytes=0;Gdiplus::GetImageEncodersSize(&n,&bytes);std::vector<BYTE> buffer(bytes);
                auto codecs=reinterpret_cast<Gdiplus::ImageCodecInfo *>(buffer.data());
                if(Gdiplus::GetImageEncoders(n,bytes,codecs)==Gdiplus::Ok)for(UINT i=0;i<n;++i)if(wcscmp(codecs[i].MimeType,L"image/png")==0){ok=native.Save(output.c_str(),&codecs[i].Clsid,nullptr)==Gdiplus::Ok;break;}
            }
        }
        DeleteObject(bitmap);
    }
    Gdiplus::GdiplusShutdown(token);return ok;
}
}
