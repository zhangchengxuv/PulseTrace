#include "pulsetrace/database.hpp"
#include <fstream>
#include <sqlite3.h>
#include <stdexcept>
#include <windows.h>
#include <chrono>
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
        int from=0;
        {Statement version(db_, "PRAGMA user_version");if(sqlite3_step(version.p)!=SQLITE_ROW)throw std::runtime_error("Cannot read database schema");from=sqlite3_column_int(version.p,0);}
        if(from>supported_schema||from<0)throw std::runtime_error("Database schema is newer than supported; update PulseTrace or restore a compatible backup. Original database preserved.");
        migration_={{"from",from},{"to",supported_schema},{"status",from==supported_schema?"current":"created"},{"backup",""}};
        if(from<supported_schema){
            bool populated=false;{Statement tables(db_,"SELECT count(*) FROM sqlite_schema WHERE type='table'");populated=sqlite3_step(tables.p)==SQLITE_ROW&&sqlite3_column_int(tables.p,0)>0;}
            if(populated){const auto stamp=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
                const auto dest=file.parent_path()/"backups"/(file.filename().string()+".pre-v"+std::to_string(from)+"-"+std::to_string(stamp)+".sqlite3");
                backup(dest);const auto path=dest.generic_u8string();migration_["backup"]=std::string(path.begin(),path.end());migration_["status"]="migrated";}
            exec("BEGIN IMMEDIATE");
            try{
                if(from<1)exec("CREATE TABLE IF NOT EXISTS counts(session TEXT NOT NULL,minute INTEGER NOT NULL,module TEXT NOT NULL,metric TEXT NOT NULL,value INTEGER NOT NULL,PRIMARY KEY(session,minute,module,metric)) WITHOUT ROWID;CREATE INDEX IF NOT EXISTS counts_range ON counts(minute,module);PRAGMA user_version=1;");
                if(from<2)exec("CREATE TABLE IF NOT EXISTS network_events(session TEXT NOT NULL,sequence INTEGER NOT NULL,observed_ms INTEGER NOT NULL,kind TEXT NOT NULL,interface_id TEXT NOT NULL,name TEXT NOT NULL,PRIMARY KEY(session,sequence)) WITHOUT ROWID;CREATE INDEX IF NOT EXISTS network_events_range ON network_events(observed_ms);PRAGMA user_version=2;");
                // Some early schema-v2 fixtures contain counts only. Preserve them and add the missing v2 table.
                exec("CREATE TABLE IF NOT EXISTS network_events(session TEXT NOT NULL,sequence INTEGER NOT NULL,observed_ms INTEGER NOT NULL,kind TEXT NOT NULL,interface_id TEXT NOT NULL,name TEXT NOT NULL,PRIMARY KEY(session,sequence)) WITHOUT ROWID;CREATE INDEX IF NOT EXISTS network_events_range ON network_events(observed_ms);");
                if(from<3)exec("CREATE TABLE IF NOT EXISTS protocol_samples(session TEXT NOT NULL,minute INTEGER NOT NULL,interface_id TEXT NOT NULL,source TEXT NOT NULL,basis TEXT NOT NULL,quality TEXT NOT NULL,ipv4_up INTEGER NOT NULL,ipv4_down INTEGER NOT NULL,ipv6_up INTEGER NOT NULL,ipv6_down INTEGER NOT NULL,accepted INTEGER NOT NULL,rejected INTEGER NOT NULL,unsupported INTEGER NOT NULL,events_lost INTEGER NOT NULL,buffers_lost INTEGER NOT NULL,PRIMARY KEY(session,minute,interface_id,source,basis)) WITHOUT ROWID;CREATE INDEX IF NOT EXISTS protocol_samples_range ON protocol_samples(minute,interface_id);PRAGMA user_version=3;");
                if(from<4)exec("CREATE TABLE traffic_rankings(session TEXT NOT NULL,minute INTEGER NOT NULL,source TEXT NOT NULL,kind TEXT NOT NULL,identity TEXT NOT NULL,family INTEGER NOT NULL,name TEXT NOT NULL,path TEXT NOT NULL,attribution TEXT NOT NULL,upload INTEGER NOT NULL,download INTEGER NOT NULL,quality INTEGER NOT NULL,PRIMARY KEY(session,minute,source,kind,identity,family)) WITHOUT ROWID;CREATE INDEX traffic_rankings_range ON traffic_rankings(minute,source,kind);PRAGMA user_version=4;");
                if(from<5)exec("CREATE TABLE app_usage(session TEXT NOT NULL,minute INTEGER NOT NULL,identity TEXT NOT NULL,name TEXT NOT NULL,path TEXT NOT NULL,foreground_ms INTEGER NOT NULL CHECK(foreground_ms>=0 AND foreground_ms<=60000),active_ms INTEGER NOT NULL CHECK(active_ms>=0 AND active_ms<=foreground_ms),PRIMARY KEY(session,minute,identity)) WITHOUT ROWID;CREATE INDEX app_usage_range ON app_usage(minute,identity);CREATE INDEX app_usage_identity ON app_usage(identity,minute);PRAGMA user_version=5;");
                {Statement c(db_,"SELECT session,minute,module,metric,value FROM counts LIMIT 0");Statement n(db_,"SELECT session,sequence,observed_ms,kind,interface_id,name FROM network_events LIMIT 0");Statement p(db_,"SELECT session,minute,interface_id,source,basis,quality,ipv4_up,ipv4_down,ipv6_up,ipv6_down,accepted,rejected,unsupported,events_lost,buffers_lost FROM protocol_samples LIMIT 0");Statement r(db_,"SELECT identity,upload,download,quality FROM traffic_rankings LIMIT 0");}
                {Statement check(db_,"PRAGMA quick_check");if(sqlite3_step(check.p)!=SQLITE_ROW||std::string(reinterpret_cast<const char *>(sqlite3_column_text(check.p,0)))!="ok")throw std::runtime_error("Migration integrity check failed");}
                exec("COMMIT");
                std::ofstream receipt(std::filesystem::path(file.wstring()+L".migration.json"));receipt<<migration_.dump(2);
            }catch(...){sqlite3_exec(db_,"ROLLBACK",nullptr,nullptr,nullptr);std::ofstream log(file.parent_path()/"migration-errors.log",std::ios::app);log<<"Schema migration rolled back from v"<<from<<"; compatible backup retained\n";throw;}
        }
        if(from==supported_schema){try{std::ifstream in(std::filesystem::path(file.wstring()+L".migration.json"));Json previous;if(in>>previous)migration_["previous"]=std::move(previous);}catch(...) {}}
        exec("PRAGMA journal_mode=WAL; PRAGMA synchronous=FULL; PRAGMA wal_autocheckpoint=256;");
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
void DatabaseManager::backup(const std::filesystem::path &destination) const {
    std::filesystem::create_directories(destination.parent_path());
    HANDLE reserved=CreateFileW(destination.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(reserved==INVALID_HANDLE_VALUE)throw std::runtime_error("Backup destination exists or is not writable");CloseHandle(reserved);
    sqlite3 *target=nullptr;const auto name=destination.u8string();
    try{
        if(sqlite3_open_v2(reinterpret_cast<const char *>(name.c_str()),&target,SQLITE_OPEN_READWRITE,nullptr)!=SQLITE_OK)throw std::runtime_error("Cannot open backup destination");
        auto *b=sqlite3_backup_init(target,"main",db_,"main");if(!b)throw std::runtime_error("Cannot begin SQLite online backup");
        int rc=SQLITE_OK;const auto deadline=GetTickCount64()+10000;
        do{rc=sqlite3_backup_step(b,256);if(rc==SQLITE_BUSY||rc==SQLITE_LOCKED)Sleep(20);}while((rc==SQLITE_OK||rc==SQLITE_BUSY||rc==SQLITE_LOCKED)&&GetTickCount64()<deadline);
        const int finish=sqlite3_backup_finish(b);if(rc!=SQLITE_DONE||finish!=SQLITE_OK)throw std::runtime_error("SQLite backup interrupted or storage unavailable");
        {Statement check(target,"PRAGMA quick_check");if(sqlite3_step(check.p)!=SQLITE_ROW||std::string(reinterpret_cast<const char *>(sqlite3_column_text(check.p,0)))!="ok")throw std::runtime_error("Backup integrity check failed");}
        sqlite3_close(target);target=nullptr;
    }catch(...){if(target)sqlite3_close(target);std::filesystem::remove(destination);throw;}
}
void DatabaseManager::backup_file(const std::filesystem::path &source,const std::filesystem::path &destination){
    DatabaseManager snapshot(ReadOnlyTag{}); // Private read-only connection: no migration or PRAGMA writes.
    const auto name=source.u8string();if(sqlite3_open_v2(reinterpret_cast<const char *>(name.c_str()),&snapshot.db_,SQLITE_OPEN_READONLY,nullptr)!=SQLITE_OK)throw std::runtime_error("Cannot open backup source read-only");
    sqlite3_busy_timeout(snapshot.db_,2000);snapshot.backup(destination);
}
void DatabaseManager::save(const std::string &session, const Json &rows, const Json &events) {
    exec("BEGIN IMMEDIATE");
    try {
        Statement s(db_, "INSERT INTO counts VALUES(?,?,?,?,?) ON CONFLICT(session,minute,module,metric) DO "
                         "UPDATE SET value=excluded.value");
        Statement ip(db_, "INSERT INTO protocol_samples VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?) ON "
                          "CONFLICT(session,minute,interface_id,source,basis) DO UPDATE SET "
                          "quality=excluded.quality,ipv4_up=excluded.ipv4_up,ipv4_down=excluded.ipv4_down,"
                          "ipv6_up=excluded.ipv6_up,ipv6_down=excluded.ipv6_down,accepted=excluded.accepted,"
                          "rejected=excluded.rejected,unsupported=excluded.unsupported,events_lost=excluded."
                          "events_lost,buffers_lost=excluded.buffers_lost");
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
            if (row.contains("ip_record")) {
                const auto &r = row.at("ip_record");
                bind_text(ip.p, 1, session);
                sqlite3_bind_int64(ip.p, 2, r.at("minute").get<std::int64_t>());
                int column = 3;
                for (const auto *k : {"interface_id", "source", "basis", "quality"})
                    bind_text(ip.p, column++, r.at(k).get<std::string>());
                for (const auto *k : {"ipv4_up", "ipv4_down", "ipv6_up", "ipv6_down", "accepted", "rejected",
                                      "unsupported", "events_lost", "buffers_lost"})
                    sqlite3_bind_int64(ip.p, column++, r.at(k).get<std::int64_t>());
                if (sqlite3_step(ip.p) != SQLITE_DONE)
                    throw std::runtime_error(sqlite3_errmsg(db_));
                sqlite3_reset(ip.p);
                sqlite3_clear_bindings(ip.p);
            }
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
Json DatabaseManager::history_bounds() const {
    Statement s(db_, "SELECT MIN(m) FROM (SELECT MIN(minute) m FROM counts UNION ALL SELECT MIN(minute) FROM protocol_samples UNION ALL SELECT MIN(minute) FROM app_usage UNION ALL SELECT MIN(minute) FROM traffic_rankings)");
    if(sqlite3_step(s.p)!=SQLITE_ROW)throw std::runtime_error(sqlite3_errmsg(db_));
    return {{"first_minute",sqlite3_column_type(s.p,0)==SQLITE_NULL?Json(nullptr):Json(sqlite3_column_int64(s.p,0))}};
}
Json DatabaseManager::query(std::int64_t from, std::int64_t to, int bucket,
                            const std::string &exclude_session, std::int64_t exclude_from) const {
    if (from < 0 || to <= from || (bucket != 1 && bucket != 60 && bucket != 1440) || to > 4223371680LL || (from != 0 && to - from > 527040))
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
    Statement ip(db_, "DELETE FROM protocol_samples WHERE minute<?");
    sqlite3_bind_int64(ip.p, 1, cutoff);
    if (sqlite3_step(ip.p) != SQLITE_DONE)
        throw std::runtime_error(sqlite3_errmsg(db_));
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
    exec(
        "BEGIN IMMEDIATE;DELETE FROM counts;DELETE FROM app_usage;DELETE FROM network_events;DELETE FROM protocol_samples;DELETE FROM traffic_rankings;COMMIT;");
}
Json DatabaseManager::traffic_summary(std::int64_t from,std::int64_t to) const {
    Json out = {{"upload_bytes", 0}, {"download_bytes", 0}, {"has_totals", false},
                {"ipv4_up", 0}, {"ipv4_down", 0}, {"ipv6_up", 0}, {"ipv6_down", 0},
                {"accepted", 0}, {"rejected", 0}, {"unsupported", 0},
                {"events_lost", 0}, {"buffers_lost", 0}, {"has_ip", false},
                {"quality", "no_data"}, {"reference_validated", false},
                {"total_basis", "selected_interface_octets"}, {"ip_basis", "validated_ip_packet_lengths"}};
    Statement s(db_, "SELECT metric,SUM(value),COUNT(DISTINCT minute) FROM counts WHERE minute>=? AND minute<? AND module='network' AND "
                     "metric IN ('interface_upload_bytes','interface_download_bytes') GROUP BY metric");
    sqlite3_bind_int64(s.p,1,from);sqlite3_bind_int64(s.p,2,to);
    out["observed_minutes"]=0;int rc;
    while ((rc = sqlite3_step(s.p)) == SQLITE_ROW) {
        const std::string metric = reinterpret_cast<const char *>(sqlite3_column_text(s.p, 0));
        out[metric == "interface_upload_bytes" ? "upload_bytes" : "download_bytes"] = sqlite3_column_int64(s.p, 1);
        out["has_totals"] = true;out["observed_minutes"]=std::max(out["observed_minutes"].get<std::int64_t>(),sqlite3_column_int64(s.p,2));
    }
    if (rc != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(db_));
    Statement ip(db_, "SELECT COALESCE(SUM(ipv4_up),0),COALESCE(SUM(ipv4_down),0),"
                      "COALESCE(SUM(ipv6_up),0),COALESCE(SUM(ipv6_down),0),COALESCE(SUM(accepted),0),"
                      "COALESCE(SUM(rejected),0),COALESCE(SUM(unsupported),0),COALESCE(MAX(events_lost),0),"
                      "COALESCE(MAX(buffers_lost),0),COALESCE(MAX(quality='incomplete_observation'),0),COUNT(DISTINCT minute),COUNT(DISTINCT source||char(31)||basis),MAX(source),MAX(basis) FROM protocol_samples WHERE minute>=? AND minute<?");
    sqlite3_bind_int64(ip.p,1,from);sqlite3_bind_int64(ip.p,2,to);
    if (sqlite3_step(ip.p) != SQLITE_ROW) throw std::runtime_error(sqlite3_errmsg(db_));
    int col=0;
    for (const char *key : {"ipv4_up","ipv4_down","ipv6_up","ipv6_down","accepted","rejected","unsupported","events_lost","buffers_lost"})
        out[key] = sqlite3_column_int64(ip.p, col++);
    out["has_ip"] = out["accepted"].get<std::int64_t>() > 0;
    out["quality"] = sqlite3_column_int(ip.p, 9) ? "incomplete_observation" :
                       out["has_ip"].get<bool>() ? "observed_unverified_reference" : "no_data";
    out["ip_minutes"]=sqlite3_column_int64(ip.p,10);
    out["basis_count"]=sqlite3_column_int(ip.p,11);
    for(const auto &[key,column]:{std::pair{"source",12},std::pair{"basis",13}})out[key]=sqlite3_column_type(ip.p,column)==SQLITE_NULL?"":reinterpret_cast<const char*>(sqlite3_column_text(ip.p,column));
    return out;
}
void DatabaseManager::visit_traffic(const std::function<void(const std::string &, const Json &)> &visitor) const {
    // Stream startup recovery in bounded batches; absolute UPSERTs make retries safe.
    Statement s(db_, "SELECT session,minute,module,metric,value FROM counts WHERE "
                     "(module='network' AND (metric IN ('interface_upload_bytes','interface_download_bytes') "
                     "OR metric LIKE 'bytes/%')) ORDER BY session,minute");
    Json rows=Json::array(); std::string session; int rc;
    while ((rc=sqlite3_step(s.p))==SQLITE_ROW) {
        const std::string next=reinterpret_cast<const char *>(sqlite3_column_text(s.p,0));
        if (!rows.empty() && (next!=session || rows.size()>=4096)) {visitor(session,rows); rows=Json::array();}
        session=next;
        rows.push_back({{"minute",sqlite3_column_int64(s.p,1)}, {"module",reinterpret_cast<const char *>(sqlite3_column_text(s.p,2))},
                        {"metric",reinterpret_cast<const char *>(sqlite3_column_text(s.p,3))}, {"value",sqlite3_column_int64(s.p,4)}});
    }
    if(rc!=SQLITE_DONE)throw std::runtime_error(sqlite3_errmsg(db_));
    if(!rows.empty())visitor(session,rows);
    Statement p(db_, "SELECT session,minute,interface_id,source,basis,quality,ipv4_up,ipv4_down,ipv6_up,ipv6_down,accepted,rejected,unsupported,events_lost,buffers_lost FROM protocol_samples ORDER BY session,minute");
    rows=Json::array();session.clear();
    while((rc=sqlite3_step(p.p))==SQLITE_ROW){
        const std::string next=reinterpret_cast<const char *>(sqlite3_column_text(p.p,0));
        if(!rows.empty()&&(next!=session||rows.size()>=1024)){visitor(session,rows);rows=Json::array();}
        session=next;Json r={{"minute",sqlite3_column_int64(p.p,1)}};int c=2;
        for(const char *key:{"interface_id","source","basis","quality"})r[key]=reinterpret_cast<const char *>(sqlite3_column_text(p.p,c++));
        for(const char *key:{"ipv4_up","ipv4_down","ipv6_up","ipv6_down","accepted","rejected","unsupported","events_lost","buffers_lost"})r[key]=sqlite3_column_int64(p.p,c++);
        rows.push_back({{"minute",r["minute"]},{"module","network_ip"},{"metric",r["interface_id"].get<std::string>()+"/quality_state"},{"value",r["rejected"]},{"ip_record",r}});
    }
    if(rc!=SQLITE_DONE)throw std::runtime_error(sqlite3_errmsg(db_));
    if(!rows.empty())visitor(session,rows);
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
Json DatabaseManager::protocol_samples(std::int64_t from, std::int64_t to, int bucket,
                                       const std::string &exclude, std::int64_t floor) const {
    if (from < 0 || to <= from || to > 4223371680LL || (from != 0 && to - from > 527040) || (bucket != 1 && bucket != 60 && bucket != 1440))
        throw std::invalid_argument("Invalid protocol range");
    Statement s(
        db_,
        "SELECT (minute/?)*?,interface_id,source,basis,CASE WHEN MAX(quality='incomplete_observation') THEN "
        "'incomplete_observation' ELSE 'observed_unverified_reference' "
        "END,SUM(ipv4_up),SUM(ipv4_down),SUM(ipv6_up),SUM(ipv6_down),SUM(accepted),SUM(rejected),SUM("
        "unsupported),MAX(events_lost),MAX(buffers_lost) FROM protocol_samples WHERE minute>=? AND minute<? "
        "AND NOT(session=? AND minute>=?) GROUP BY 1,interface_id,source,basis ORDER BY 1,interface_id");
    sqlite3_bind_int(s.p, 1, bucket);
    sqlite3_bind_int(s.p, 2, bucket);
    sqlite3_bind_int64(s.p, 3, from);
    sqlite3_bind_int64(s.p, 4, to);
    bind_text(s.p, 5, exclude);
    sqlite3_bind_int64(s.p, 6, floor);
    Json out = Json::array();
    int result{};
    while ((result = sqlite3_step(s.p)) == SQLITE_ROW) {
        Json r = {{"minute", sqlite3_column_int64(s.p, 0)}};
        int c = 1;
        for (const auto *k : {"interface_id", "source", "basis", "quality"})
            r[k] = reinterpret_cast<const char *>(sqlite3_column_text(s.p, c++));
        for (const auto *k : {"ipv4_up", "ipv4_down", "ipv6_up", "ipv6_down", "accepted", "rejected",
                              "unsupported", "events_lost", "buffers_lost"})
            r[k] = sqlite3_column_int64(s.p, c++);
        out.push_back(std::move(r));
        if (out.size() > 100000)
            throw std::runtime_error("Protocol result too large; use hour/day aggregation");
    }
    if (result != SQLITE_DONE)
        throw std::runtime_error(sqlite3_errmsg(db_));
    return out;
}
std::string DatabaseManager::ranking_icon_path(const std::string &source,const std::string &identity)const{
 Statement s(db_,"SELECT path FROM traffic_rankings WHERE source=? AND kind='app' AND identity=? ORDER BY minute DESC LIMIT 1");bind_text(s.p,1,source);bind_text(s.p,2,identity);const auto rc=sqlite3_step(s.p);if(rc==SQLITE_ROW)return reinterpret_cast<const char*>(sqlite3_column_text(s.p,0));if(rc!=SQLITE_DONE)throw std::runtime_error(sqlite3_errmsg(db_));return {};
}
void DatabaseManager::save_rankings(const std::string &session,const Json &rows){
 if(rows.empty())return;exec("BEGIN IMMEDIATE");
 try{Statement s(db_,"INSERT INTO traffic_rankings VALUES(?,?,?,?,?,?,?,?,?,?,?,?) ON CONFLICT(session,minute,source,kind,identity,family) DO UPDATE SET upload=excluded.upload,download=excluded.download,quality=excluded.quality,name=excluded.name,path=excluded.path,attribution=excluded.attribution");
  for(const auto &r:rows){bind_text(s.p,1,session);sqlite3_bind_int64(s.p,2,r.at("minute").get<std::int64_t>());int col=3;for(const char *k:{"source","kind","key"})bind_text(s.p,col++,r.at(k));sqlite3_bind_int(s.p,col++,r.at("family"));for(const char *k:{"name","path","attribution"})bind_text(s.p,col++,r.at(k));for(const char *k:{"upload","download","quality"})sqlite3_bind_int64(s.p,col++,r.at(k).get<std::int64_t>());
   if(sqlite3_step(s.p)!=SQLITE_DONE)throw std::runtime_error(sqlite3_errmsg(db_));sqlite3_reset(s.p);sqlite3_clear_bindings(s.p);}
  exec("COMMIT");}catch(...){exec("ROLLBACK");throw;}
}
Json DatabaseManager::rankings(std::int64_t from,std::int64_t to,const std::string &source,const std::string &kind,const std::string &exclude,std::int64_t floor)const{
 if(from<0||to<=from||to>4223371680LL||(from!=0&&to-from>527040))throw std::invalid_argument("Invalid ranking range");
 Statement s(db_,"SELECT identity,family,MAX(name),MAX(path),MAX(attribution),SUM(upload),SUM(download),MAX(quality&1)+MAX(quality&2)+MAX(quality&4)+MAX(quality&8)+MAX(quality&16)+MAX(quality&32) FROM traffic_rankings WHERE minute>=? AND minute<? AND source=? AND kind=? AND NOT(session=? AND minute>=?) GROUP BY identity,family ORDER BY identity,family");
 sqlite3_bind_int64(s.p,1,from);sqlite3_bind_int64(s.p,2,to);bind_text(s.p,3,source);bind_text(s.p,4,kind);bind_text(s.p,5,exclude);sqlite3_bind_int64(s.p,6,floor);
 Json rows=Json::array();int rc{};while((rc=sqlite3_step(s.p))==SQLITE_ROW){Json r={{"source",source},{"kind",kind},{"key",reinterpret_cast<const char*>(sqlite3_column_text(s.p,0))},{"family",sqlite3_column_int(s.p,1)}};int c=2;for(const char *k:{"name","path","attribution"})r[k]=reinterpret_cast<const char*>(sqlite3_column_text(s.p,c++));for(const char *k:{"upload","download","quality"})r[k]=sqlite3_column_int64(s.p,c++);rows.push_back(std::move(r));if(rows.size()>65536)throw std::runtime_error("ranking_result_capacity");}
 if(rc!=SQLITE_DONE)throw std::runtime_error(sqlite3_errmsg(db_));return rows;
}
void DatabaseManager::clear_rankings(bool domains){exec(domains?"DELETE FROM traffic_rankings WHERE kind='domain'":"DELETE FROM traffic_rankings");}
void DatabaseManager::prune_rankings(std::int64_t cutoff){Statement s(db_,"DELETE FROM traffic_rankings WHERE minute<?");sqlite3_bind_int64(s.p,1,cutoff);if(sqlite3_step(s.p)!=SQLITE_DONE)throw std::runtime_error(sqlite3_errmsg(db_));}
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
