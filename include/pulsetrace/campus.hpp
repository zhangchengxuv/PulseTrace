#pragma once
#include "pulsetrace/json.hpp"
#include <filesystem>
#include <string>
namespace pulsetrace {
std::string normalize_campus_portal_url(const std::string &value);
std::string campus_origin(const std::string &url);
Json load_campus_rules(const std::filesystem::path &data);
void save_campus_rules(const std::filesystem::path &data,const Json &rules);
Json campus_rule_for(const Json &rules,const std::string &origin);
void validate_campus_mapping(const Json &mapping);
void normalize_campus_mapping(Json &mapping);
}
