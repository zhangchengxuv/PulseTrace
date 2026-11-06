#pragma once
#include "pulsetrace/json.hpp"
#include "pulsetrace/sqlite_error.hpp"
#include <filesystem>
#include <functional>
#include <string>
#include <vector>
struct sqlite3;
namespace pulsetrace {
class DatabaseManager {
  public:
    explicit DatabaseManager(const std::filesystem::path &file);
    ~DatabaseManager();
    DatabaseManager(const DatabaseManager &) = delete;
    void save(const std::string &session, const Json &rows, const Json &events = Json::array());
    // One atomic transaction, including a durable revision receipt for journal replay.
    bool save_batch(const Json &batch);
    Json history_bounds() const;
    Json query(std::int64_t from, std::int64_t to, int bucket, const std::string &exclude_session = "",
               std::int64_t exclude_from = 0) const;
    void prune(std::int64_t cutoff);
    void clear(const std::vector<std::string> &pending_sessions={});
    Json network_events(std::int64_t from, std::int64_t to) const;
    Json protocol_samples(std::int64_t from, std::int64_t to, int bucket,
                          const std::string &exclude_session = "", std::int64_t exclude_from = 0) const;
    void export_csv(const std::filesystem::path &file, std::int64_t from, std::int64_t to) const;
    Json traffic_summary(std::int64_t from=0,std::int64_t to=4223371680LL) const;
    void visit_traffic(const std::function<void(const std::string &, const Json &)> &visitor) const;
    void save_rankings(const std::string &session,const Json &rows);
    Json rankings(std::int64_t from,std::int64_t to,const std::string &source,const std::string &kind,
                  const std::string &exclude_session="",std::int64_t exclude_from=0) const;
    void clear_rankings(bool domains_only);
    void prune_rankings(std::int64_t cutoff);
    std::string ranking_icon_path(const std::string &source,const std::string &identity)const;
    void save_usage(const std::string &session,const Json &rows);
    Json usage(std::int64_t from,std::int64_t to,const std::string &exclude_session="",std::int64_t floor=0,int bucket=1)const;
    void prune_usage(std::int64_t cutoff);
    std::string usage_icon_path(const std::string &identity)const;
    static constexpr int supported_schema=6;
    void backup(const std::filesystem::path &destination) const;
    static void backup_file(const std::filesystem::path &source,const std::filesystem::path &destination);
    Json migration_info() const{return migration_;}

  private:
    struct ReadOnlyTag{};
    explicit DatabaseManager(ReadOnlyTag){}
    sqlite3 *db_{};
    bool batch_{};
    Json migration_;
    void exec(const char *sql) const;
};
} // namespace pulsetrace
