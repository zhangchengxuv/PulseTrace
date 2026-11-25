#pragma once
#include <filesystem>
#include <string>
#include <string_view>
#include <windows.h>

namespace pulsetrace {
struct StartupRunState {
    bool registered{}, blocked{};
};
struct StartupTaskState {
    bool available{}, registered{}, enabled{};
};

StartupRunState read_startup_run(HKEY root=HKEY_CURRENT_USER);
void set_startup_run(bool enabled,const std::filesystem::path &exe,const std::filesystem::path &data,
                     HKEY root=HKEY_CURRENT_USER);
bool calibrate_startup_run(const std::filesystem::path &exe,HKEY root=HKEY_CURRENT_USER);
StartupTaskState read_startup_task() noexcept;
StartupTaskState set_startup_mode(std::string_view mode,const std::filesystem::path &exe,const std::filesystem::path &data);
std::wstring startup_task_xml(const std::wstring &sid,const std::filesystem::path &exe,
                              const std::filesystem::path &data);
void remove_startup_task_for_installation(const std::wstring &sid,const std::filesystem::path &exe);
void write_startup_error(const char *stage,unsigned long error=0) noexcept;
}
