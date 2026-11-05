#pragma once
#include <windows.h>
#include <filesystem>
#include <stdexcept>
#include "pulsetrace/localization.hpp"
namespace pulsetrace {
inline UINT maintenance_message(){return RegisterWindowMessageW(L"PulseTrace.GracefulUpgradeExit.v1");}
class DataDirectoryManager {
 public:
 static std::filesystem::path resolve(const std::filesystem::path &exe,const std::filesystem::path &override_path={}){
  if(!override_path.empty())return std::filesystem::absolute(override_path).lexically_normal();
  const auto expected=std::filesystem::absolute(exe/L"data").lexically_normal(),marker=exe/L"installed.ini";
  if(std::filesystem::exists(marker)){wchar_t old[32768]{};GetPrivateProfileStringW(L"PulseTrace",L"DataDirectory",L"",old,32768,marker.c_str());
   if(*old&&(!std::filesystem::path(old).is_absolute()||_wcsicmp(std::filesystem::weakly_canonical(old).c_str(),std::filesystem::weakly_canonical(expected).c_str())!=0))throw std::runtime_error("Legacy external data directory requires installer migration. Existing data is preserved.");
  }return expected;
 }
};
}
