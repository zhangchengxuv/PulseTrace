#pragma once
#include "pulsetrace/database.hpp"
#include <windows.h>
#include <set>
namespace pulsetrace {
// Independent network-only archives survive the rolling detail retention policy.
class MonthlyTrafficStore {
  public:
    explicit MonthlyTrafficStore(std::filesystem::path directory);
    void save(const std::string &session, const Json &rows);
    void recover(const DatabaseManager &source);
    Json query(const std::string &month = "") const;
    void clear();
    std::string month_at(std::int64_t utc_minute) const;
  private:
    void publish(const std::string &month) const;
    std::filesystem::path directory_;
    DYNAMIC_TIME_ZONE_INFORMATION zone_{};
    std::string zone_key_;
};
} // namespace pulsetrace
