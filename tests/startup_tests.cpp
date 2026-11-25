#include "pulsetrace/desktop/startup.hpp"
#include "pulsetrace/protocol.hpp"
#include <objbase.h>
#include <oleauto.h>
#include <taskschd.h>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>

using namespace pulsetrace;
namespace pulsetrace {std::wstring current_user_sid();}
namespace {
constexpr wchar_t run[]=L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t approved[]=L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\Run";
void check(bool result,const char *message){if(!result)throw std::runtime_error(message);}
struct Fixture {
    std::wstring name=L"Software\\PulseTraceStartupTest-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64());
    HKEY key{};
    Fixture(){check(RegCreateKeyExW(HKEY_CURRENT_USER,name.c_str(),0,nullptr,REG_OPTION_NON_VOLATILE,KEY_ALL_ACCESS,nullptr,&key,nullptr)==ERROR_SUCCESS,"isolated registry fixture");}
    ~Fixture(){RegCloseKey(key);RegDeleteTreeW(HKEY_CURRENT_USER,name.c_str());}
    void approval(BYTE code){
        HKEY value{};
        check(RegCreateKeyExW(key,approved,0,nullptr,REG_OPTION_VOLATILE,KEY_SET_VALUE,nullptr,&value,nullptr)==ERROR_SUCCESS,"approval fixture");
        const BYTE bytes[12]{code};
        const auto result=RegSetValueExW(value,L"PulseTrace",0,REG_BINARY,bytes,sizeof(bytes));
        RegCloseKey(value);
        check(result==ERROR_SUCCESS,"write approval fixture");
    }
    std::wstring command()const{
        wchar_t text[4096]{};DWORD size=sizeof(text);
        check(RegGetValueW(key,run,L"PulseTrace",RRF_RT_REG_SZ,nullptr,text,&size)==ERROR_SUCCESS,"read command");
        return text;
    }
};
struct Release { void operator()(IUnknown *object)const noexcept{object->Release();} };
void validate_task(const std::filesystem::path &exe,const std::filesystem::path &data) {
    const auto xml=startup_task_xml(current_user_sid(),exe,data);
    check(xml.find(L"HighestAvailable")!=std::wstring::npos&&xml.find(L"<LogonTrigger>")!=std::wstring::npos&&
          xml.find(L"&amp;")!=std::wstring::npos,"elevated sign-in definition and XML path escaping");
    ITaskService *service_ptr{};
    const auto service_hr=CoCreateInstance(CLSID_TaskScheduler,nullptr,CLSCTX_INPROC_SERVER,IID_ITaskService,
                                          reinterpret_cast<void **>(&service_ptr));
    std::unique_ptr<ITaskService,Release> service(service_ptr);
    check(SUCCEEDED(service_hr)&&service,"Task Scheduler API available");
    check(SUCCEEDED(service->Connect({}, {}, {}, {})),"Task Scheduler COM connection");
    ITaskDefinition *definition_ptr{};
    const auto new_task=service->NewTask(0,&definition_ptr);
    std::unique_ptr<ITaskDefinition,Release> definition(definition_ptr);
    check(SUCCEEDED(new_task)&&definition,"unregistered task definition");
    const std::unique_ptr<wchar_t,decltype(&SysFreeString)> text(SysAllocString(xml.c_str()),SysFreeString);
    check(text&&SUCCEEDED(definition->put_XmlText(text.get())),"Windows accepts task definition XML");
    IPrincipal *principal_ptr{};
    const auto principal_hr=definition->get_Principal(&principal_ptr);
    std::unique_ptr<IPrincipal,Release> principal(principal_ptr);
    TASK_RUNLEVEL_TYPE level{};TASK_LOGON_TYPE logon{};
    check(SUCCEEDED(principal_hr)&&principal&&SUCCEEDED(principal->get_RunLevel(&level))&&level==TASK_RUNLEVEL_HIGHEST&&
          SUCCEEDED(principal->get_LogonType(&logon))&&logon==TASK_LOGON_INTERACTIVE_TOKEN,
          "scheduler resolved highest available and interactive token without registering a task");
    check(read_startup_task().available,"installed task status can be queried even when the app has no task");
    ITaskFolder *folder_ptr{};
    const std::unique_ptr<wchar_t,decltype(&SysFreeString)> root(SysAllocString(L"\\"),SysFreeString);
    const auto folder_hr=service->GetFolder(root.get(),&folder_ptr);
    std::unique_ptr<ITaskFolder,Release> folder(folder_ptr);
    check(SUCCEEDED(folder_hr)&&folder,"Task Scheduler folder available");
    const std::unique_ptr<wchar_t,decltype(&SysFreeString)> name(SysAllocString(L"PulseTrace-ValidationOnly"),SysFreeString);
    const std::unique_ptr<wchar_t,decltype(&SysFreeString)> user(SysAllocString(current_user_sid().c_str()),SysFreeString);
    VARIANT account{};account.vt=VT_BSTR;account.bstrVal=user.get();
    IRegisteredTask *unused{};
    const auto validation=folder->RegisterTask(name.get(),text.get(),TASK_VALIDATE_ONLY,account,{},
                                               TASK_LOGON_INTERACTIVE_TOKEN,{},&unused);
    std::unique_ptr<IRegisteredTask,Release> cleanup(unused);
    check(SUCCEEDED(validation),"Windows validation-only API accepts the logon task");
}
}
int main(){
    try{
        Fixture fixture;
        const auto directory=std::filesystem::absolute("data/tests/startup-"+std::to_string(GetTickCount64()));
        const auto old=directory/L"Old place"/L"PulseTrace.exe",next=directory/L"New & location"/L"PulseTrace.exe";
        const auto custom=directory/L"Other data";
        check(!read_startup_run(fixture.key).registered,"absent Run entry");
        set_startup_run(true,old,old.parent_path()/L"data",fixture.key);
        check(read_startup_run(fixture.key).registered&&!read_startup_run(fixture.key).blocked,"Run entry enabled");
        fixture.approval(3);
        check(read_startup_run(fixture.key).blocked,"odd approval byte detected");
        fixture.approval(7);
        check(read_startup_run(fixture.key).blocked,"alternate odd approval byte detected");
        check(calibrate_startup_run(next,fixture.key)&&read_startup_run(fixture.key).blocked,"path repair preserves blocked approval");
        check(fixture.command()==quote_windows_argument(next.wstring())+L" --background","new portable path follows default data");
        set_startup_run(true,old,custom,fixture.key);
        check(!read_startup_run(fixture.key).blocked,"explicit re-enable clears Task Manager block");
        check(calibrate_startup_run(next,fixture.key)&&fixture.command().ends_with(L"--data-dir "+quote_windows_argument(custom.wstring())),
              "current signed-in user startup keeps custom data path");
        check(!calibrate_startup_run(next,fixture.key),"unchanged executable causes no registry write");
        std::filesystem::create_directories(old.parent_path());
        std::ofstream(old).put('\0');
        set_startup_run(true,old,{},fixture.key);
        check(!calibrate_startup_run(next,fixture.key),"launching a second existing copy preserves the registered one");
        set_startup_run(false,next,{},fixture.key);
        check(!read_startup_run(fixture.key).registered,"disabling removes the Run entry");
        const auto com=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
        check(SUCCEEDED(com),"COM initialize");
        try{validate_task(next,custom);}catch(...){CoUninitialize();throw;}
        CoUninitialize();
        std::cout<<"Startup approval, portable path calibration and Windows logon task definition passed\n";
        return 0;
    }catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}
}
