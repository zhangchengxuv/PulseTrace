// Explicit Windows integration test. Controls only the setup process it launches.
// No existing installations, user data, or system settings are modified here.
#include <windows.h>
#include <objbase.h>
#include <objidl.h>
#include <tlhelp32.h>
#include <gdiplus.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <vector>
#include <functional>
#include <json.hpp>
using Json = nlohmann::json;
namespace fs = std::filesystem;
std::wstring text(HWND h) {
    wchar_t value[8192]{}; DWORD_PTR result{};
    SendMessageTimeoutW(h, WM_GETTEXT, 8192, reinterpret_cast<LPARAM>(value), SMTO_ABORTIFHUNG, 300, &result);
    return value;
}
std::string utf8(const std::wstring &s) {
    const auto n=WideCharToMultiByte(CP_UTF8,0,s.c_str(),static_cast<int>(s.size()),nullptr,0,nullptr,nullptr);
    std::string out(n,' '); WideCharToMultiByte(CP_UTF8,0,s.c_str(),static_cast<int>(s.size()),out.data(),n,nullptr,nullptr);return out;
}
std::wstring klass(HWND h) {wchar_t c[128]{};GetClassNameW(h,c,128);return c;}
std::vector<HWND> children(HWND h) {
    std::vector<HWND> result;
    EnumChildWindows(h,[](HWND child,LPARAM p)->BOOL {if(IsWindowVisible(child))reinterpret_cast<std::vector<HWND>*>(p)->push_back(child);return TRUE;},reinterpret_cast<LPARAM>(&result));return result;
}
struct Setup {
    PROCESS_INFORMATION process{};std::set<DWORD> pids;fs::path output;Json events=Json::array();
    Setup(const fs::path &exe,const fs::path &out,const std::wstring &language,bool attach=false):output(fs::absolute(out).lexically_normal().make_preferred()) {
        fs::create_directories(out);STARTUPINFOW startup{};startup.cb=sizeof(startup);
        if(attach){
            std::ifstream(output/"launched-pid.txt")>>process.dwProcessId;
            process.hProcess=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,process.dwProcessId);
            wchar_t image[32768]{};DWORD size=32768;
            if(!process.hProcess||!QueryFullProcessImageNameW(process.hProcess,0,image,&size)||fs::weakly_canonical(image)!=fs::weakly_canonical(exe))throw std::runtime_error("Recorded test process no longer matches setup");
            pids.insert(process.dwProcessId);return;
        }
        auto command=L"\""+exe.wstring()+L"\" /CURRENTUSER /NOLEGACYDETECT /LANG="+language+L" /LOG=\""+(out/L"setup.log").wstring()+L"\"";
        if(!CreateProcessW(exe.c_str(),command.data(),nullptr,nullptr,FALSE,0,nullptr,nullptr,&startup,&process))throw std::runtime_error("Cannot launch test setup");pids.insert(process.dwProcessId);
        std::ofstream(output/"launched-pid.txt")<<process.dwProcessId;
    }
    ~Setup(){try{if(wizard())cancel();}catch(...){std::cerr<<"Test setup still open; inspect its fixture log\n";}CloseHandle(process.hThread);CloseHandle(process.hProcess);}
    std::vector<HWND> windows() {
        auto snap=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);PROCESSENTRY32W entry{};entry.dwSize=sizeof(entry);
        std::vector<PROCESSENTRY32W> all;if(Process32FirstW(snap,&entry))do{all.push_back(entry);}while(Process32NextW(snap,&entry));CloseHandle(snap);
        for(size_t i=0;i<all.size();++i)for(const auto &p:all)if(pids.contains(p.th32ParentProcessID))pids.insert(p.th32ProcessID);
        struct State{Setup *self;std::vector<HWND> result;}state{this,{}};
        EnumWindows([](HWND h,LPARAM p)->BOOL {auto &s=*reinterpret_cast<State*>(p);DWORD id{};GetWindowThreadProcessId(h,&id);if(s.self->pids.contains(id)&&IsWindowVisible(h))s.result.push_back(h);return TRUE;},reinterpret_cast<LPARAM>(&state));return state.result;
    }
    HWND wizard() {for(auto h:windows())if(klass(h)==L"TWizardForm")return h;return nullptr;}
    std::wstring allText(HWND window){auto s=text(window);for(auto h:children(window))s+=L"\n"+text(h);return s;}
    void wait(const std::function<bool()> &condition,const char *reason) {
        const auto end=GetTickCount64()+15000;while(GetTickCount64()<end){if(condition())return;Sleep(80);}
        int i=0;for(auto h:windows())shot(h,("failure-"+std::to_string(i++)).c_str());throw std::runtime_error(reason);
    }
    void shot(HWND h,const char *name) {
        RECT r{};GetWindowRect(h,&r);auto screen=GetDC(nullptr),dc=CreateCompatibleDC(screen);
        auto bmp=CreateCompatibleBitmap(screen,r.right-r.left,r.bottom-r.top);auto old=SelectObject(dc,bmp);
        PrintWindow(h,dc,2);CLSID png{};CLSIDFromString(L"{557CF406-1A04-11D3-9A73-0000F81EF32E}",&png);
        {Gdiplus::Bitmap image(bmp,nullptr);image.Save((output/(std::string(name)+".png")).c_str(),&png,nullptr);}
        SelectObject(dc,old);DeleteObject(bmp);DeleteDC(dc);ReleaseDC(nullptr,screen);
        Json controls=Json::array();for(auto c:children(h))controls.push_back({{"class",utf8(klass(c))},{"id",GetDlgCtrlID(c)},{"text",utf8(text(c))}});
        events.push_back({{"phase",name},{"text",utf8(allText(h))},{"controls",controls}});
        std::ofstream(output/"windows.json")<<events.dump(2);
    }
    HWND button(HWND window,const wchar_t *en,const wchar_t *zh) {
        for(auto h:children(window)){auto t=text(h);if(IsWindowEnabled(h)&&klass(h).find(L"Button")!=std::wstring::npos&&(t.find(en)!=t.npos||t.find(zh)!=t.npos))return h;}throw std::runtime_error("Expected setup button not found");
    }
    void click(HWND h){PostMessageW(GetParent(h),WM_COMMAND,MAKEWPARAM(GetDlgCtrlID(h),BN_CLICKED),reinterpret_cast<LPARAM>(h));Sleep(250);}
    void cancel() {
        auto h=wizard();if(h)click(button(h,L"Cancel",L"取消"));
        const auto end=GetTickCount64()+5000;
        while(GetTickCount64()<end){auto ws=windows();if(ws.empty())return;for(auto w:ws){if(klass(w)==L"#32770"||klass(w)==L"TMessageForm"){for(auto c:children(w))if(GetDlgCtrlID(c)==IDYES||text(c)==L"&Yes"||text(c).find(L"是(")!=std::wstring::npos)click(c);}}Sleep(80);}
        throw std::runtime_error("Test setup did not exit after cancellation");
    }
};
int wmain(int argc,wchar_t **argv) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    Gdiplus::GdiplusStartupInput input;ULONG_PTR gdip{};Gdiplus::GdiplusStartup(&gdip,&input,nullptr);
    try {
        if(argc!=5)throw std::runtime_error("SETUP OUTPUT LANGUAGE baseline|navigate|cancel-welcome|default-denied|cleanup");
        const std::wstring mode=argv[4];
        if(mode!=L"baseline"&&mode!=L"navigate"&&mode!=L"cancel-welcome"&&mode!=L"default-denied"&&mode!=L"cleanup")throw std::runtime_error("Unknown wizard test mode");
        Setup setup(argv[1],argv[2],argv[3],std::wstring(argv[4])==L"cleanup");const bool baseline=std::wstring(argv[4])==L"baseline";
        if(std::wstring(argv[4])==L"cleanup"){setup.cancel();std::cout<<"Recorded test setup cancelled\n";return 0;}
        if(baseline){
            HWND error{};setup.wait([&]{for(auto w:setup.windows())if(setup.allText(w).find(L"before it was initialized")!=std::wstring::npos){error=w;return true;}return false;},"Expected baseline error was not reproduced");
            setup.shot(error,"baseline-error");setup.click(setup.button(error,L"OK",L"确定"));
            // After dismissal Inno terminates or returns to its wizard; cancel only this test instance.
            Sleep(250);if(setup.wizard())setup.cancel();
            setup.wait([&]{return setup.windows().empty();},"Baseline dialog did not close");
            std::cout<<"Baseline app-constant failure reproduced\n";
        }else{
            setup.wait([&]{return setup.wizard()!=nullptr;},"Wizard did not open");auto h=setup.wizard();setup.shot(h,"welcome");
            if(std::wstring(argv[4])==L"cancel-welcome"){
                setup.cancel();std::ofstream(setup.output/"result.json")<<Json({{"success",true},{"mode","cancel-welcome"}}).dump(2);
                std::cout<<"Cancellation from Welcome passed\n";Gdiplus::GdiplusShutdown(gdip);return 0;
            }
            setup.click(setup.button(h,L"Next",L"下一步"));setup.shot(h,"directory-page");HWND edit{};
            setup.wait([&]{for(auto c:children(h))if(klass(c)==L"TNewPathEdit"){edit=c;return true;}return false;},"Directory page did not open");
            setup.shot(h,"default-directory");const auto initial=text(edit);
            if(GetFileAttributesW(L"D:\\")!=INVALID_FILE_ATTRIBUTES&&initial!=L"D:\\Program Files\\PulseTrace")throw std::runtime_error("Unexpected fresh-install D default");
            if(std::wstring(argv[4])==L"default-denied"){
                setup.click(setup.button(h,L"Next",L"下一步"));HWND denied{};
                setup.wait([&]{for(auto w:setup.windows())if(w!=h&&(setup.allText(w).find(L"not writable")!=std::wstring::npos||setup.allText(w).find(L"不可写")!=std::wstring::npos)){denied=w;return true;}return false;},"Expected default-directory permission refusal was not shown");
                setup.shot(denied,"default-permission-refusal");setup.click(setup.button(denied,L"OK",L"确定"));setup.cancel();
                std::ofstream(setup.output/"result.json")<<Json({{"success",true},{"mode","default-denied"},{"default_install_completed",false}}).dump(2);
                std::cout<<"Default path denied safely; elevated default installation remains unverified\n";Gdiplus::GdiplusShutdown(gdip);return 0;
            }
            const auto first=(setup.output/L"First App").wstring();SendMessageW(edit,WM_SETTEXT,0,reinterpret_cast<LPARAM>(first.c_str()));
            setup.click(setup.button(h,L"Next",L"下一步"));setup.shot(h,"after-directory");
            setup.click(setup.button(h,L"Back",L"上一步"));setup.wait([&]{return IsWindowVisible(edit);},"Back did not restore directory page");
            const auto second=(setup.output/L"Second App").wstring();SendMessageW(edit,WM_SETTEXT,0,reinterpret_cast<LPARAM>(second.c_str()));setup.shot(h,"reselected-directory");
            if(setup.allText(h).find(second+L"\\data")==std::wstring::npos)throw std::runtime_error("Data preview did not follow directory reselection");
            setup.click(setup.button(h,L"Next",L"下一步"));
            // Tasks -> Ready; unchecked portable import must not introduce a second data-directory page.
            setup.click(setup.button(h,L"Next",L"下一步"));setup.shot(h,"ready");
            if(setup.allText(h).find(second)==std::wstring::npos)throw std::runtime_error("Ready page uses stale installation directory");
            setup.cancel();if(fs::exists(setup.output/L"First App/data/monitor.db")||fs::exists(setup.output/L"Second App/data/monitor.db"))throw std::runtime_error("Cancelled wizard wrote a database");
            std::ofstream(setup.output/"result.json")<<Json({{"success",true},{"mode","navigate"},{"startup",true},{"back_and_reselect",true},{"data_preview_follows_directory",true},{"cancel",true}}).dump(2);
            std::cout<<"Wizard startup, default, forward/back, reselect and cancel passed\n";
        }
        Gdiplus::GdiplusShutdown(gdip);return 0;
    }catch(const std::exception &e){std::cerr<<e.what()<<'\n';Gdiplus::GdiplusShutdown(gdip);return 1;}
}
