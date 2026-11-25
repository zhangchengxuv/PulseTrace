#pragma once
#include "pulsetrace/json.hpp"
namespace pulsetrace {
inline constexpr std::size_t summary_png_base64_limit=1024*1024;
inline constexpr std::size_t desktop_message_limit=summary_png_base64_limit+1024;
Json parse_message(const std::string &text, Json *request_id = nullptr);
void validate_request(const Json &request);
std::wstring quote_windows_argument(const std::wstring &argument);
} // namespace pulsetrace
