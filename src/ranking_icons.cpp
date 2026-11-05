#include <windows.h>
#include <objidl.h>
#include <objbase.h>
#include <shellapi.h>
#include <gdiplus.h>
#include <wincrypt.h>
#include "pulsetrace/traffic_rankings.hpp"
namespace pulsetrace {
std::string application_icon(const std::string &path){
 // Only known local executable identities, never remote paths or URLs. Extract
 // resources without executing the application. Icons are display-only caches.
 const auto id=ranking_identity(path);if(!id.key.starts_with("exe:")||id.path.size()<4)return {};
 auto extension=id.path.substr(id.path.size()-4);for(auto &c:extension)c=static_cast<char>(tolower(static_cast<unsigned char>(c)));if(extension!=".exe")return {};
 const auto u8=std::u8string(id.path.begin(),id.path.end());const auto local=std::filesystem::path(u8);
 if(GetDriveTypeW(local.root_path().c_str())==DRIVE_REMOTE)return {};
 HICON icon{};if(!ExtractIconExW(local.c_str(),0,nullptr,&icon,1)||!icon)return {};
 ULONG_PTR token{};Gdiplus::GdiplusStartupInput input;if(Gdiplus::GdiplusStartup(&token,&input,nullptr)!=Gdiplus::Ok){DestroyIcon(icon);return {};}
 std::string result;
 {Gdiplus::Bitmap bitmap(icon);IStream *stream{};if(CreateStreamOnHGlobal(nullptr,TRUE,&stream)==S_OK){CLSID png{0x557cf406,0x1a04,0x11d3,{0x9a,0x73,0,0,0xf8,0x1e,0xf3,0x2e}};
   if(bitmap.Save(stream,&png,nullptr)==Gdiplus::Ok){HGLOBAL memory{};if(GetHGlobalFromStream(stream,&memory)==S_OK){STATSTG stat{};if(stream->Stat(&stat,STATFLAG_NONAME)==S_OK&&stat.cbSize.QuadPart<65536){auto bytes=static_cast<BYTE*>(GlobalLock(memory));DWORD n{};if(bytes&&CryptBinaryToStringA(bytes,stat.cbSize.LowPart,CRYPT_STRING_BASE64|CRYPT_STRING_NOCRLF,nullptr,&n)){std::string b64(n,'\0');if(CryptBinaryToStringA(bytes,stat.cbSize.LowPart,CRYPT_STRING_BASE64|CRYPT_STRING_NOCRLF,b64.data(),&n)){b64.resize(n);result="data:image/png;base64,"+b64;}}if(bytes)GlobalUnlock(memory);}}}stream->Release();}}
 Gdiplus::GdiplusShutdown(token);DestroyIcon(icon);return result;
}
}
