#pragma once
#include "pulsetrace/cloud_backup.hpp"
#include <windows.h>
#include <fstream>
#include <regex>
#include <stdexcept>
namespace pulsetrace::cloud_detail {
constexpr size_t max_bytes=128*1024*1024;
std::string identity();
inline std::wstring wide(const std::string &s){if(s.empty())return {};int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),nullptr,0);if(!n)throw std::runtime_error("CLOUD_INVALID_TEXT");std::wstring out(n,0);MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),out.data(),n);return out;}
inline CloudBytes read(const std::filesystem::path &p,size_t limit=max_bytes){std::ifstream in(p,std::ios::binary|std::ios::ate);if(!in||in.tellg()<0||static_cast<size_t>(in.tellg())>limit)throw std::runtime_error("CLOUD_FILE_SIZE_OR_READ_FAILED");CloudBytes bytes(static_cast<size_t>(in.tellg()));in.seekg(0);if(!in.read(reinterpret_cast<char*>(bytes.data()),static_cast<std::streamsize>(bytes.size())))throw std::runtime_error("CLOUD_FILE_READ_FAILED");return bytes;}
inline void write(const std::filesystem::path &p,std::span<const unsigned char> bytes,bool replace=false){std::filesystem::create_directories(p.parent_path());const auto temp=p.wstring()+L".tmp-"+wide(identity());HANDLE h=CreateFileW(temp.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);if(h==INVALID_HANDLE_VALUE)throw std::runtime_error("CLOUD_FILE_WRITE_FAILED");DWORD written{};const bool ok=WriteFile(h,bytes.data(),static_cast<DWORD>(bytes.size()),&written,nullptr)&&written==bytes.size()&&FlushFileBuffers(h);CloseHandle(h);if(!ok||!MoveFileExW(temp.c_str(),p.c_str(),MOVEFILE_WRITE_THROUGH|(replace?MOVEFILE_REPLACE_EXISTING:0))){DeleteFileW(temp.c_str());throw std::runtime_error("CLOUD_FILE_WRITE_FAILED");}}
inline void json_write(const std::filesystem::path &p,const Json &j){const auto text=j.dump(2);write(p,{reinterpret_cast<const unsigned char*>(text.data()),text.size()},true);}
inline Json json_read(const std::filesystem::path &p){return Json::parse(read(p,2*1024*1024));}
CloudBytes protect(std::span<const unsigned char>);
CloudBytes unprotect(std::span<const unsigned char>);
std::string identity();
bool safe_file(const std::string &);
void validate_database(const std::filesystem::path &,bool main);
inline void discard_restore_stage(const std::filesystem::path &root,const Json &intent)noexcept{try{const auto token=intent.value("token",std::string());if(!std::regex_match(token,std::regex("[a-f0-9]{32}")))return;const auto path=root/("restore-"+token);if(std::filesystem::exists(path)&&std::filesystem::weakly_canonical(path).parent_path()==std::filesystem::weakly_canonical(root)){std::error_code error;std::filesystem::remove_all(path,error);}}catch(...) {}}
}
