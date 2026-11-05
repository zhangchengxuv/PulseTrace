// Explicit native desktop acceptance. Touches only the widget of the supplied owner PID.
#include <windows.h>
#include <objbase.h>
#include <objidl.h>
#include <gdiplus.h>
#include <dwmapi.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>
#include "pulsetrace/json.hpp"
#include "pulsetrace/desktop/widget_ipc.hpp"
using namespace pulsetrace;
namespace fs=std::filesystem;
std::vector<std::uint32_t> read_pixels(const RECT &r,const fs::path &png={}) {
    const int width=r.right-r.left,height=r.bottom-r.top;
    if(width<=0||height<=0||width>16384||height>256)throw std::runtime_error("Unexpected taskbar capture bounds");
    auto screen=GetDC(nullptr),dc=CreateCompatibleDC(screen);BITMAPINFO info{};
    info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=width;info.bmiHeader.biHeight=-height;
    info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;void *bytes{};
    auto bitmap=CreateDIBSection(screen,&info,DIB_RGB_COLORS,&bytes,nullptr,0);auto old=SelectObject(dc,bitmap);
    const bool ok=BitBlt(dc,0,0,width,height,screen,r.left,r.top,SRCCOPY|CAPTUREBLT)!=FALSE;GdiFlush();
    std::vector<std::uint32_t> pixels;if(ok)pixels.assign(static_cast<std::uint32_t*>(bytes),static_cast<std::uint32_t*>(bytes)+width*height);
    if(ok&&!png.empty()){CLSID encoder{};CLSIDFromString(L"{557CF406-1A04-11D3-9A73-0000F81EF32E}",&encoder);Gdiplus::Bitmap image(bitmap,nullptr);image.Save(png.c_str(),&encoder,nullptr);}
    SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);ReleaseDC(nullptr,screen);
    if(!ok)throw std::runtime_error("Desktop readback failed");return pixels;
}
unsigned distance(std::uint32_t a,std::uint32_t b){return abs(int(a&255)-int(b&255))+abs(int((a>>8)&255)-int((b>>8)&255))+abs(int((a>>16)&255)-int((b>>16)&255));}
template<class F> void wait_for(F condition,const char *message){const auto end=GetTickCount64()+5000;while(GetTickCount64()<end){if(condition())return;Sleep(80);}throw std::runtime_error(message);}
int wmain(int argc,wchar_t **argv){
    Gdiplus::GdiplusStartupInput input;ULONG_PTR token{};Gdiplus::GdiplusStartup(&token,&input,nullptr);
    Json report={{"success",false}};fs::path output;
    try {
        if(argc<3||argc>4)throw std::runtime_error("OUTPUT OWNER_PID [--interact]");
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        output=fs::absolute(argv[1]);fs::create_directories(output);const DWORD owner=wcstoul(argv[2],nullptr,10);
        auto mapping=OpenFileMappingW(FILE_MAP_ALL_ACCESS,FALSE,widget_mapping_name(owner).c_str());if(!mapping)throw std::runtime_error("Owner mapping missing");
        auto shared=static_cast<WidgetShared*>(MapViewOfFile(mapping,FILE_MAP_ALL_ACCESS,0,0,sizeof(WidgetShared)));
        if(!shared){CloseHandle(mapping);throw std::runtime_error("Cannot map widget state");}
        WidgetSnapshot data;WidgetFeedback feedback;
        const bool read=read_snapshot(shared->data_sequence,shared->data,data)&&read_snapshot(shared->feedback_sequence,shared->feedback,feedback);
        UnmapViewOfFile(shared);CloseHandle(mapping);
        if(!read||!valid_snapshot(data)||data.owner_pid!=owner||feedback.version!=widget_protocol)throw std::runtime_error("Wrong owner or protocol");
        auto window=reinterpret_cast<HWND>(static_cast<UINT_PTR>(feedback.hwnd));auto main=reinterpret_cast<HWND>(static_cast<UINT_PTR>(data.owner_window));
        DWORD actual_owner{};GetWindowThreadProcessId(main,&actual_owner);
        if(actual_owner!=owner||!IsWindow(window)||GetParent(window)!=FindWindowW(L"Shell_TrayWnd",nullptr)||!IsWindowVisible(window))throw std::runtime_error("Real visible taskbar child missing");
        if(feedback.background_alpha!=0||feedback.transparent_pixels==0||feedback.opaque_pixels==0||feedback.partial_pixels==0)throw std::runtime_error("Alpha surface quality counters invalid");
        RECT area{},bar{};GetWindowRect(window,&area);GetWindowRect(GetParent(window),&bar);
        report["parent_verified"]=true;report["dpi"]=GetDpiForWindow(window);report["background_alpha"]=feedback.background_alpha;
        report["transparent_pixels"]=feedback.transparent_pixels;report["opaque_pixels"]=feedback.opaque_pixels;report["partial_pixels"]=feedback.partial_pixels;
        read_pixels(bar,output/L"taskbar-before.png");const auto before=read_pixels(area,output/L"widget-visible.png");
        struct Restore{HWND window;~Restore(){if(IsWindow(window))ShowWindow(window,SW_SHOWNOACTIVATE);}}restore{window};
        ShowWindow(window,SW_HIDE);DwmFlush();Sleep(120);const auto below=read_pixels(area,output/L"widget-underlay.png");
        ShowWindow(window,SW_SHOWNOACTIVATE);RedrawWindow(window,nullptr,nullptr,RDW_INVALIDATE|RDW_UPDATENOW);DwmFlush();Sleep(120);
        const auto after=read_pixels(area,output/L"widget-restored.png");const int w=area.right-area.left,h=area.bottom-area.top;
        unsigned matching=0,samples=0;
        for(int y=2;y<h-2;y+=std::max(1,h/8))for(int x:{2,w-3}){++samples;if(distance(after[y*w+x],below[y*w+x])<=12)++matching;}
        report["background_samples"]=samples;report["background_matches"]=matching;
        if(matching!=samples)throw std::runtime_error("Visible widget background differs from the real taskbar underlay");
        report["background_hit_passes_through"]=WindowFromPoint({area.left+2,area.top+2})!=window;
        if(!report["background_hit_passes_through"].get<bool>())throw std::runtime_error("Transparent padding captures input");
        POINT glyph{};bool found=false;
        for(int y=1;y<h-1&&!found;++y)for(int x=1;x<w-1;++x)if(distance(after[y*w+x],below[y*w+x])>120&&WindowFromPoint({area.left+x,area.top+y})==window){glyph={x,y};found=true;break;}
        report["glyph_hit_interactive"]=found;if(!found)throw std::runtime_error("Glyphs are invisible or whole window is click-through");
        read_pixels(bar,output/L"taskbar-screen.png");
        const RECT context{std::max(bar.left,area.left-40),bar.top,std::min(bar.right,area.right+250),bar.bottom};
        read_pixels(context,output/L"taskbar-context.png");
        if(argc==4&&std::wstring(argv[3])==L"--interact"){
            if(IsWindowVisible(main))throw std::runtime_error("Interaction acceptance requires an isolated background owner");
            PostMessageW(window,WM_LBUTTONUP,0,MAKELPARAM(glyph.x,glyph.y));
            wait_for([&]{return IsWindowVisible(main)!=FALSE;},"Click did not open PulseTrace");report["click_opened_main"]=true;
            PostMessageW(window,WM_RBUTTONUP,0,MAKELPARAM(glyph.x,glyph.y));const auto thread=GetWindowThreadProcessId(window,nullptr);
            wait_for([&]{GUITHREADINFO gui{};gui.cbSize=sizeof(gui);return GetGUIThreadInfo(thread,&gui)&&(gui.flags&GUI_POPUPMENUMODE);},"Right-click menu did not open");
            report["context_menu_opened"]=true;PostMessageW(window,WM_CANCELMODE,0,0);Sleep(160);
            PostMessageW(main,WM_CLOSE,0,0);wait_for([&]{return !IsWindowVisible(main);},"Main did not close to tray");
            report["survived_close_to_tray"]=IsWindowVisible(window)!=FALSE;
            if(!report["survived_close_to_tray"].get<bool>())throw std::runtime_error("Widget lost after tray close");
            PostMessageW(main,widget_message(),7,0);report["graceful_exit_requested"]=true;
        }
        report["success"]=true;std::ofstream(output/L"alpha-validation.json")<<report.dump(2);
        std::cout<<report.dump(2)<<'\n';Gdiplus::GdiplusShutdown(token);return 0;
    }catch(const std::exception &e){report["error"]=e.what();if(!output.empty())std::ofstream(output/L"alpha-validation.json")<<report.dump(2);std::cerr<<e.what()<<'\n';Gdiplus::GdiplusShutdown(token);return 1;}
}
