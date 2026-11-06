#include "pulsetrace/campus.hpp"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <set>
#include <stdexcept>
#include <windows.h>
namespace pulsetrace {
namespace {
std::string lower(std::string value){
    std::transform(value.begin(),value.end(),value.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
    return value;
}
bool http_url(const std::string &value){
    const auto prefix=lower(value.substr(0,8));
    return prefix.starts_with("http://")||prefix.starts_with("https://");
}
std::filesystem::path rule_path(const std::filesystem::path &data){return data/"campus"/"rules.json";}
}
std::string normalize_campus_portal_url(const std::string &value){
    if(value.empty())return {};
    if(value.size()>2048||!http_url(value)||value.find('@')!=std::string::npos||
       value.find('?')!=std::string::npos||value.find('#')!=std::string::npos)
        throw std::invalid_argument("Portal URL must be an http(s) page URL without credentials, query or fragment");
    if(value.find_first_of(" \t\r\n\\")!=std::string::npos)
        throw std::invalid_argument("Portal URL contains invalid characters");
    const auto authority=value.find("://")+3,slash=value.find('/',authority);
    const auto host_end=slash==std::string::npos?value.size():slash;
    if(authority>=value.size()||host_end==authority)
        throw std::invalid_argument("Portal URL needs a host");
    return value;
}
std::string campus_origin(const std::string &url){
    if(url.empty())return {};
    const auto clean=normalize_campus_portal_url(url);
    const auto start=clean.find("://")+3,end=clean.find('/',start);
    return lower(clean.substr(0,end==std::string::npos?clean.size():end));
}
Json load_campus_rules(const std::filesystem::path &data){
    Json rules={{"format",1},{"portals",Json::array()}};
    const auto file=rule_path(data);
    if(!std::filesystem::exists(file))return rules;
    std::ifstream input(file);input>>rules;
    if(!rules.is_object()||rules.value("format",0)!=1||!rules.contains("portals")||!rules["portals"].is_array()||rules["portals"].size()>256)
        throw std::runtime_error("Campus mapping rules are invalid; original file preserved");
    for(const auto &rule:rules["portals"]){
        if(!rule.is_object()||!rule.contains("origin")||!rule["origin"].is_string()||
           !rule.contains("fields")||!rule["fields"].is_object())
            throw std::runtime_error("Campus mapping rule is invalid; original file preserved");
        validate_campus_mapping(rule["fields"]);
    }
    return rules;
}
void save_campus_rules(const std::filesystem::path &data,const Json &rules){
    if(!rules.is_object()||rules.value("format",0)!=1||!rules.contains("portals")||!rules["portals"].is_array())
        throw std::invalid_argument("Invalid campus rule document");
    const auto file=rule_path(data),directory=file.parent_path();std::filesystem::create_directories(directory);
    const auto temp=file.string()+".tmp";std::ofstream output(temp,std::ios::binary|std::ios::trunc);
    output<<rules.dump(2);output.close();
    if(!output)throw std::runtime_error("Cannot write campus mapping rules");
    const auto temporary=std::filesystem::path(temp).wstring(),destination=file.wstring();
    if(!MoveFileExW(temporary.c_str(),destination.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error("Cannot publish campus mapping rules");
}
Json campus_rule_for(const Json &rules,const std::string &origin){
    if(origin.empty()||!rules.contains("portals"))return nullptr;
    for(const auto &rule:rules["portals"])if(lower(rule.value("origin",""))==lower(origin))return rule;
    return nullptr;
}
void validate_campus_mapping(const Json &fields){
    if(!fields.is_object()||fields.empty())throw std::invalid_argument("Campus mapping must not be empty");
    const std::set<std::string> allowed={"balance","used_bytes","remaining_bytes","quota_bytes","fee","billing_cycle"};
    for(auto it=fields.begin();it!=fields.end();++it){
        if(!allowed.contains(it.key())||!it->is_object())throw std::invalid_argument("Unknown campus field mapping");
        const auto &m=*it;
        if(!m.contains("selector")||!m["selector"].is_string()||m["selector"].get<std::string>().empty()||m["selector"].get<std::string>().size()>2048)
            throw std::invalid_argument("Invalid campus selector");
        if(!m.contains("kind")||(m["kind"]!="bytes"&&m["kind"]!="currency"&&m["kind"]!="text"))
            throw std::invalid_argument("Invalid campus field kind");
        if(!m.contains("unit")||!m["unit"].is_string()||m["unit"].get<std::string>().size()>16)
            throw std::invalid_argument("Invalid campus unit");
        if(!m.contains("confidence")||!m["confidence"].is_number()||m["confidence"].get<double>()<0||m["confidence"].get<double>()>1)
            throw std::invalid_argument("Invalid campus confidence");
    }
}
}
