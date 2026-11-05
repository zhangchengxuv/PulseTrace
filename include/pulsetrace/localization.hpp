#pragma once
#include <windows.h>
#include <atomic>
#include <string>
#include <string_view>
#include "pulsetrace/locales_generated.hpp"
namespace pulsetrace {
inline std::string effective_language(std::string_view preference){if(preference=="zh-CN"||preference=="en-US")return std::string(preference);return PRIMARYLANGID(GetUserDefaultUILanguage())==LANG_CHINESE?"zh-CN":"en-US";}
inline std::atomic<bool> ui_chinese{PRIMARYLANGID(GetUserDefaultUILanguage())==LANG_CHINESE};
inline void set_ui_language(std::string_view preference){ui_chinese.store(effective_language(preference)=="zh-CN");}
inline const wchar_t *ui_text(std::string_view key){for(const auto &item:native_texts)if(key==item.key)return ui_chinese.load()?item.zh:item.en;return L"PulseTrace";}
}
