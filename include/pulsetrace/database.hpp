#pragma once
#include "pulsetrace/json.hpp"
#include <filesystem>
#include <functional>
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
    Json protocol_samples(std::int64_t from, std::int64_t to, int bucket,
                          const std::string &exclude_session = "", std::int64_t exclude_from = 0) const;
    void export_csv(const std::filesystem::path &file, std::int64_t from, std::int64_t to) const;
    Json traffic_summary() const;
    void visit_traffic(const std::function<void(const std::string &, const Json &)> &visitor) const;
    static constexpr int supported_schema=3;
    void backup(const std::filesystem::path &destination) const;
    static void backup_file(const std::filesystem::path &source,const std::filesystem::path &destination);
    Json migration_info() const{return migration_;}

  private:
    struct ReadOnlyTag{};
    explicit DatabaseManager(ReadOnlyTag){}
    sqlite3 *db_{};
    Json migration_;
    void exec(const char *sql) const;
};
} // namespace pulsetrace
