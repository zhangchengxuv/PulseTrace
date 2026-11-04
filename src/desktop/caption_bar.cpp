#include "pulsetrace/desktop/caption_bar.hpp"
#include "resource.h"
#include <commctrl.h>
#include <dwmapi.h>
#include <windowsx.h>
#include <algorithm>
#include <stdexcept>
#include <gdiplus.h>
#include <vector>
namespace pulsetrace {
namespace {
constexpr int first_button=710;
LRESULT CALLBACK button_proc(HWND h,UINT m,WPARAM w,LPARAM l,UINT_PTR,DWORD_PTR){
    if(m==WM_MOUSEMOVE){TRACKMOUSEEVENT t{sizeof(t),TME_LEAVE,h,0};TrackMouseEvent(&t);InvalidateRect(h,nullptr,FALSE);}
    if(m==WM_MOUSELEAVE||m==WM_KILLFOCUS||m==WM_SETFOCUS)InvalidateRect(h,nullptr,FALSE);
    return DefSubclassProc(h,m,w,l);
}
int dip(HWND h,int n){return MulDiv(n,GetDpiForWindow(h),96);}
}
int CaptionBar::height() const{return dip(window_,height_dip);}
void CaptionBar::create(HWND window,HINSTANCE instance){
    window_=window;instance_=instance;
    const wchar_t *names[]={L"最小化",L"最大化 / 还原",L"关闭到托盘 / 关闭窗口"};
    for(int i=0;i<3;++i){buttons_[i]=CreateWindowExW(0,L"BUTTON",names[i],WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,
        0,0,0,0,window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(first_button+i)),instance,nullptr);
        if(!buttons_[i])throw std::runtime_error("Cannot create caption control");
        SetWindowSubclass(buttons_[i],button_proc,1,0);
    }
    const COLORREF no_border=0xFFFFFFFE;
    DwmSetWindowAttribute(window_,static_cast<DWMWINDOWATTRIBUTE>(34),&no_border,sizeof(no_border));
    SetWindowPos(window_,nullptr,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_FRAMECHANGED);
    layout();theme("system");
}
void CaptionBar::theme(const std::string &setting){
    bool dark=setting=="dark";
    if(setting=="system"){DWORD light=1,size=sizeof(light);RegGetValueW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",L"AppsUseLightTheme",RRF_RT_REG_DWORD,nullptr,&light,&size);dark=!light;}
    if(setting_==setting&&dark_==dark)return;setting_=setting;dark_=dark;
    InvalidateRect(window_,nullptr,TRUE);for(auto b:buttons_)if(b)InvalidateRect(b,nullptr,TRUE);
}
void CaptionBar::layout(){RECT r{};GetClientRect(window_,&r);int width=dip(window_,46);
    for(int i=0;i<3;++i)if(buttons_[i])SetWindowPos(buttons_[i],nullptr,r.right-resize_border(window_)-(3-i)*width,0,width,height(),SWP_NOZORDER|SWP_NOACTIVATE);
    if(buttons_[1])SetWindowTextW(buttons_[1],IsZoomed(window_)?L"还原":L"最大化");InvalidateRect(window_,nullptr,FALSE);
}
void CaptionBar::paint(HDC dc){RECT r{};GetClientRect(window_,&r);r.bottom=height();auto brush=CreateSolidBrush(dark_?RGB(10,38,57):RGB(212,233,247));FillRect(dc,&r,brush);DeleteObject(brush);
    const int size=dip(window_,18),x=dip(window_,12),y=(height()-size)/2;
    DrawIconEx(dc,x,y,LoadIconW(instance_,MAKEINTRESOURCEW(IDI_PULSETRACE)),size,size,0,nullptr,DI_NORMAL);
    HFONT font=CreateFontW(-dip(window_,12),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    auto old=SelectObject(dc,font);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,dark_?RGB(239,247,252):RGB(10,38,57));r.left=dip(window_,38);r.right-=dip(window_,150);DrawTextW(dc,L"PulseTrace",-1,&r,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS);SelectObject(dc,old);DeleteObject(font);
}
void CaptionBar::draw_button(const DRAWITEMSTRUCT &item){int index=static_cast<int>(item.CtlID)-first_button;if(index<0||index>2)return;
    POINT cursor{};GetCursorPos(&cursor);RECT screen{};GetWindowRect(item.hwndItem,&screen);bool hover=PtInRect(&screen,cursor);
    auto bg=dark_?RGB(10,38,57):RGB(212,233,247);auto fg=dark_?RGB(239,247,252):RGB(10,38,57);
    if(hover||item.itemState&ODS_SELECTED){bg=index==2?RGB(183,32,62):dark_?RGB(25,70,99):RGB(186,215,235);if(index==2)fg=RGB(255,255,255);}
    auto brush=CreateSolidBrush(bg);FillRect(item.hDC,&item.rcItem,brush);DeleteObject(brush);
    const int cx=(item.rcItem.right+item.rcItem.left)/2,cy=(item.rcItem.bottom+item.rcItem.top)/2,s=dip(window_,5);
    auto pen=CreatePen(PS_SOLID,std::max(1,dip(window_,1)),fg);auto old=SelectObject(item.hDC,pen);auto hollow=SelectObject(item.hDC,GetStockObject(HOLLOW_BRUSH));
    if(index==0){MoveToEx(item.hDC,cx-s,cy,nullptr);LineTo(item.hDC,cx+s,cy);}
    if(index==1){if(IsZoomed(window_)){Rectangle(item.hDC,cx-s+2,cy-s,cx+s,cy+s-2);Rectangle(item.hDC,cx-s,cy-s+2,cx+s-2,cy+s);}else Rectangle(item.hDC,cx-s,cy-s,cx+s,cy+s);}
    if(index==2){MoveToEx(item.hDC,cx-s,cy-s,nullptr);LineTo(item.hDC,cx+s,cy+s);MoveToEx(item.hDC,cx+s,cy-s,nullptr);LineTo(item.hDC,cx-s,cy+s);}
    SelectObject(item.hDC,hollow);SelectObject(item.hDC,old);DeleteObject(pen);
    if(item.itemState&ODS_FOCUS){auto r=item.rcItem;InflateRect(&r,-4,-4);DrawFocusRect(item.hDC,&r);}
}
bool CaptionBar::capture_frame(const std::filesystem::path &page,const std::filesystem::path &output){
    // Diagnostic image of this app only: native client caption + actual WebView preview.
    Gdiplus::GdiplusStartupInput input;ULONG_PTR token{};
    if(Gdiplus::GdiplusStartup(&token,&input,nullptr)!=Gdiplus::Ok)return false;
    bool ok=false;capture_diagnostic_="startup ok";
    {
        RECT r{};GetClientRect(window_,&r);auto dc=GetDC(window_);auto memory=CreateCompatibleDC(dc);auto bitmap=CreateCompatibleBitmap(dc,r.right,r.bottom);auto old=SelectObject(memory,bitmap);
        const bool printed=PrintWindow(window_,memory,PW_CLIENTONLY|2)!=FALSE;
        capture_diagnostic_+="; PrintWindow="+std::to_string(printed);
        SelectObject(memory,old);DeleteDC(memory);ReleaseDC(window_,dc);
        if(printed){Gdiplus::Bitmap native(bitmap,nullptr);Gdiplus::Bitmap web(page.c_str());bool drawn=false;
            const auto native_status=native.GetLastStatus(),web_status=web.GetLastStatus();
            capture_diagnostic_+="; native="+std::to_string(native_status)+"; web="+std::to_string(web_status);
            {Gdiplus::Graphics g(&native);const int edge=resize_border(window_);const auto status=g.DrawImage(&web,edge,height(),r.right-2*edge,r.bottom-height()-edge);drawn=web_status==Gdiplus::Ok&&status==Gdiplus::Ok;capture_diagnostic_+="; draw="+std::to_string(status);}
            if(drawn){
                UINT n=0,bytes=0;Gdiplus::GetImageEncodersSize(&n,&bytes);std::vector<BYTE> buffer(bytes);auto codecs=reinterpret_cast<Gdiplus::ImageCodecInfo *>(buffer.data());
                if(Gdiplus::GetImageEncoders(n,bytes,codecs)==Gdiplus::Ok)for(UINT i=0;i<n;++i)if(wcscmp(codecs[i].MimeType,L"image/png")==0){const auto status=native.Save(output.c_str(),&codecs[i].Clsid,nullptr);ok=status==Gdiplus::Ok;capture_diagnostic_+="; save="+std::to_string(status);break;}
            }
        }
        DeleteObject(bitmap);
    }
    Gdiplus::GdiplusShutdown(token);return ok;
}
bool CaptionBar::message(UINT m,WPARAM w,LPARAM l,LRESULT &result){
    result=0;
    if(m==WM_NCCALCSIZE&&w){auto p=reinterpret_cast<NCCALCSIZE_PARAMS *>(l);
        if(IsZoomed(window_)){const int x=GetSystemMetricsForDpi(SM_CXSIZEFRAME,GetDpiForWindow(window_))+GetSystemMetricsForDpi(SM_CXPADDEDBORDER,GetDpiForWindow(window_));
            const int y=GetSystemMetricsForDpi(SM_CYSIZEFRAME,GetDpiForWindow(window_))+GetSystemMetricsForDpi(SM_CXPADDEDBORDER,GetDpiForWindow(window_));InflateRect(&p->rgrc[0],-x,-y);}return true;}
    if(!window_)return false;
    if(m==WM_NCHITTEST){POINT p{GET_X_LPARAM(l),GET_Y_LPARAM(l)};ScreenToClient(window_,&p);RECT r{};GetClientRect(window_,&r);const int edge=dip(window_,6);
        if(!IsZoomed(window_)){
            const bool left=p.x<edge,right=p.x>=r.right-edge,top=p.y<edge,bottom=p.y>=r.bottom-edge;
            if(top||bottom||left||right){result=top?(left?HTTOPLEFT:right?HTTOPRIGHT:HTTOP):bottom?(left?HTBOTTOMLEFT:right?HTBOTTOMRIGHT:HTBOTTOM):left?HTLEFT:HTRIGHT;return true;}}
        if(p.y<height()&&p.x<r.right-dip(window_,138)){result=HTCAPTION;return true;}
    }
    if(m==WM_COMMAND&&LOWORD(w)>=first_button&&LOWORD(w)<first_button+3){
        const auto id=LOWORD(w)-first_button;SendMessageW(window_,WM_SYSCOMMAND,id==0?SC_MINIMIZE:id==1?(IsZoomed(window_)?SC_RESTORE:SC_MAXIMIZE):SC_CLOSE,0);return true;}
    if(m==WM_DRAWITEM&&w>=first_button&&w<first_button+3){draw_button(*reinterpret_cast<DRAWITEMSTRUCT *>(l));return true;}
    if(m==WM_SIZE||m==WM_DPICHANGED)layout();
    if(m==WM_ERASEBKGND){RECT r{};GetClientRect(window_,&r);auto brush=CreateSolidBrush(dark_?RGB(10,38,57):RGB(212,233,247));FillRect(reinterpret_cast<HDC>(w),&r,brush);DeleteObject(brush);return true;}
    if(m==WM_SETTINGCHANGE)theme(setting_);
    return false;
}
}
