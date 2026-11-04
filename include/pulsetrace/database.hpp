#pragma once
#include "pulsetrace/json.hpp"
#include <filesystem>
#include <string>
struct sqlite3;
namespace pulsetrace {
class DatabaseManager {
  public:
    explicit DatabaseManager(const std::filesystem::path &file);
    ~DatabaseManager();
    DatabaseManager(const DatabaseManager &) = delete;
    void save(const std::string &session, const Json &rows, const Json &events = Json::array());
    Json query(std::int64_t from, std::int64_t to, int bucket, const std::string &exclude_session = "",
               std::int64_t exclude_from = 0) const;
    void prune(std::int64_t cutoff);
    void clear();
    Json network_events(std::int64_t from, std::int64_t to) const;
    void export_csv(const std::filesystem::path &file, std::int64_t from, std::int64_t to) const;

  private:
    sqlite3 *db_{};
    void exec(const char *sql) const;
};
} // namespace pulsetrace
