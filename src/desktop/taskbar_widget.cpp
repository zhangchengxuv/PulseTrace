// Independent Win32 implementation. No TrafficMonitor code or MFC is included.
#include "pulsetrace/desktop/widget_ipc.hpp"
#include "pulsetrace/desktop/taskbar_layout.hpp"
#include <uiautomation.h>
#include <shellapi.h>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <vector>
#include <fstream>
#include <array>
#include <dwmapi.h>
#include "pulsetrace/localization.hpp"
using namespace pulsetrace;
namespace {
bool geometry_dirty=true;
void CALLBACK geometry_event(HWINEVENTHOOK,DWORD,HWND,LONG,LONG,DWORD,DWORD){geometry_dirty=true;}
std::wstring rate(double n){unsigned unit=0;while(n>=1024&&unit<3){n/=1024;++unit;}std::wostringstream s;s<<std::fixed<<std::setprecision(unit?1:0)<<n<<L" "<<std::array{L"B/s",L"KiB/s",L"MiB/s",L"GiB/s"}[unit];return s.str();}
HFONT widget_font(HDC dc,UINT dpi){
 auto make=[&](const wchar_t *name){return CreateFontW(-MulDiv(10,dpi,72),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,name);};
 auto font=make(L"Segoe UI Variable Text");auto old=SelectObject(dc,font);wchar_t actual[128]{};GetTextFaceW(dc,128,actual);SelectObject(dc,old);
 if(wcsncmp(actual,L"Segoe UI",8)){DeleteObject(font);font=make(L"Segoe UI");}return font;
}
struct AccessibleRects {
 IUIAutomation2 *uia{};
 AccessibleRects(){if(SUCCEEDED(CoCreateInstance(CLSID_CUIAutomation8,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&uia)))){uia->put_ConnectionTimeout(250);uia->put_TransactionTimeout(250);uia->put_AutoSetFocus(FALSE);}}
 ~AccessibleRects(){if(uia)uia->Release();}
 bool get(HWND bar,std::vector<RECT> &rects){if(!uia)return false;IUIAutomationElement *root=nullptr;IUIAutomationCondition *condition=nullptr;IUIAutomationElementArray *elements=nullptr;bool ok=false;
  if(SUCCEEDED(uia->ElementFromHandle(bar,&root))&&SUCCEEDED(uia->CreateTrueCondition(&condition))&&SUCCEEDED(root->FindAll(TreeScope_Descendants,condition,&elements))){int count=0;const auto deadline=GetTickCount64()+350;
   if(SUCCEEDED(elements->get_Length(&count))&&count>0&&count<=512){ok=true;for(int i=0;i<count;++i){if(GetTickCount64()>deadline){ok=false;break;}IUIAutomationElement *item=nullptr;if(FAILED(elements->GetElement(i,&item))){ok=false;break;}CONTROLTYPEID type{};RECT r{};BOOL off=TRUE;int pid=0;
    const bool read=SUCCEEDED(item->get_CurrentControlType(&type))&&SUCCEEDED(item->get_CurrentIsOffscreen(&off))&&SUCCEEDED(item->get_CurrentProcessId(&pid))&&SUCCEEDED(item->get_CurrentBoundingRectangle(&r));item->Release();if(!read){ok=false;break;}
    if(!off&&pid!=static_cast<int>(GetCurrentProcessId())&&type!=UIA_PaneControlTypeId&&type!=UIA_GroupControlTypeId&&type!=UIA_ToolBarControlTypeId&&type!=UIA_WindowControlTypeId&&r.right>r.left&&r.bottom>r.top)rects.push_back(r);
   }}
  }if(elements)elements->Release();if(condition)condition->Release();if(root)root->Release();return ok;
 }
};
class Widget {
 public:
  WidgetShared *shared{};HANDLE owner{};HWND control{},window{},bar{};WidgetSnapshot data{};WidgetFeedback feedback{};AccessibleRects accessibility;
  UINT previous_mode=99;DWORD previous_monitor=99;unsigned attempts{};ULONGLONG last_layout{};HWINEVENTHOOK hook{};bool dark=true;std::wstring line1,line2;
  struct Pixel{int x,y;COLORREF color;};std::vector<Pixel> text_pixels;unsigned invisible_checks{};bool composition_fallback{};bool fullscreen{};ULONGLONG last_visibility_check{};
  ~Widget(){if(hook)UnhookWinEvent(hook);if(window)DestroyWindow(window);if(control)DestroyWindow(control);}
  static LRESULT CALLBACK proc(HWND h,UINT m,WPARAM w,LPARAM l){auto *self=reinterpret_cast<Widget *>(GetWindowLongPtrW(h,GWLP_USERDATA));if(m==WM_NCCREATE){self=static_cast<Widget *>(reinterpret_cast<CREATESTRUCTW *>(l)->lpCreateParams);SetWindowLongPtrW(h,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}if(!self)return DefWindowProcW(h,m,w,l);
   if(m==WM_TIMER){self->tick();return 0;}if(m==WM_MOUSEACTIVATE)return MA_NOACTIVATE;if(m==WM_ERASEBKGND)return 1;
   if(m==WM_PAINT){self->paint(h);return 0;}if(m==WM_LBUTTONUP){self->action(1);return 0;}if(m==WM_RBUTTONUP){self->menu();return 0;}
   if(m==WM_DPICHANGED||m==WM_DISPLAYCHANGE||m==WM_SETTINGCHANGE){geometry_dirty=true;return 0;}
   if(m==WM_NCHITTEST&&self->feedback.mode==1&&(GetKeyState(VK_CONTROL)&0x8000))return HTCAPTION;
   if(m==WM_NCDESTROY){if(self->window==h)self->window=nullptr;if(self->control==h)self->control=nullptr;}
   return DefWindowProcW(h,m,w,l);
  }
  HWND choose_bar(){if(data.monitor==0)return FindWindowW(L"Shell_TrayWnd",nullptr);std::vector<HWND> bars;EnumWindows([](HWND h,LPARAM p)->BOOL{wchar_t name[128]{};GetClassNameW(h,name,128);if(wcscmp(name,L"Shell_SecondaryTrayWnd")==0)reinterpret_cast<std::vector<HWND> *>(p)->push_back(h);return TRUE;},reinterpret_cast<LPARAM>(&bars));std::sort(bars.begin(),bars.end(),[](HWND a,HWND b){RECT ra{},rb{};GetWindowRect(a,&ra);GetWindowRect(b,&rb);return ra.left==rb.left?ra.top<rb.top:ra.left<rb.left;});return data.monitor<=bars.size()?bars[data.monitor-1]:nullptr;}
  void destroy(){if(window){DestroyWindow(window);window=nullptr;}text_pixels.clear();feedback.visibility=0;}
  void status(UINT mode,const wchar_t *reason,DWORD error=0){feedback.mode=mode;feedback.attempts=attempts;feedback.win32_error=error;feedback.hwnd=reinterpret_cast<UINT_PTR>(window);feedback.parent=reinterpret_cast<UINT_PTR>(window?GetParent(window):nullptr);feedback.bounds={};if(window)GetWindowRect(window,&feedback.bounds);wcsncpy_s(feedback.reason,reason,_TRUNCATE);publish_snapshot(shared->feedback_sequence,shared->feedback,feedback);}
  HWND create(){return CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE|WS_EX_LAYERED,L"PulseTrace.TrafficWidget",L"PulseTrace network speed",WS_POPUP,0,0,1,1,nullptr,nullptr,GetModuleHandleW(nullptr),this);}
  void layout(){last_layout=GetTickCount64();geometry_dirty=false;
   const auto found=choose_bar();if(found!=bar||data.mode!=previous_mode||data.monitor!=previous_monitor){destroy();bar=found;attempts=0;invisible_checks=0;composition_fallback=false;previous_mode=data.mode;previous_monitor=data.monitor;if(hook){UnhookWinEvent(hook);hook=nullptr;}if(bar){DWORD pid=0;GetWindowThreadProcessId(bar,&pid);hook=SetWinEventHook(EVENT_OBJECT_CREATE,EVENT_OBJECT_LOCATIONCHANGE,nullptr,geometry_event,pid,0,WINEVENT_OUTOFCONTEXT|WINEVENT_SKIPOWNPROCESS);}}
   if(data.mode==2){destroy();status(2,L"Tray-only selected");return;}
   if(data.mode==1||composition_fallback){if(!window)window=create();if(!window){status(2,L"Floating window creation failed",GetLastError());return;}SetLayeredWindowAttributes(window,0,255,LWA_ALPHA);MONITORINFO info{sizeof(info)};GetMonitorInfoW(MonitorFromWindow(bar?bar:control,MONITOR_DEFAULTTOPRIMARY),&info);const auto dpi=GetDpiForWindow(window);const int width=MulDiv(156,dpi,96),height=MulDiv(40,dpi,96);if(feedback.mode!=1)SetWindowPos(window,HWND_TOPMOST,info.rcWork.right-width-12,info.rcWork.top+12,width,height,SWP_NOACTIVATE);ShowWindow(window,SW_SHOWNOACTIVATE);status(1,composition_fallback?L"Embedded rendering was not visible; floating fallback":L"Independent floating window");return;}
   if(!bar||!IsWindow(bar)){destroy();status(2,L"Taskbar unavailable");return;}if(attempts>=3){destroy();status(2,L"SetParent retry limit reached");return;}
   RECT taskbar{},notify{},slot{};const auto tray=FindWindowExW(bar,nullptr,L"TrayNotifyWnd",nullptr);GetWindowRect(bar,&taskbar);
   if(!tray||!GetWindowRect(tray,&notify)){if(window)ShowWindow(window,SW_HIDE);status(2,L"Notification region not verifiable; no guessed coordinates");return;}
   MONITORINFO monitor{sizeof(monitor)};GetMonitorInfoW(MonitorFromWindow(bar,MONITOR_DEFAULTTONULL),&monitor);if(!inside_rect(taskbar,monitor.rcMonitor)||!IsWindowVisible(bar)){if(window)ShowWindow(window,SW_HIDE);status(window?0:2,L"Taskbar hidden");return;}
   // Query Explorer's content subtree only. Scanning Shell_TrayWnd would include
   // our own child provider and can deadlock a same-thread UIA call after embedding.
   auto content=FindWindowExW(bar,nullptr,L"Windows.UI.Composition.DesktopWindowContentBridge",nullptr);
   if(!content)content=FindWindowExW(bar,nullptr,L"ReBarWindow32",nullptr);
   std::vector<RECT> occupied;if(!content||!accessibility.get(content,occupied)){if(window)ShowWindow(window,SW_HIDE);status(2,L"Cannot verify taskbar occupied elements");return;}
   // Account for other PulseTrace instances, without reading taskbar application names.
   auto occupied_context=std::pair{this,&occupied};
   EnumChildWindows(bar,[](HWND child,LPARAM p)->BOOL{auto *pair=reinterpret_cast<std::pair<Widget *,std::vector<RECT> *> *>(p);wchar_t c[128]{};GetClassNameW(child,c,128);if(child!=pair->first->window&&wcscmp(c,L"PulseTrace.TrafficWidget")==0&&IsWindowVisible(child)){RECT r{};GetWindowRect(child,&r);pair->second->push_back(r);}return TRUE;},reinterpret_cast<LPARAM>(&occupied_context));
   if(!taskbar_slot(taskbar,notify,occupied,GetDpiForWindow(bar),slot)){if(window)ShowWindow(window,SW_HIDE);status(2,L"Insufficient verified free taskbar space");return;}
   if(!window){window=create();if(!window){status(2,L"Widget creation failed",GetLastError());return;}SetWindowLongPtrW(window,GWL_STYLE,WS_CHILD|WS_CLIPSIBLINGS);SetLastError(ERROR_SUCCESS);SetParent(window,bar);const auto error=GetLastError();++attempts;if(GetParent(window)!=bar){destroy();status(2,L"SetParent failed",error);return;}}
   if(!SetLayeredWindowAttributes(window,0,255,LWA_ALPHA)){destroy();status(2,L"Layered taskbar composition failed",GetLastError());return;}POINT origin{slot.left,slot.top};MapWindowPoints(nullptr,bar,&origin,1);SetWindowPos(window,HWND_TOP,origin.x,origin.y,slot.right-slot.left,slot.bottom-slot.top,SWP_NOACTIVATE|SWP_SHOWWINDOW);
   RECT actual{};GetWindowRect(window,&actual);if(GetParent(window)!=bar||!inside_rect(actual,taskbar)){destroy();feedback.visibility=4;status(2,L"Embedded geometry verification failed");return;}status(0,feedback.visibility==1?L"Embedded and screen-visible":L"Attached; verifying on-screen rendering");
  }
  void verify_visible(){if(!window||fullscreen||feedback.mode>1)return;if(feedback.visibility==1&&GetTickCount64()-last_visibility_check<5000)return;last_visibility_check=GetTickCount64();RECT r{};GetWindowRect(window,&r);if(!IsWindowVisible(window)){feedback.visibility=2;status(feedback.mode,L"Attached but hidden");return;}if(text_pixels.empty()){feedback.visibility=0;return;}
   // One small readback of our own rectangle; no desktop recording or per-pixel GPU calls.
   auto screen=GetDC(nullptr);auto memory=CreateCompatibleDC(screen);BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=r.right-r.left;info.bmiHeader.biHeight=-(r.bottom-r.top);info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;void *bytes=nullptr;auto bitmap=CreateDIBSection(screen,&info,DIB_RGB_COLORS,&bytes,nullptr,0);unsigned matches=0;
   if(memory&&bitmap){auto old=SelectObject(memory,bitmap);if(BitBlt(memory,0,0,r.right-r.left,r.bottom-r.top,screen,r.left,r.top,SRCCOPY|CAPTUREBLT)){GdiFlush();for(const auto &pixel:text_pixels){if(pixel.x<0||pixel.y<0||pixel.x>=r.right-r.left||pixel.y>=r.bottom-r.top)continue;const auto p=static_cast<const BYTE*>(bytes)+(pixel.y*(r.right-r.left)+pixel.x)*4;const auto distance=abs(static_cast<int>(p[2])-GetRValue(pixel.color))+abs(static_cast<int>(p[1])-GetGValue(pixel.color))+abs(static_cast<int>(p[0])-GetBValue(pixel.color));if(distance<65)++matches;}}SelectObject(memory,old);}if(bitmap)DeleteObject(bitmap);if(memory)DeleteDC(memory);ReleaseDC(nullptr,screen);feedback.text_samples=static_cast<UINT>(text_pixels.size());feedback.screen_matches=matches;
   if(matches>=4&&matches*2>=text_pixels.size()){feedback.visibility=1;invisible_checks=0;status(feedback.mode,feedback.mode==0?L"Embedded and screen-visible":L"Floating and screen-visible");}
   else{feedback.visibility=3;status(feedback.mode,L"Attached and painted, but screen pixels are occluded");if(feedback.mode==0&&++invisible_checks>=3){destroy();composition_fallback=true;layout();}}
  }
  void tick(){if(owner&&WaitForSingleObject(owner,0)!=WAIT_TIMEOUT){PostQuitMessage(0);return;}WidgetSnapshot s;if(!read_snapshot(shared->data_sequence,shared->data,s)||!valid_snapshot(s)){line1=ui_text("widget.ipc");line2=L"PulseTrace";}else{data=s;set_ui_language(data.language==1?"zh-CN":"en-US");if(data.flags&0x80000000){PostQuitMessage(0);return;}if(!(data.flags&1)){line1=ui_text("widget.paused");line2=L"PulseTrace";}else if(!(data.flags&2)||(data.flags&12)||GetTickCount64()-data.tick>5000){line1=ui_text("widget.unavailable");line2=ui_text("widget.check_interface");}else{line1=L"↓ "+rate(data.download);line2=L"↑ "+rate(data.upload);}}
   DWORD light=0,size=sizeof(light);RegGetValueW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",L"SystemUsesLightTheme",RRF_RT_REG_DWORD,nullptr,&light,&size);dark=!light;
   if(geometry_dirty||choose_bar()!=bar||data.mode!=previous_mode||data.monitor!=previous_monitor||GetTickCount64()-last_layout>5000)layout();
   const bool previously_fullscreen=fullscreen;fullscreen=false;
   if(window&&feedback.mode==0){RECT fg{};const auto front=GetForegroundWindow();MONITORINFO mi{sizeof(mi)};GetMonitorInfoW(MonitorFromWindow(window,MONITOR_DEFAULTTOPRIMARY),&mi);wchar_t cls[128]{};GetClassNameW(front,cls,128);if(front&&front!=bar&&front!=window&&wcscmp(cls,L"Progman")&&wcscmp(cls,L"WorkerW")&&GetWindowRect(front,&fg)&&fg.left<=mi.rcMonitor.left&&fg.top<=mi.rcMonitor.top&&fg.right>=mi.rcMonitor.right&&fg.bottom>=mi.rcMonitor.bottom)fullscreen=true;
    if(fullscreen){ShowWindow(window,SW_HIDE);feedback.visibility=2;status(0,L"Temporarily hidden by fullscreen application");}
    else if(previously_fullscreen){ShowWindow(window,SW_SHOWNOACTIVATE);geometry_dirty=true;}
   }
   if(window&&!fullscreen){verify_visible();InvalidateRect(window,nullptr,FALSE);}
  }
  void paint(HWND h){
   PAINTSTRUCT ps{};auto dc=BeginPaint(h,&ps);RECT r{};GetClientRect(h,&r);
   if(r.right>0&&r.bottom>0){
    BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=r.right;info.bmiHeader.biHeight=-r.bottom;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;void *pixels=nullptr;
    auto mem=CreateCompatibleDC(dc);auto bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&pixels,nullptr,0);
    if(mem&&bitmap){auto old=SelectObject(mem,bitmap);const auto color=dark?RGB(10,38,57):RGB(232,241,247);auto brush=CreateSolidBrush(color);FillRect(mem,&r,brush);DeleteObject(brush);SetBkMode(mem,TRANSPARENT);const auto dpi=GetDpiForWindow(h);auto font=widget_font(mem,dpi);auto prior=SelectObject(mem,font);RECT top=r;top.bottom=r.bottom/2;RECT bottom=r;bottom.top=top.bottom;SetTextColor(mem,dark?RGB(139,196,234):RGB(25,96,144));DrawTextW(mem,line1.c_str(),-1,&top,DT_CENTER|DT_VCENTER|DT_SINGLELINE);SetTextColor(mem,dark?RGB(52,152,219):RGB(25,96,144));DrawTextW(mem,line2.c_str(),-1,&bottom,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
     BitBlt(dc,0,0,r.right,r.bottom,mem,0,0,SRCCOPY);GdiFlush();std::vector<Pixel> candidates;const auto bytes=static_cast<const BYTE*>(pixels);
     for(int y=1;y<r.bottom-1;++y)for(int x=1;x<r.right-1;++x){const auto p=bytes+(y*r.right+x)*4;const auto c=RGB(p[2],p[1],p[0]);const auto distance=abs(p[2]-static_cast<int>(GetRValue(color)))+abs(p[1]-static_cast<int>(GetGValue(color)))+abs(p[0]-static_cast<int>(GetBValue(color)));if(distance>180)candidates.push_back({x,y,c});}
     text_pixels.clear();for(std::size_t i=0;i<16&&i<candidates.size();i++)text_pixels.push_back(candidates[i*candidates.size()/16]);++feedback.paint_count;SelectObject(mem,prior);DeleteObject(font);SelectObject(mem,old);
    }
    if(bitmap)DeleteObject(bitmap);if(mem)DeleteDC(mem);
   }EndPaint(h,&ps);
  }
  void action(unsigned command){DWORD pid=0;auto hwnd=reinterpret_cast<HWND>(static_cast<UINT_PTR>(data.owner_window));GetWindowThreadProcessId(hwnd,&pid);if(pid==data.owner_pid)PostMessageW(hwnd,widget_message(),command,0);}
  void menu(){auto m=CreatePopupMenu();for(const auto &[id,label]:std::vector<std::pair<UINT,const wchar_t *>>{{1,ui_text("tray.open")},{2,ui_text("widget.hide")},{3,ui_text("widget.embedded")},{4,ui_text("widget.floating")},{5,ui_text("widget.tray")},{6,ui_text("widget.interface")},{7,ui_text("tray.exit")}})AppendMenuW(m,MF_STRING,id,label);POINT p{};GetCursorPos(&p);SetForegroundWindow(window);const auto chosen=TrackPopupMenu(m,TPM_RETURNCMD|TPM_NONOTIFY|TPM_RIGHTBUTTON,p.x,p.y,0,window,nullptr);DestroyMenu(m);if(chosen)action(chosen);}
};
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int){
 int argc=0;auto argv=CommandLineToArgvW(GetCommandLineW(),&argc);DWORD pid=0;for(int i=1;i+1<argc;++i)if(std::wstring(argv[i])==L"--owner")pid=wcstoul(argv[++i],nullptr,10);LocalFree(argv);if(!pid)return 2;
 const auto taskbar=FindWindowW(L"Shell_TrayWnd",nullptr);const auto context=taskbar?GetWindowDpiAwarenessContext(taskbar):DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2;SetProcessDpiAwarenessContext(context);
 CoInitializeEx(nullptr,COINIT_MULTITHREADED);int exit=0;{
 Widget app;app.owner=OpenProcess(SYNCHRONIZE,FALSE,pid);HANDLE mapping=OpenFileMappingW(FILE_MAP_ALL_ACCESS,FALSE,widget_mapping_name(pid).c_str());
 if(!mapping||!app.owner){if(mapping)CloseHandle(mapping);if(app.owner)CloseHandle(app.owner);CoUninitialize();return 3;}
 app.shared=static_cast<WidgetShared *>(MapViewOfFile(mapping,FILE_MAP_ALL_ACCESS,0,0,sizeof(WidgetShared)));if(!app.shared){CloseHandle(mapping);CloseHandle(app.owner);CoUninitialize();return 4;}
 WNDCLASSW wc{};wc.lpfnWndProc=Widget::proc;wc.hInstance=instance;wc.lpszClassName=L"PulseTrace.TrafficWidget";wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);RegisterClassW(&wc);
 app.control=CreateWindowExW(0,wc.lpszClassName,L"",0,0,0,0,0,HWND_MESSAGE,nullptr,instance,&app);SetTimer(app.control,1,1000,nullptr);app.tick();MSG msg{};while(GetMessageW(&msg,nullptr,0,0)>0){TranslateMessage(&msg);DispatchMessageW(&msg);}app.destroy();UnmapViewOfFile(app.shared);app.shared=nullptr;CloseHandle(mapping);CloseHandle(app.owner);app.owner=nullptr;
 }CoUninitialize();return exit;
}
