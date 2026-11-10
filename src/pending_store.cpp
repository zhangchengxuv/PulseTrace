#include "pulsetrace/pending_store.hpp"
#include <windows.h>
#include <fstream>
#include <algorithm>
#include <stdexcept>
namespace pulsetrace {
std::filesystem::path PendingStore::path(const std::string &session)const{
 if(session.empty()||session.size()>100||session.find_first_not_of("0123456789-")!=std::string::npos)throw std::runtime_error("Invalid pending session identity");
 return directory_/(session+".pending");
}
void PendingStore::stage(const Json &batch)const{
 const auto file=path(batch.at("session"));auto temp=file;temp+=L".tmp";
 std::filesystem::create_directories(directory_);const auto bytes=batch.dump();
 if(bytes.size()>128ULL*1024*1024)throw std::runtime_error("Pending aggregates exceed recovery limit; previous snapshot preserved");
 HANDLE handle=CreateFileW(temp.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_WRITE_THROUGH,nullptr);
 if(handle==INVALID_HANDLE_VALUE)throw std::runtime_error("Cannot stage pending aggregates; Win32="+std::to_string(GetLastError()));
 bool ok=true;DWORD failure=0;
 for(size_t offset=0;offset<bytes.size();){DWORD written=0;const auto size=static_cast<DWORD>(std::min<size_t>(bytes.size()-offset,1<<20));
  if(!WriteFile(handle,bytes.data()+offset,size,&written,nullptr)||written==0){ok=false;failure=GetLastError();break;}offset+=written;
 }
 if(ok&&!FlushFileBuffers(handle)){ok=false;failure=GetLastError();}CloseHandle(handle);
 if(!ok)throw std::runtime_error("Cannot flush pending aggregates; Win32="+std::to_string(failure));
 if(!MoveFileExW(temp.c_str(),file.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("Cannot publish pending aggregates; Win32="+std::to_string(GetLastError()));
}
std::vector<std::filesystem::path> PendingStore::files()const{
 std::vector<std::filesystem::path> result;if(!std::filesystem::exists(directory_))return result;
 for(const auto &entry:std::filesystem::directory_iterator(directory_))if(entry.is_regular_file()&&entry.path().extension()==".pending")result.push_back(entry.path());
 std::sort(result.begin(),result.end());return result;
}
Json PendingStore::read(const std::filesystem::path &file)const{
 if(std::filesystem::file_size(file)>128ULL*1024*1024)throw std::runtime_error("Pending aggregate file exceeds recovery limit; preserved");
 std::ifstream input(file,std::ios::binary);Json batch;input>>batch;
 if(batch.at("format")!=1||batch.at("revision").get<std::int64_t>()<1||path(batch.at("session"))!=file)throw std::runtime_error("Invalid pending aggregate identity; file preserved");
 for(const char *key:{"rows","events","usage","rankings"})if(!batch.at(key).is_array())throw std::runtime_error("Invalid pending aggregates; file preserved");
 if(batch.contains("music")&&!batch.at("music").is_array())throw std::runtime_error("Invalid pending music aggregates; file preserved");
 return batch;
}
void PendingStore::acknowledge(const std::string &session)const{
 const auto file=path(session);if(!DeleteFileW(file.c_str())&&GetLastError()!=ERROR_FILE_NOT_FOUND)throw std::runtime_error("Cannot acknowledge committed pending aggregates; Win32="+std::to_string(GetLastError()));
}
}
