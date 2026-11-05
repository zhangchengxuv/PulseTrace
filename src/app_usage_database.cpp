#include "pulsetrace/database.hpp"
#include <sqlite3.h>
#include <stdexcept>
namespace pulsetrace {
namespace {
struct UsageStatement{
    sqlite3_stmt *p{};
    UsageStatement(sqlite3 *db,const char *sql){if(sqlite3_prepare_v2(db,sql,-1,&p,nullptr)!=SQLITE_OK)throw std::runtime_error(sqlite3_errmsg(db));}
    ~UsageStatement(){sqlite3_finalize(p);}
    void text(int i,const std::string &s){sqlite3_bind_text(p,i,s.c_str(),static_cast<int>(s.size()),SQLITE_TRANSIENT);}
};
}
void DatabaseManager::save_usage(const std::string &session,const Json &rows){
    if(rows.empty())return;exec("BEGIN IMMEDIATE");
    try{UsageStatement s(db_,"INSERT INTO app_usage VALUES(?,?,?,?,?,?,?) ON CONFLICT(session,minute,identity) DO UPDATE SET name=excluded.name,path=excluded.path,foreground_ms=excluded.foreground_ms,active_ms=excluded.active_ms");
        for(const auto &r:rows){s.text(1,session);sqlite3_bind_int64(s.p,2,r.at("minute"));s.text(3,r.at("key"));s.text(4,r.at("name"));s.text(5,r.at("path"));
            const auto foreground=r.at("foreground_ms").get<std::int64_t>(),active=r.at("active_ms").get<std::int64_t>();
            if(foreground<0||active<0||active>foreground||foreground>60000)throw std::invalid_argument("Invalid application duration");
            sqlite3_bind_int64(s.p,6,foreground);sqlite3_bind_int64(s.p,7,active);if(sqlite3_step(s.p)!=SQLITE_DONE)throw std::runtime_error(sqlite3_errmsg(db_));sqlite3_reset(s.p);sqlite3_clear_bindings(s.p);
        }exec("COMMIT");
    }catch(...){sqlite3_exec(db_,"ROLLBACK",nullptr,nullptr,nullptr);throw;}
}
Json DatabaseManager::usage(std::int64_t from,std::int64_t to,const std::string &exclude,std::int64_t floor,int bucket)const{
    if(bucket!=1&&bucket!=60&&bucket!=1440)throw std::invalid_argument("Invalid usage bucket");
    UsageStatement s(db_,"SELECT (minute/?)*?,identity,MAX(name),MAX(path),SUM(foreground_ms),SUM(active_ms) FROM app_usage WHERE minute>=? AND minute<? AND NOT(session=? AND minute>=?) GROUP BY 1,identity ORDER BY 1,identity");
    sqlite3_bind_int(s.p,1,bucket);sqlite3_bind_int(s.p,2,bucket);sqlite3_bind_int64(s.p,3,from);sqlite3_bind_int64(s.p,4,to);s.text(5,exclude);sqlite3_bind_int64(s.p,6,floor);Json result=Json::array();int rc;
    while((rc=sqlite3_step(s.p))==SQLITE_ROW){if(result.size()>=100000)throw std::runtime_error("Usage query too large; narrow the range");
        const auto text=[&](int c){return std::string(reinterpret_cast<const char*>(sqlite3_column_text(s.p,c)));};
        result.push_back({{"minute",sqlite3_column_int64(s.p,0)},{"key",text(1)},{"name",text(2)},{"path",text(3)},{"foreground_ms",sqlite3_column_int64(s.p,4)},{"active_ms",sqlite3_column_int64(s.p,5)}});
    }if(rc!=SQLITE_DONE)throw std::runtime_error(sqlite3_errmsg(db_));return result;
}
void DatabaseManager::prune_usage(std::int64_t cutoff){UsageStatement s(db_,"DELETE FROM app_usage WHERE minute<?");sqlite3_bind_int64(s.p,1,cutoff);if(sqlite3_step(s.p)!=SQLITE_DONE)throw std::runtime_error(sqlite3_errmsg(db_));}
std::string DatabaseManager::usage_icon_path(const std::string &identity)const{
    UsageStatement s(db_,"SELECT path FROM app_usage WHERE identity=? ORDER BY minute DESC LIMIT 1");s.text(1,identity);
    const auto rc=sqlite3_step(s.p);if(rc==SQLITE_DONE)return "";if(rc!=SQLITE_ROW)throw std::runtime_error(sqlite3_errmsg(db_));return reinterpret_cast<const char*>(sqlite3_column_text(s.p,0));
}
}
