#include <windows.h>
#include <sddl.h>
#include <aclapi.h>
#include <filesystem>
#include <fstream>
#include <vector>
#include <stdexcept>
namespace pulsetrace {
void check_install_path(const std::filesystem::path &directory){const auto path=std::filesystem::absolute(directory).lexically_normal();if(path==path.root_path()||path.filename()==L"Program Files"||path.filename()==L"Windows")throw std::runtime_error("Choose a dedicated PulseTrace directory");DWORD flags=0;const auto drive=path.root_path();if(!GetVolumeInformationW(drive.c_str(),nullptr,0,nullptr,nullptr,&flags,nullptr,0)||(flags&FILE_PERSISTENT_ACLS)==0)throw std::runtime_error("Installation requires an available local filesystem with persistent ACL support");
 if(std::filesystem::exists(path/L"PulseTrace.exe")){auto h=CreateFileW(path.c_str(),WRITE_DAC,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS,nullptr);if(h==INVALID_HANDLE_VALUE)throw std::runtime_error("Cannot update installation permissions; choose another directory or explicitly run the installer with the required rights");CloseHandle(h);return;}
 if(std::filesystem::exists(path))for(const auto &entry:std::filesystem::directory_iterator(path))if(entry.path().filename()!=L"data")throw std::runtime_error("Choose an empty application directory or an existing PulseTrace installation");
 std::filesystem::create_directories(path);const auto probe=path/(L".pulsetrace-write-test-"+std::to_wstring(GetCurrentProcessId()));auto h=CreateFileW(probe.c_str(),GENERIC_WRITE|DELETE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_TEMPORARY|FILE_FLAG_DELETE_ON_CLOSE,nullptr);if(h==INVALID_HANDLE_VALUE)throw std::runtime_error("Installation directory is not writable; choose another location or explicitly authorize an elevated installation");CloseHandle(h);
}
std::wstring current_user_sid(){HANDLE token{};if(!OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token))throw std::runtime_error("Cannot identify installation user");DWORD size=0;GetTokenInformation(token,TokenUser,nullptr,0,&size);std::vector<BYTE> bytes(size);const bool ok=GetTokenInformation(token,TokenUser,bytes.data(),size,&size)!=0;CloseHandle(token);if(!ok)throw std::runtime_error("Cannot read user identity");LPWSTR text{};if(!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(bytes.data())->User.Sid,&text))throw std::runtime_error("Invalid user identity");std::wstring sid=text;LocalFree(text);return sid;}
void set_install_permissions(const std::filesystem::path &app,const std::wstring &sid,bool unlock){
 PSID parsed{};if(!ConvertStringSidToSidW(sid.c_str(),&parsed))throw std::runtime_error("Invalid original-user SID");LocalFree(parsed);
 const auto root=std::filesystem::weakly_canonical(std::filesystem::absolute(app));if(root==root.root_path()||!std::filesystem::exists(root/L"PulseTrace.exe"))throw std::runtime_error("Permissions can only be set on a PulseTrace installation");
 const auto program_acl=L"D:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)(A;OICI;"+std::wstring(unlock?L"0x1301bf":L"GRGX")+L";;;"+sid+L")";
 const auto data_acl=L"D:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)(A;OICI;0x1301bf;;;"+sid+L")";
 auto apply=[](const std::filesystem::path &path,const std::wstring &sddl){if(GetFileAttributesW(path.c_str())&FILE_ATTRIBUTE_REPARSE_POINT)throw std::runtime_error("Refusing to change permissions through a reparse point");PSECURITY_DESCRIPTOR descriptor{};if(!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(),SDDL_REVISION_1,&descriptor,nullptr))throw std::runtime_error("Cannot build installation ACL");PACL acl{};BOOL present=FALSE,def=FALSE;GetSecurityDescriptorDacl(descriptor,&present,&acl,&def);const auto rc=SetNamedSecurityInfoW(const_cast<LPWSTR>(path.c_str()),SE_FILE_OBJECT,DACL_SECURITY_INFORMATION|PROTECTED_DACL_SECURITY_INFORMATION,nullptr,nullptr,acl,nullptr);LocalFree(descriptor);if(rc!=ERROR_SUCCESS)throw std::runtime_error("Cannot secure installation directory; choose a writable location or run the installer with the required permissions. Win32 "+std::to_string(rc));};
 // Children first: the program directory is sealed only after all updates are complete.
 for(auto it=std::filesystem::recursive_directory_iterator(root);it!=std::filesystem::recursive_directory_iterator();++it){const auto relative=it->path().lexically_relative(root);if(*relative.begin()==L"data"){if(it->is_directory())it.disable_recursion_pending();continue;}const auto filename=it->path().filename().wstring();const bool uninstall_record=filename.starts_with(L"unins")&&it->path().extension()==L".dat";apply(it->path(),uninstall_record?data_acl:program_acl);}
 apply(root,program_acl);const auto data=root/L"data";if(std::filesystem::exists(data))apply(data,data_acl);
}
}
