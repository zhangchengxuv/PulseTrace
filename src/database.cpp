#include "pulsetrace/database.hpp"
#include <fstream>
#include <sqlite3.h>
#include <stdexcept>
namespace pulsetrace {
namespace {
struct Statement {
    sqlite3_stmt *p{};
    Statement(sqlite3 *db, const char *sql) {
        if (sqlite3_prepare_v2(db, sql, -1, &p, nullptr) != SQLITE_OK)
            throw std::runtime_error(sqlite3_errmsg(db));
    }
    ~Statement() { sqlite3_finalize(p); }
};
void bind_text(sqlite3_stmt *p, int i, const std::string &s) {
    sqlite3_bind_text(p, i, s.c_str(), static_cast<int>(s.size()), SQLITE_TRANSIENT);
}
} // namespace
void DatabaseManager::exec(const char *sql) const {
    char *error{};
    if (sqlite3_exec(db_, sql, nullptr, nullptr, &error) != SQLITE_OK) {
        std::string message = error ? error : "SQLite error";
        sqlite3_free(error);
        throw std::runtime_error(message);
    }
}
DatabaseManager::DatabaseManager(const std::filesystem::path &file) {
    const auto utf8 = file.u8string();
    if (sqlite3_open_v2(reinterpret_cast<const char *>(utf8.c_str()), &db_,
                        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
                        nullptr) != SQLITE_OK) {
        std::string e = db_ ? sqlite3_errmsg(db_) : "SQLite open failed";
        if (db_)
            sqlite3_close(db_);
        db_ = nullptr;
        throw std::runtime_error(e);
    }
    try {
        sqlite3_busy_timeout(db_, 2000);
        exec("PRAGMA journal_mode=WAL; PRAGMA synchronous=FULL; PRAGMA wal_autocheckpoint=256;");
        Statement version(db_, "PRAGMA user_version");
        sqlite3_step(version.p);
        if (sqlite3_column_int(version.p, 0) > 2)
            throw std::runtime_error("Database created by a newer PulseTrace version");
        exec("BEGIN;CREATE TABLE IF NOT EXISTS counts(session TEXT NOT NULL,minute INTEGER NOT NULL,module "
             "TEXT NOT NULL,metric TEXT NOT NULL,value INTEGER NOT NULL,PRIMARY "
             "KEY(session,minute,module,metric)) WITHOUT ROWID;CREATE INDEX IF NOT EXISTS counts_range ON "
             "counts(minute,module);CREATE TABLE IF NOT EXISTS network_events(session TEXT NOT NULL,sequence "
             "INTEGER NOT NULL,observed_ms INTEGER NOT NULL,kind TEXT NOT NULL,interface_id TEXT NOT "
             "NULL,name TEXT NOT NULL,PRIMARY KEY(session,sequence)) WITHOUT ROWID;CREATE INDEX IF NOT "
             "EXISTS network_events_range ON network_events(observed_ms);PRAGMA user_version=2;COMMIT;");
        Statement check(db_, "PRAGMA quick_check");
        if (sqlite3_step(check.p) != SQLITE_ROW ||
            std::string(reinterpret_cast<const char *>(sqlite3_column_text(check.p, 0))) != "ok")
            throw std::runtime_error("Database integrity check failed; original file preserved");
    } catch (...) {
        sqlite3_close(db_);
        db_ = nullptr;
        throw;
    }
}
DatabaseManager::~DatabaseManager() {
    if (db_)
        sqlite3_close(db_);
}
void DatabaseManager::save(const std::string &session, const Json &rows, const Json &events) {
    exec("BEGIN IMMEDIATE");
    try {
        Statement s(db_, "INSERT INTO counts VALUES(?,?,?,?,?) ON CONFLICT(session,minute,module,metric) DO "
                         "UPDATE SET value=excluded.value");
        for (const auto &row : rows) {
            bind_text(s.p, 1, session);
            sqlite3_bind_int64(s.p, 2, row.at("minute").get<std::int64_t>());
            bind_text(s.p, 3, row.at("module").get<std::string>());
            bind_text(s.p, 4, row.at("metric").get<std::string>());
            sqlite3_bind_int64(s.p, 5, row.at("value").get<std::int64_t>());
            if (sqlite3_step(s.p) != SQLITE_DONE)
                throw std::runtime_error(sqlite3_errmsg(db_));
            sqlite3_reset(s.p);
            sqlite3_clear_bindings(s.p);
        }
        Statement event(db_, "INSERT OR IGNORE INTO network_events VALUES(?,?,?,?,?,?)");
        for (const auto &row : events) {
            bind_text(event.p, 1, session);
            sqlite3_bind_int64(event.p, 2, row.at("sequence").get<std::int64_t>());
            sqlite3_bind_int64(event.p, 3, row.at("observed_ms").get<std::int64_t>());
            bind_text(event.p, 4, row.at("kind"));
            bind_text(event.p, 5, row.at("interface_id"));
            bind_text(event.p, 6, row.at("name"));
            if (sqlite3_step(event.p) != SQLITE_DONE)
                throw std::runtime_error(sqlite3_errmsg(db_));
            sqlite3_reset(event.p);
            sqlite3_clear_bindings(event.p);
        }
        exec("COMMIT");
    } catch (...) {
        exec("ROLLBACK");
        throw;
    }
}
Json DatabaseManager::query(std::int64_t from, std::int64_t to, int bucket,
                            const std::string &exclude_session, std::int64_t exclude_from) const {
    if (from < 0 || to <= from || (bucket != 1 && bucket != 60 && bucket != 1440) || to - from > 527040)
        throw std::invalid_argument("Invalid minute range (maximum 366 days) or bucket");
    Statement s(db_, "SELECT (minute/?)*?,module,metric,SUM(value) FROM counts WHERE minute>=? AND minute<? "
                     "AND NOT(session=? AND minute>=?) GROUP BY 1,module,metric ORDER BY 1,module,metric");
    sqlite3_bind_int(s.p, 1, bucket);
    sqlite3_bind_int(s.p, 2, bucket);
    sqlite3_bind_int64(s.p, 3, from);
    sqlite3_bind_int64(s.p, 4, to);
    bind_text(s.p, 5, exclude_session);
    sqlite3_bind_int64(s.p, 6, exclude_from);
    Json rows = Json::array();
    int result{};
    while ((result = sqlite3_step(s.p)) == SQLITE_ROW) {
        rows.push_back({{"minute", sqlite3_column_int64(s.p, 0)},
                        {"module", reinterpret_cast<const char *>(sqlite3_column_text(s.p, 1))},
                        {"metric", reinterpret_cast<const char *>(sqlite3_column_text(s.p, 2))},
                        {"value", sqlite3_column_int64(s.p, 3)}});
        if (rows.size() > 100000)
            throw std::runtime_error("Result too large; choose hour/day aggregation");
    }
    if (result != SQLITE_DONE)
        throw std::runtime_error(sqlite3_errmsg(db_));
    return rows;
}
void DatabaseManager::prune(std::int64_t cutoff) {
    Statement s(db_, "DELETE FROM counts WHERE minute<?");
    sqlite3_bind_int64(s.p, 1, cutoff);
    if (sqlite3_step(s.p) != SQLITE_DONE)
        throw std::runtime_error(sqlite3_errmsg(db_));
    Statement e(db_, "DELETE FROM network_events WHERE observed_ms<?");
    sqlite3_bind_int64(e.p, 1, cutoff * 60000);
    if (sqlite3_step(e.p) != SQLITE_DONE)
        throw std::runtime_error(sqlite3_errmsg(db_));
}
void DatabaseManager::clear() {
    exec("BEGIN IMMEDIATE;DELETE FROM counts;DELETE FROM network_events;COMMIT;");
}
Json DatabaseManager::network_events(std::int64_t from, std::int64_t to) const {
    Statement s(db_, "SELECT observed_ms,kind,interface_id,name FROM network_events WHERE observed_ms>=? AND "
                     "observed_ms<? ORDER BY observed_ms DESC LIMIT 500");
    sqlite3_bind_int64(s.p, 1, from * 60000);
    sqlite3_bind_int64(s.p, 2, to * 60000);
    Json events = Json::array();
    int result{};
    while ((result = sqlite3_step(s.p)) == SQLITE_ROW)
        events.push_back({{"observed_ms", sqlite3_column_int64(s.p, 0)},
                          {"kind", reinterpret_cast<const char *>(sqlite3_column_text(s.p, 1))},
                          {"interface_id", reinterpret_cast<const char *>(sqlite3_column_text(s.p, 2))},
                          {"name", reinterpret_cast<const char *>(sqlite3_column_text(s.p, 3))}});
    if (result != SQLITE_DONE)
        throw std::runtime_error(sqlite3_errmsg(db_));
    return events;
}
void DatabaseManager::export_csv(const std::filesystem::path &path, std::int64_t from,
                                 std::int64_t to) const {
    auto rows = query(from, to, 1);
    std::ofstream out(path, std::ios::binary);
    if (!out)
        throw std::runtime_error("Cannot write export");
    out << "utc_minute,module,metric,value\r\n";
    for (const auto &row : rows)
        out << row["minute"] << ',' << row["module"].get<std::string>() << ','
            << row["metric"].get<std::string>() << ',' << row["value"] << "\r\n";
    out.close();
    if (!out)
        throw std::runtime_error("Export write failed");
}
} // namespace pulsetrace
