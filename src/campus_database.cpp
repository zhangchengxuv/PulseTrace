#include "pulsetrace/database.hpp"
#include <sqlite3.h>
#include <stdexcept>
namespace pulsetrace {
namespace {
struct CampusStatement {
    sqlite3_stmt *p{};
    CampusStatement(sqlite3 *db,const char *sql){if(sqlite3_prepare_v2(db,sql,-1,&p,nullptr)!=SQLITE_OK)throw SQLiteError(db,"prepare",sql);}
    ~CampusStatement(){sqlite3_finalize(p);}
    int step(){const int rc=sqlite3_step(p);if(rc!=SQLITE_ROW&&rc!=SQLITE_DONE)throw SQLiteError(sqlite3_db_handle(p),"step",sqlite3_sql(p));return rc;}
    void text(int i,const std::string &value){if(sqlite3_bind_text(p,i,value.c_str(),static_cast<int>(value.size()),SQLITE_TRANSIENT)!=SQLITE_OK)throw SQLiteError(sqlite3_db_handle(p),"bind",sqlite3_sql(p));}
};
void optional_number(sqlite3_stmt *p,int column,const Json &value){
    if(value.is_null())sqlite3_bind_null(p,column);
    else if(value.is_number_unsigned())sqlite3_bind_int64(p,column,static_cast<sqlite3_int64>(value.get<std::uint64_t>()));
    else sqlite3_bind_double(p,column,value.get<double>());
}
Json read_optional(sqlite3_stmt *p,int column){
    if(sqlite3_column_type(p,column)==SQLITE_NULL)return nullptr;
    return sqlite3_column_type(p,column)==SQLITE_FLOAT?Json(sqlite3_column_double(p,column)):Json(sqlite3_column_int64(p,column));
}
Json read_snapshot(sqlite3_stmt *p){
    return {{"observed_ms",sqlite3_column_int64(p,0)},
            {"portal_origin",reinterpret_cast<const char*>(sqlite3_column_text(p,1))},
            {"portal_url",reinterpret_cast<const char*>(sqlite3_column_text(p,2))},
            {"balance",read_optional(p,3)},{"used_bytes",read_optional(p,4)},
            {"remaining_bytes",read_optional(p,5)},{"quota_bytes",read_optional(p,6)},
            {"fee",read_optional(p,7)},
            {"cycle_text",reinterpret_cast<const char*>(sqlite3_column_text(p,8))},
            {"quality",reinterpret_cast<const char*>(sqlite3_column_text(p,9))},
            {"confidence",sqlite3_column_double(p,10)},
            {"details",Json::parse(reinterpret_cast<const char*>(sqlite3_column_text(p,11)))}};
}
}
void DatabaseManager::save_campus_snapshot(const Json &snapshot){
    CampusStatement s(db_,"INSERT INTO campus_snapshots VALUES(?,?,?,?,?,?,?,?,?,?,?,?) ON CONFLICT(observed_ms) DO NOTHING");
    sqlite3_bind_int64(s.p,1,snapshot.at("observed_ms").get<std::int64_t>());
    s.text(2,snapshot.at("portal_origin"));
    s.text(3,snapshot.at("portal_url"));
    optional_number(s.p,4,snapshot.at("balance"));
    optional_number(s.p,5,snapshot.at("used_bytes"));
    optional_number(s.p,6,snapshot.at("remaining_bytes"));
    optional_number(s.p,7,snapshot.at("quota_bytes"));
    optional_number(s.p,8,snapshot.at("fee"));
    s.text(9,snapshot.value("cycle_text",""));
    s.text(10,snapshot.at("quality"));
    sqlite3_bind_double(s.p,11,snapshot.at("confidence").get<double>());
    s.text(12,snapshot.value("details",Json::object()).dump());
    if(s.step()!=SQLITE_DONE)throw std::runtime_error("Campus snapshot insert failed");
}
Json DatabaseManager::campus_latest(const std::string &origin) const{
    CampusStatement s(db_,origin.empty()
        ?"SELECT observed_ms,portal_origin,portal_url,balance,used_bytes,remaining_bytes,quota_bytes,fee,cycle_text,quality,confidence,details_json FROM campus_snapshots ORDER BY observed_ms DESC LIMIT 1"
        :"SELECT observed_ms,portal_origin,portal_url,balance,used_bytes,remaining_bytes,quota_bytes,fee,cycle_text,quality,confidence,details_json FROM campus_snapshots WHERE portal_origin=? ORDER BY observed_ms DESC LIMIT 1");
    if(!origin.empty())s.text(1,origin);
    const int rc=s.step();if(rc==SQLITE_DONE)return nullptr;return read_snapshot(s.p);
}
}
