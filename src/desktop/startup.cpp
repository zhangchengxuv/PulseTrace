#include "pulsetrace/desktop/startup.hpp"
#include "pulsetrace/protocol.hpp"
#include <objbase.h>
#include <oleauto.h>
#include <shellapi.h>
#include <taskschd.h>
#include <cstdio>
#include <memory>
#include <string_view>
#include <system_error>
#include <vector>

namespace pulsetrace {
std::wstring current_user_sid();
namespace {
constexpr wchar_t run_key[]=L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t approved_key[]=L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\Run";
constexpr wchar_t value_name[]=L"PulseTrace";

bool missing(LSTATUS result){return result==ERROR_FILE_NOT_FOUND||result==ERROR_PATH_NOT_FOUND;}
void registry_check(LSTATUS result){
    if(result!=ERROR_SUCCESS)
        throw std::system_error(static_cast<int>(result),std::system_category(),"PulseTrace startup registry");
}
void task_check(HRESULT result){
    if(FAILED(result))
        throw std::system_error(result,std::system_category(),"PulseTrace Task Scheduler");
}
bool same_path(const std::filesystem::path &a,const std::filesystem::path &b){
    return _wcsicmp(a.lexically_normal().c_str(),b.lexically_normal().c_str())==0;
}
std::wstring startup_args(const std::filesystem::path &exe,const std::filesystem::path &data){
    if(data.empty()||same_path(data,exe.parent_path()/L"data"))return L"--background";
    return L"--background --data-dir "+quote_windows_argument(data.wstring());
}
std::wstring run_command(const std::filesystem::path &exe,const std::filesystem::path &data){
    return quote_windows_argument(exe.wstring())+L" "+startup_args(exe,data);
}
void delete_value(HKEY root,const wchar_t *key){
    const auto result=RegDeleteKeyValueW(root,key,value_name);
    if(!missing(result))registry_check(result);
}
void write_run(HKEY root,const std::wstring &text){
    HKEY key{};
    registry_check(RegCreateKeyExW(root,run_key,0,nullptr,0,KEY_SET_VALUE,nullptr,&key,nullptr));
    const auto result=RegSetValueExW(key,value_name,0,REG_SZ,reinterpret_cast<const BYTE *>(text.c_str()),
                                    static_cast<DWORD>((text.size()+1)*sizeof(wchar_t)));
    RegCloseKey(key);
    registry_check(result);
}
std::wstring read_run(HKEY root){
    constexpr DWORD flags=RRF_RT_REG_SZ|RRF_RT_REG_EXPAND_SZ|RRF_NOEXPAND;
    DWORD size{};
    if(RegGetValueW(root,run_key,value_name,flags,nullptr,nullptr,&size)!=ERROR_SUCCESS||size>65536)
        return {};
    std::wstring command(size/sizeof(wchar_t),L'\0');
    if(RegGetValueW(root,run_key,value_name,flags,nullptr,command.data(),&size)!=ERROR_SUCCESS)
        return {};
    command.resize(wcslen(command.c_str()));
    return command;
}
std::wstring xml_text(std::wstring_view value){
    std::wstring escaped;
    for(const auto c:value){
        if(c==L'&')escaped+=L"&amp;";
        else if(c==L'<')escaped+=L"&lt;";
        else if(c==L'>')escaped+=L"&gt;";
        else escaped+=c;
    }
    return escaped;
}
class BStr {
  public:
    explicit BStr(const std::wstring &value):value_(SysAllocString(value.c_str())) {
        if(!value_)throw std::bad_alloc();
    }
    ~BStr(){SysFreeString(value_);}
    BStr(const BStr &)=delete;
    operator BSTR()const{return value_;}
  private:
    BSTR value_;
};
struct ReleaseCom {
    void operator()(IUnknown *item)const noexcept{item->Release();}
};
template<typename T,typename F> std::unique_ptr<T,ReleaseCom> task_object(F get){
    T *item{};
    const auto result=get(&item);
    std::unique_ptr<T,ReleaseCom> owner(item);
    task_check(result);
    return owner;
}
class TaskAccess {
  public:
    explicit TaskAccess(std::wstring sid):sid_(std::move(sid)),name_(L"PulseTrace-"+sid_){
        auto service=task_object<ITaskService>([](auto **out){
            return CoCreateInstance(CLSID_TaskScheduler,nullptr,CLSCTX_INPROC_SERVER,IID_ITaskService,
                                    reinterpret_cast<void **>(out));
        });
        task_check(service->Connect({}, {}, {}, {}));
        folder_=task_object<ITaskFolder>([&](auto **out){return service->GetFolder(BStr(L"\\"),out);});
        IRegisteredTask *existing{};
        const auto result=folder_->GetTask(name_,&existing);
        existing_.reset(existing);
        if(result!=HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND)&&result!=HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND))
            task_check(result);
    }
    StartupTaskState status()const{
        VARIANT_BOOL enabled=VARIANT_FALSE;
        if(existing_)task_check(existing_->get_Enabled(&enabled));
        return {true,static_cast<bool>(existing_),enabled==VARIANT_TRUE};
    }
    bool owns(const std::filesystem::path &exact={})const{
        if(!existing_)return false;
        auto definition=task_object<ITaskDefinition>([&](auto **out){return existing_->get_Definition(out);});
        auto actions=task_object<IActionCollection>([&](auto **out){return definition->get_Actions(out);});
        LONG count{};task_check(actions->get_Count(&count));
        if(count!=1)return false;
        auto action=task_object<IAction>([&](auto **out){return actions->get_Item(1,out);});
        TASK_ACTION_TYPE type{};
        task_check(action->get_Type(&type));
        if(type!=TASK_ACTION_EXEC)return false;
        auto command=task_object<IExecAction>([&](auto **out){return action->QueryInterface(IID_IExecAction,reinterpret_cast<void **>(out));});
        BSTR raw{};
        const auto result=command->get_Path(&raw);
        std::unique_ptr<wchar_t,decltype(&SysFreeString)> path(raw,SysFreeString);
        task_check(result);
        if(!path)return false;
        const std::filesystem::path exe(path.get());
        return exe.is_absolute()&&_wcsicmp(exe.filename().c_str(),L"PulseTrace.exe")==0&&
               (exact.empty()||same_path(exe,exact));
    }
    void remove(){
        if(!existing_)return;
        if(!owns())throw std::runtime_error("Startup task name already in use");
        task_check(folder_->DeleteTask(name_,0));
        existing_.reset();
    }
    void enable(const std::filesystem::path &exe,const std::filesystem::path &data){
        if(existing_&&!owns())throw std::runtime_error("Startup task name already in use");
        BStr user(sid_);
        VARIANT principal{};
        principal.vt=VT_BSTR;principal.bstrVal=user;
        existing_=task_object<IRegisteredTask>([&](auto **out){
            return folder_->RegisterTask(name_,BStr(startup_task_xml(sid_,exe,data)),TASK_CREATE_OR_UPDATE,
                                        principal,{},TASK_LOGON_INTERACTIVE_TOKEN,{},out);
        });
    }
  private:
    std::wstring sid_;
    BStr name_;
    std::unique_ptr<ITaskFolder,ReleaseCom> folder_;
    std::unique_ptr<IRegisteredTask,ReleaseCom> existing_;
};
}

StartupRunState read_startup_run(HKEY root){
    StartupRunState state;
    DWORD size{};
    state.registered=RegGetValueW(root,run_key,value_name,RRF_RT_REG_SZ|RRF_RT_REG_EXPAND_SZ|RRF_NOEXPAND,
                                 nullptr,nullptr,&size)==ERROR_SUCCESS;
    if(!state.registered)return state;
    size=0;
    if(RegGetValueW(root,approved_key,value_name,RRF_RT_REG_BINARY,nullptr,nullptr,&size)==ERROR_SUCCESS&&
       size>0&&size<=65536){
        std::vector<BYTE> approval(size);
        if(RegGetValueW(root,approved_key,value_name,RRF_RT_REG_BINARY,nullptr,approval.data(),&size)==ERROR_SUCCESS&&size>0)
            state.blocked=(approval[0]&1)!=0;
    }
    return state;
}
void set_startup_run(bool enabled,const std::filesystem::path &exe,const std::filesystem::path &data,HKEY root){
    if(!enabled){delete_value(root,run_key);return;}
    write_run(root,run_command(exe,data));
    delete_value(root,approved_key);
}
bool calibrate_startup_run(const std::filesystem::path &exe,HKEY root){
    const auto command=read_run(root);
    if(command.empty())return false;
    int argc{};
    std::unique_ptr<LPWSTR,decltype(&LocalFree)> argv(CommandLineToArgvW(command.c_str(),&argc),LocalFree);
    if(!argv||(argc!=2&&argc!=4)||std::wstring_view(argv.get()[1])!=L"--background"||
       (argc==4&&std::wstring_view(argv.get()[2])!=L"--data-dir"))
        return false;
    const std::filesystem::path old(argv.get()[0]);
    if(!old.is_absolute()||_wcsicmp(old.filename().c_str(),L"PulseTrace.exe")!=0||same_path(exe,old))
        return false;
    std::error_code ec;
    if(std::filesystem::exists(old,ec)||ec)return false;
    std::filesystem::path data;
    if(argc==4){
        data=argv.get()[3];
        if(!data.is_absolute())return false;
        if(same_path(data,old.parent_path()/L"data"))data.clear();
    }
    write_run(root,run_command(exe,data));
    return true;
}
std::wstring startup_task_xml(const std::wstring &sid,const std::filesystem::path &exe,
                              const std::filesystem::path &data){
    const auto user=xml_text(sid),path=xml_text(exe.wstring()),args=xml_text(startup_args(exe,data));
    return L"<Task version=\"1.2\" xmlns=\"http://schemas.microsoft.com/windows/2004/02/mit/task\">"
           L"<Triggers><LogonTrigger><Enabled>true</Enabled><UserId>"+user+L"</UserId></LogonTrigger></Triggers>"
           L"<Principals><Principal id=\"PulseTraceUser\"><UserId>"+user+L"</UserId>"
           L"<LogonType>InteractiveToken</LogonType><RunLevel>HighestAvailable</RunLevel></Principal></Principals>"
           L"<Settings><MultipleInstancesPolicy>IgnoreNew</MultipleInstancesPolicy>"
           L"<DisallowStartIfOnBatteries>false</DisallowStartIfOnBatteries>"
           L"<StopIfGoingOnBatteries>false</StopIfGoingOnBatteries><ExecutionTimeLimit>PT0S</ExecutionTimeLimit></Settings>"
           L"<Actions Context=\"PulseTraceUser\"><Exec><Command>"+path+L"</Command><Arguments>"+args+
           L"</Arguments></Exec></Actions></Task>";
}
StartupTaskState read_startup_task() noexcept{
    try{return TaskAccess(current_user_sid()).status();}
    catch(...){return {};}
}
StartupTaskState set_startup_mode(std::string_view mode,const std::filesystem::path &exe,const std::filesystem::path &data){
    std::unique_ptr<TaskAccess> task;
    try{task=std::make_unique<TaskAccess>(current_user_sid());}
    catch(...){if(mode=="task")throw;}
    if(mode=="task"){
        const bool existed=task->status().registered;
        task->enable(exe,data);
        try{set_startup_run(false,exe,data);}
        catch(...){if(!existed)try{task->remove();}catch(...){}throw;}
    }else{
        if(task)task->remove();
        set_startup_run(mode=="run",exe,data);
    }
    return task?task->status():StartupTaskState{};
}
void remove_startup_task_for_installation(const std::wstring &sid,const std::filesystem::path &exe){
    try{
        TaskAccess task(sid);
        if(task.owns(exe))task.remove();
    }catch(const std::system_error &e){
        if(e.code().value()!=HRESULT_FROM_WIN32(ERROR_SERVICE_NOT_ACTIVE)&&
           e.code().value()!=HRESULT_FROM_WIN32(ERROR_SERVICE_DISABLED)&&
           e.code().value()!=HRESULT_FROM_WIN32(RPC_S_SERVER_UNAVAILABLE)&&
           e.code().value()!=SCHED_E_SERVICE_NOT_RUNNING)throw;
    }
}
void write_startup_error(const char *stage,unsigned long error) noexcept{
    try{
        wchar_t temp[MAX_PATH+1]{};
        const auto count=GetTempPathW(MAX_PATH+1,temp);
        if(!count||count>=MAX_PATH+1)return;
        const auto path=std::filesystem::path(temp)/L"PulseTrace-startup-error.log";
        GUID id{};
        wchar_t temporary_name[40]{};
        if(FAILED(CoCreateGuid(&id))||!StringFromGUID2(id,temporary_name,40))return;
        const auto temporary=path.parent_path()/(std::wstring(temporary_name)+L".tmp");
        SYSTEMTIME utc{};GetSystemTime(&utc);
        char time[40]{};
        std::snprintf(time,sizeof(time),"%04u-%02u-%02uT%02u:%02u:%02uZ",
                      unsigned(utc.wYear),unsigned(utc.wMonth),unsigned(utc.wDay),
                      unsigned(utc.wHour),unsigned(utc.wMinute),unsigned(utc.wSecond));
        std::string message=std::string(time)+" PulseTrace startup failed\nStage: "+stage+"\n";
        if(error)message+="System code: "+std::to_string(error)+"\n";
        const auto file=CreateFileW(temporary.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(file==INVALID_HANDLE_VALUE)return;
        DWORD written{};
        const bool saved=WriteFile(file,message.data(),static_cast<DWORD>(message.size()),&written,nullptr)&&written==message.size();
        CloseHandle(file);
        if(!saved||!MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
            DeleteFileW(temporary.c_str());
    }catch(...){}
}
}
