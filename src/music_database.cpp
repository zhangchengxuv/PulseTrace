#include "pulsetrace/database.hpp"
#include <sqlite3.h>
namespace pulsetrace {
namespace {
struct MusicStatement {
 sqlite3_stmt *p{};
 MusicStatement(sqlite3 *db,const char *sql){if(sqlite3_prepare_v2(db,sql,-1,&p,nullptr)!=SQLITE_OK)throw SQLiteError(db,"prepare",sql);}
 ~MusicStatement(){sqlite3_finalize(p);}
 void text(int i,const std::string &s){if(sqlite3_bind_text(p,i,s.c_str(),static_cast<int>(s.size()),SQLITE_TRANSIENT)!=SQLITE_OK)throw SQLiteError(sqlite3_db_handle(p),"bind");}
 int step(){int rc=sqlite3_step(p);if(rc!=SQLITE_ROW&&rc!=SQLITE_DONE)throw SQLiteError(sqlite3_db_handle(p),"step",sqlite3_sql(p));return rc;}
};
}
void DatabaseManager::save_music(const std::string &session,const Json &rows){
 if(rows.empty())return;if(!batch_)exec("BEGIN IMMEDIATE");
 try{MusicStatement s(db_,"INSERT INTO music_minutes VALUES(?,?,?,?,?,?,?,?,?,?,?) ON CONFLICT(session,minute,player,identity) DO UPDATE SET played_ms=excluded.played_ms,plays=excluded.plays");
  for(const auto &r:rows){
   auto minute=r.at("minute").get<std::int64_t>(),ms=r.at("played_ms").get<std::int64_t>(),plays=r.at("plays").get<std::int64_t>();
   if(minute<0||ms<0||ms>960000||plays<0||plays>960||r.at("key").get<std::string>().size()>16384||(r.at("player")!="qq"&&r.at("player")!="netease"))throw std::invalid_argument("Invalid music aggregate");
   s.text(1,session);sqlite3_bind_int64(s.p,2,minute);int i=3;for(const char *key:{"player","key","title","artist","album"}){const auto t=r.at(key).get<std::string>();if(i>4&&t.size()>1024)throw std::invalid_argument("Invalid music metadata");s.text(i++,t);}
   sqlite3_bind_int64(s.p,8,r.at("duration_ms").get<std::int64_t>());sqlite3_bind_int64(s.p,9,ms);sqlite3_bind_int64(s.p,10,plays);sqlite3_bind_int(s.p,11,1);s.step();sqlite3_reset(s.p);sqlite3_clear_bindings(s.p);
  }if(!batch_)exec("COMMIT");
 }catch(...){if(!batch_)sqlite3_exec(db_,"ROLLBACK",nullptr,nullptr,nullptr);throw;}
}
Json DatabaseManager::music(std::int64_t from,std::int64_t to,const std::string &exclude,std::int64_t floor)const{
 MusicStatement s(db_,"SELECT minute,player,identity,MAX(title),MAX(artist),MAX(album),MAX(duration_ms),SUM(played_ms),SUM(plays) FROM music_minutes WHERE minute>=? AND minute<? AND NOT(session=? AND minute>=?) GROUP BY minute,player,identity ORDER BY minute,player,identity");
 sqlite3_bind_int64(s.p,1,from);sqlite3_bind_int64(s.p,2,to);s.text(3,exclude);sqlite3_bind_int64(s.p,4,floor);Json rows=Json::array();
 while(s.step()==SQLITE_ROW){if(rows.size()>=200000)throw std::runtime_error("Music range too large; narrow the range");auto text=[&](int col){return std::string(reinterpret_cast<const char*>(sqlite3_column_text(s.p,col)));};
  rows.push_back({{"minute",sqlite3_column_int64(s.p,0)},{"player",text(1)},{"key",text(2)},{"title",text(3)},{"artist",text(4)},{"album",text(5)},{"duration_ms",sqlite3_column_int64(s.p,6)},{"played_ms",sqlite3_column_int64(s.p,7)},{"plays",sqlite3_column_int64(s.p,8)}});
 }return rows;
}
}
