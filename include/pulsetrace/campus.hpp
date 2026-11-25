#pragma once
#include "pulsetrace/json.hpp"
#include <filesystem>
#include <string>
#include <array>
#include <string_view>
namespace pulsetrace {
inline constexpr std::array<std::string_view,11> campus_field_names={"balance","free_quota_bytes","free_remaining_bytes","used_free_bytes",
    "spendable_balance","paid_used_bytes","used_bytes","remaining_bytes","quota_bytes","fee","billing_cycle"};
std::string normalize_campus_portal_url(const std::string &value);
std::string campus_origin(const std::string &url);
Json load_campus_rules(const std::filesystem::path &data);
void save_campus_rules(const std::filesystem::path &data,const Json &rules);
Json campus_rule_for(const Json &rules,const std::string &origin);
void validate_campus_mapping(const Json &mapping);
void normalize_campus_mapping(Json &mapping);
double campus_unit_factor(const std::string &unit,int radix);
Json campus_measurement(const Json &snapshot);
struct CampusThresholds {
    bool low_free{}, low_balance{};
    std::string cycle;
    Json flags() const;
};
void validate_campus_alert_settings(const Json &settings);
CampusThresholds campus_thresholds(const Json &config, const Json &rule, const Json &snapshot,
                                  std::int64_t now_ms);
Json campus_due_alerts(const CampusThresholds &thresholds, Json &receipts, std::int64_t now_ms);
}
