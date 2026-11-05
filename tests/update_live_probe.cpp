#include "pulsetrace/update.hpp"
#include <windows.h>
#include <fstream>
#include <iostream>
using namespace pulsetrace;
// Explicit developer tool. Checks only public release metadata, never downloads an installer.
int main(){const auto dir=std::filesystem::absolute("data/tests/live-update-"+std::to_string(GetTickCount64()));UpdateService service(dir);service.check("preview","auto");const auto end=GetTickCount64()+60000;while(service.status().value("busy",false)&&GetTickCount64()<end)Sleep(50);if(service.status().value("busy",false))service.cancel();const auto result=service.status();std::filesystem::create_directories(dir);std::ofstream report(dir/"live-result.json");report<<result.dump(2);std::cout<<result.dump(2)<<'\n';return result.value("busy",false)?1:0;}
