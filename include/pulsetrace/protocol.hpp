#pragma once
#include "pulsetrace/json.hpp"
namespace pulsetrace {
Json parse_message(const std::string &text);
void validate_request(const Json &request);
std::wstring quote_windows_argument(const std::wstring &argument);
} // namespace pulsetrace
