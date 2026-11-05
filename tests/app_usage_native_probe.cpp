#include "pulsetrace/app_usage.hpp"
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <iostream>
using namespace pulsetrace;
LRESULT CALLBACK host_proc(HWND w,UINT m,WPARAM p,LPARAM l){if(m==WM_DESTROY){PostQuitMessage(0);return 0;}return DefWindowProcW(w,m,p,l);}
HWND process_window(DWORD pid){struct Search{DWORD pid;HWND window;}s{pid,nullptr};EnumWindows([](HWND w,LPARAM p)->BOOL{auto &s=*reinterpret_cast<Search*>(p);DWORD id=0;GetWindowThreadProcessId(w,&id);if(id==s.pid&&IsWindowVisible(w)){s.window=w;return FALSE;}return TRUE;},reinterpret_cast<LPARAM>(&s));return s.window;}
struct Child {
 PROCESS_INFORMATION p{};
 ~Child(){if(auto w=process_window(p.dwProcessId))PostMessageW(w,WM_CLOSE,0,0);if(p.hProcess){WaitForSingleObject(p.hProcess,5000);CloseHandle(p.hProcess);}if(p.hThread)CloseHandle(p.hThread);}
 void start(const std::filesystem::path &exe){STARTUPINFOW si{};si.cb=sizeof(si);auto command=L"\""+exe.wstring()+L"\" --host";if(!CreateProcessW(exe.c_str(),command.data(),nullptr,nullptr,FALSE,0,nullptr,nullptr,&si,&p))throw std::runtime_error("Cannot create controlled window");WaitForInputIdle(p.hProcess,5000);}
};
int main(int argc,char **argv){
 if(argc>1&&std::string(argv[1])=="--host"){WNDCLASSW wc{};wc.lpfnWndProc=host_proc;wc.lpszClassName=L"PulseTrace.UsageFixture";wc.hInstance=GetModuleHandleW(nullptr);RegisterClassW(&wc);auto w=CreateWindowW(wc.lpszClassName,L"PulseTrace controlled usage test",WS_OVERLAPPEDWINDOW|WS_VISIBLE,50,50,500,300,nullptr,nullptr,wc.hInstance,nullptr);if(!w)return 2;MSG m;while(GetMessageW(&m,nullptr,0,0)>0){TranslateMessage(&m);DispatchMessageW(&m);}return 0;}
 try{
  const auto directory=std::filesystem::absolute("data/tests/usage-live-"+std::to_string(GetTickCount64()));std::filesystem::create_directories(directory);wchar_t file[32768];GetModuleFileNameW(nullptr,file,32768);
  for(const auto *name:{L"usage-a.exe",L"usage-b.exe"})std::filesystem::copy_file(file,directory/name);
  const auto original=GetForegroundWindow();Child a,b;a.start(directory/L"usage-a.exe");b.start(directory/L"usage-b.exe");
  AppUsageMonitor monitor;Json config={{"usage_enabled",true},{"usage_idle_seconds",120},{"usage_exclusions",Json::array()}};monitor.configure(config,true);Sleep(500);
  const auto show=[&](DWORD pid){const auto window=process_window(pid);if(!window)throw std::runtime_error("Controlled window missing");ShowWindow(window,SW_RESTORE);SetForegroundWindow(window);Sleep(100);if(GetForegroundWindow()!=window)throw std::runtime_error("Foreground activation denied by Windows");Sleep(2300);};
  show(a.p.dwProcessId);show(b.p.dwProcessId);show(a.p.dwProcessId);monitor.configure(config,false);
  const auto rows=monitor.rows();std::uint64_t first=0,second=0;for(const auto &r:rows){if(r["active_ms"]>r["foreground_ms"])throw std::runtime_error("Active invariant failed");const auto path=r["path"].get<std::string>();if(path.ends_with("usage-a.exe"))first+=r["foreground_ms"].get<std::uint64_t>();if(path.ends_with("usage-b.exe"))second+=r["foreground_ms"].get<std::uint64_t>();}
  if(first<3000||second<1500||first+second>8000)throw std::runtime_error("Controlled foreground attribution mismatch");
  Sleep(1500);if(monitor.rows()!=rows)throw std::runtime_error("Paused monitor continued counting");
  config["usage_exclusions"]=Json::array({"usage-a.exe"});monitor.configure(config,true);Sleep(2300);monitor.configure(config,false);if(monitor.rows()!=rows)throw std::runtime_error("Excluded foreground counted");
  monitor.stop();if(original&&IsWindow(original))SetForegroundWindow(original);std::ofstream evidence(directory/"result.json");evidence<<Json{{"foreground_a_ms",first},{"foreground_b_ms",second},{"pause",true},{"exclusion",true},{"rows",rows}}.dump(2);
  std::cout<<"Controlled native foreground/pause/exclusion passed; evidence "<<directory<<'\n';return 0;
 }catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}
}
