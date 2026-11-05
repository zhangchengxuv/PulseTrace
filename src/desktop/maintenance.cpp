#include "pulsetrace/database.hpp"
#include "pulsetrace/core_service.hpp"
#include "pulsetrace/installation.hpp"
#include <shellapi.h>
#include <objbase.h>
#include <restartmanager.h>
#include <fstream>
#include <iostream>
#include <set>
#include <vector>
using namespace pulsetrace;
namespace pulsetrace {std::wstring current_user_sid();void set_install_permissions(const std::filesystem::path&,const std::wstring&,bool);void check_install_path(const std::filesystem::path&);}
namespace {
std::wstring canonical_path(const std::filesystem::path &p){return std::filesystem::weakly_canonical(std::filesystem::absolute(p)).wstring();}
struct CloseRequest{std::wstring target;std::vector<HANDLE> handles;};
BOOL CALLBACK close_window(HWND w,LPARAM value){auto &r=*reinterpret_cast<CloseRequest*>(value);wchar_t cls[128]{};GetClassNameW(w,cls,128);if(wcscmp(cls,L"PulseTrace.Desktop"))return TRUE;DWORD pid{};GetWindowThreadProcessId(w,&pid);HANDLE p=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,pid);if(!p)return TRUE;wchar_t file[32768]{};DWORD size=32768;bool match=false;try{match=QueryFullProcessImageNameW(p,0,file,&size)&&_wcsicmp(canonical_path(file).c_str(),r.target.c_str())==0;}catch(...){}if(match){PostMessageW(w,maintenance_message(),0,0);r.handles.push_back(p);}else CloseHandle(p);return TRUE;}
void close_application(const std::filesystem::path &exe){CloseRequest request{canonical_path(exe),{}};EnumWindows(close_window,reinterpret_cast<LPARAM>(&request));bool done=true;const auto end=GetTickCount64()+60000;for(auto p:request.handles){const auto now=GetTickCount64();if(WaitForSingleObject(p,now<end?static_cast<DWORD>(end-now):0)!=WAIT_OBJECT_0)done=false;CloseHandle(p);}if(!done)throw std::runtime_error("PulseTrace is still running. Exit from its tray menu, resolve save errors, and retry. No process was forcibly terminated.");}
void copy_data(const std::filesystem::path &source,const std::filesystem::path &destination,bool importing){
 if(!std::filesystem::is_directory(source))throw std::runtime_error("Source data directory not found");const auto s=canonical_path(source),d=canonical_path(destination);const bool safe_internal_backup=!importing&&d.starts_with(s+L"\\backups\\");if(_wcsicmp(s.c_str(),d.c_str())==0||(d.starts_with(s+L"\\")&&!safe_internal_backup)||s.starts_with(d+L"\\"))throw std::runtime_error("Source and destination must be separate directories");
 if(std::filesystem::exists(destination)&&!std::filesystem::is_empty(destination))throw std::runtime_error("Destination is not empty; existing data was not overwritten");
 std::filesystem::create_directories(destination);std::vector<std::pair<std::filesystem::path,std::filesystem::path>> files;
 for(auto iterator=std::filesystem::recursive_directory_iterator(source);iterator!=std::filesystem::recursive_directory_iterator();++iterator){const auto &entry=*iterator;
  const auto relative=entry.path().lexically_relative(source);const auto first=*relative.begin();if(first==L"backups"||first==L"webview2"||first==L"logs"||first==L"updates"){if(entry.is_directory())iterator.disable_recursion_pending();continue;}
  if(entry.is_symlink())throw std::runtime_error("Data migration refuses symbolic links");if(!entry.is_regular_file())continue;const auto extension=entry.path().extension();if(extension==L".db"||extension==L".sqlite3"||extension==L".json")files.emplace_back(entry.path(),destination/relative);
 }
 if(importing&&!std::filesystem::exists(source/L"monitor.db"))throw std::runtime_error("Portable source must contain monitor.db");
 try{for(const auto &[from,to]:files){std::filesystem::create_directories(to.parent_path());if(from.extension()==L".db"||from.extension()==L".sqlite3")DatabaseManager::backup_file(from,to);else if(!std::filesystem::copy_file(from,to,std::filesystem::copy_options::none))throw std::runtime_error("Cannot copy configuration/archive");}
  // Opening a copied database performs migrations on the copy, never the source.
  if(importing){DatabaseManager verify(destination/L"monitor.db");CoreService verify_config(destination);}
  std::ofstream receipt(destination/L"migration-receipt.json");const auto name=source.u8string();receipt<<Json{{"source",std::string(name.begin(),name.end())},{"source_preserved",true},{"sqlite_copy","online_backup"},{"file_count",files.size()}}.dump(2);if(!receipt)throw std::runtime_error("Cannot write migration receipt");
 }catch(...){std::ofstream failure(destination/L"migration-failed.txt");failure<<"Import/backup was incomplete. Original source is untouched. Use a new empty destination to retry.\n";throw;}
}
void quiesce_data(const std::filesystem::path &data){const auto database=data/L"monitor.db";if(!std::filesystem::exists(database))return;DWORD session=0;wchar_t key[CCH_RM_SESSION_KEY+1]{};if(RmStartSession(&session,0,key)!=ERROR_SUCCESS)throw std::runtime_error("Cannot inspect data owners");try{const wchar_t *files[]={database.c_str()};if(RmRegisterResources(session,1,files,0,nullptr,0,nullptr)!=ERROR_SUCCESS)throw std::runtime_error("Cannot inspect database handles");UINT needed=0,count=0;DWORD reason=0;auto rc=RmGetList(session,&needed,&count,nullptr,&reason);if(rc==ERROR_MORE_DATA){std::vector<RM_PROCESS_INFO> processes(needed);count=needed;if(RmGetList(session,&needed,&count,processes.data(),&reason)!=ERROR_SUCCESS)throw std::runtime_error("Database owner list changed; retry");for(UINT i=0;i<count;i++){auto handle=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,processes[i].Process.dwProcessId);wchar_t exe[32768]{};DWORD size=32768;const bool known=handle&&QueryFullProcessImageNameW(handle,0,exe,&size);if(handle)CloseHandle(handle);if(!known||_wcsicmp(std::filesystem::path(exe).filename().c_str(),L"PulseTrace.exe"))throw std::runtime_error("Database is in use by another process. Close it before migration.");close_application(exe);}}else if(rc!=ERROR_SUCCESS)throw std::runtime_error("Cannot determine whether data is in use");RmEndSession(session);}catch(...){RmEndSession(session);throw;}}
void update_existing_autostart(const std::filesystem::path &app,const std::wstring &sid,bool remove){
 const auto path=canonical_path(app/L"PulseTrace.exe");if(!std::filesystem::exists(path))throw std::runtime_error("Not a PulseTrace installation");
 const auto subkey=sid+L"\\Software\\Microsoft\\Windows\\CurrentVersion\\Run";HKEY key{};auto rc=RegOpenKeyExW(HKEY_USERS,subkey.c_str(),0,KEY_QUERY_VALUE|KEY_SET_VALUE,&key);if(rc==ERROR_FILE_NOT_FOUND)return;if(rc!=ERROR_SUCCESS)throw std::runtime_error("Cannot access original user's existing startup preference");
 wchar_t text[32768]{};DWORD bytes=sizeof(text),type=0;rc=RegQueryValueExW(key,L"PulseTrace",nullptr,&type,reinterpret_cast<BYTE*>(text),&bytes);
 text[32767]=0;if(rc==ERROR_FILE_NOT_FOUND){RegCloseKey(key);return;}if(rc!=ERROR_SUCCESS||type!=REG_SZ){RegCloseKey(key);return;}
 int count=0;auto args=CommandLineToArgvW(text,&count);const bool owned=args&&count>0&&_wcsicmp(canonical_path(args[0]).c_str(),path.c_str())==0;if(args)LocalFree(args);
 if(owned){const auto command=L"\""+path+L"\" --background";rc=remove?RegDeleteValueW(key,L"PulseTrace"):RegSetValueExW(key,L"PulseTrace",0,REG_SZ,reinterpret_cast<const BYTE*>(command.c_str()),static_cast<DWORD>((command.size()+1)*sizeof(wchar_t)));}RegCloseKey(key);if(owned&&rc!=ERROR_SUCCESS)throw std::runtime_error("Cannot preserve the existing startup preference");
}
}
int wmain(int argc,wchar_t **argv){try{
 if(argc==3&&std::wstring(argv[1])==L"--write-user-sid"){std::wofstream file{std::filesystem::path(argv[2])};file<<current_user_sid();if(!file)throw std::runtime_error("Cannot save original-user identity");return 0;}
 if(argc==3&&std::wstring(argv[1])==L"--check-install-path"){check_install_path(argv[2]);return 0;}
 if(argc==5&&std::wstring(argv[1])==L"--permissions"){set_install_permissions(argv[2],argv[3],std::wstring(argv[4])==L"unlock");return 0;}
 if(argc==5&&std::wstring(argv[1])==L"--existing-autostart"){update_existing_autostart(argv[2],argv[3],std::wstring(argv[4])==L"remove");return 0;}
 if(argc==3&&std::wstring(argv[1])==L"--quiesce-data"){quiesce_data(argv[2]);return 0;}
 if(argc==3&&std::wstring(argv[1])==L"--close"){close_application(argv[2]);return 0;}
 if(argc==4&&(std::wstring(argv[1])==L"--backup"||std::wstring(argv[1])==L"--import")){copy_data(argv[2],argv[3],std::wstring(argv[1])==L"--import");return 0;}
 if(argc==2&&std::wstring(argv[1])==L"--runtime-check"){using Check=HRESULT(STDAPICALLTYPE*)(PCWSTR,LPWSTR*);const auto loader=std::filesystem::path(argv[0]).parent_path()/L"WebView2Loader.dll";auto dll=LoadLibraryExW(loader.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);if(!dll)return 2;auto fn=reinterpret_cast<Check>(GetProcAddress(dll,"GetAvailableCoreWebView2BrowserVersionString"));LPWSTR version{};const bool ok=fn&&SUCCEEDED(fn(nullptr,&version))&&version;if(version)CoTaskMemFree(version);FreeLibrary(dll);return ok?0:2;}
 std::cerr<<"PulseTraceMaintenance --close EXE | --backup SOURCE EMPTY_DEST | --import PORTABLE_DATA EMPTY_DEST | --runtime-check\n";return 2;
 }catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
