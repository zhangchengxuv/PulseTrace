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

Json DatabaseManager::music_insights(const Json &options, const MusicInsightLive &live) const {
 // Use music as the driver and join on both the collecting session and UTC minute.
 // Replace the persisted tail of each stream with its in-memory snapshot.
 MusicStatement s(db_, R"SQL(
  WITH source AS (
   SELECT session,minute,player,identity,title,artist,played_ms
     FROM music_minutes
    WHERE minute>=:from AND minute<:to AND NOT(session=:session AND minute>=:music_floor)
      AND played_ms>0 AND (:player='all' OR player=:player)
   UNION ALL
   SELECT :session,value->>'minute',value->>'player',value->>'key',
          value->>'title',value->>'artist',value->>'played_ms'
     FROM json_each(:live_music)
    WHERE value->>'played_ms'>0 AND (:player='all' OR value->>'player'=:player)
  ), identities AS (
   SELECT session,minute,CASE WHEN :merge THEN '' ELSE player END AS source_player,
          CASE WHEN :artists THEN COALESCE(
                CASE WHEN json_valid(identity) THEN NULLIF(identity->>'$[1]','') END,lower(trim(artist)))
               ELSE identity END AS item,
          title,artist,played_ms
     FROM source
  ), listening AS MATERIALIZED (
   SELECT session,minute,source_player,item,MAX(title) AS title,MAX(artist) AS artist,
          MIN(60000,SUM(played_ms)) AS duration
     FROM identities WHERE item IS NOT NULL AND item<>''
    GROUP BY session,minute,source_player,item
  ), totals AS MATERIALIZED (
   SELECT source_player,item,MAX(title) AS title,MAX(artist) AS artist,SUM(duration) AS duration
     FROM listening GROUP BY source_player,item
  ), top_music AS MATERIALIZED (
   SELECT * FROM totals ORDER BY duration DESC,source_player,item LIMIT 5
  ), listening_minutes AS (
   SELECT DISTINCT session,minute FROM listening
  ), input AS (
   SELECT l.session,l.minute,
          MIN(60000,SUM(CASE WHEN c.module='coverage' AND c.metric='observed_ms' THEN c.value ELSE 0 END)) AS covered,
          SUM(CASE WHEN c.module='keyboard' THEN c.value ELSE 0 END) AS presses
     FROM listening_minutes l CROSS JOIN counts c
    WHERE c.session=l.session AND c.minute=l.minute AND c.module IN ('coverage','keyboard')
      AND NOT(c.session=:session AND c.minute>=:input_floor)
    GROUP BY l.session,l.minute
   UNION ALL
   SELECT :session,value->>0,value->>2,value->>1 FROM json_each(:live_input)
  ), matched_input AS (
   SELECT l.source_player,l.item,SUM(1.0*l.duration*i.covered/60000) AS observed_ms,
          SUM(1.0*i.presses*l.duration/60000) AS estimated_presses
     FROM input i JOIN listening l ON l.session=i.session AND l.minute=i.minute
    WHERE i.covered>0 GROUP BY l.source_player,l.item HAVING observed_ms>=300000
  ), top_input AS (
   SELECT t.*,m.observed_ms,60000.0*m.estimated_presses/m.observed_ms AS kpm
     FROM matched_input m JOIN totals t USING(source_player,item)
    ORDER BY kpm DESC,m.observed_ms DESC,t.source_player,t.item LIMIT 8
  ), top_minutes AS MATERIALIZED (
   SELECT l.* FROM top_music t JOIN listening l USING(source_player,item)
  ), live_apps AS (
   SELECT value->>'minute' AS minute,value->>'key' AS identity,value->>'name' AS name,
          value->>'foreground_ms' AS foreground_ms FROM json_each(:live_usage)
  ), matched_apps AS (
   SELECT l.source_player,l.item,u.identity AS app,u.name,1.0*l.duration*u.foreground_ms/60000 AS overlap
     FROM top_minutes l CROSS JOIN app_usage u
    WHERE u.session=l.session AND u.minute=l.minute AND u.foreground_ms>0
      AND NOT(u.session=:session AND u.minute>=:usage_floor)
   UNION ALL
   SELECT l.source_player,l.item,u.identity,u.name,1.0*l.duration*u.foreground_ms/60000
     FROM live_apps u JOIN top_minutes l ON l.session=:session AND l.minute=u.minute
    WHERE u.foreground_ms>0
  ), app_totals AS MATERIALIZED (
   SELECT source_player,item,app,MAX(name) AS name,SUM(overlap) AS overlap_ms
     FROM matched_apps GROUP BY source_player,item,app
  )
  SELECT 'kpm',source_player,title,artist,duration,observed_ms,kpm,'[]' FROM top_input
  UNION ALL
  SELECT 'apps',source_player,title,artist,duration,0,0,
         (SELECT json_group_array(json_object('name',name,'overlap_ms',overlap_ms))
            FROM (SELECT name,overlap_ms FROM app_totals a
                   WHERE a.source_player=t.source_player AND a.item=t.item
                   ORDER BY overlap_ms DESC,a.app LIMIT 3))
    FROM top_music t
 )SQL");
 const auto param = [&](const char *name) { return sqlite3_bind_parameter_index(s.p, name); };
 const auto integer = [&](const char *name, std::int64_t value) { sqlite3_bind_int64(s.p, param(name), value); };
 integer(":from", options.at("from")); integer(":to", options.at("to"));
 integer(":input_floor", live.input_floor); integer(":music_floor", live.music_floor);
 integer(":usage_floor", live.usage_floor); integer(":merge", options.at("merge").get<bool>());
 integer(":artists", options.at("group") == "artists");
 s.text(param(":session"), live.session); s.text(param(":player"), options.at("player"));
 s.text(param(":live_music"), live.music.dump()); s.text(param(":live_input"), live.input.dump());
 s.text(param(":live_usage"), live.usage.dump());
 Json result = {{"leaderboard", Json::array()}, {"soundtracks", Json::array()}};
 while (s.step() == SQLITE_ROW) {
  const auto text = [&](int column) { return std::string(reinterpret_cast<const char *>(sqlite3_column_text(s.p, column))); };
  Json item = {{"player", text(1)}, {"title", text(2)}, {"artist", text(3)}, {"played_ms", sqlite3_column_int64(s.p, 4)}};
  if (text(0) == "kpm") {
   item["observed_ms"] = sqlite3_column_int64(s.p, 5);
   item["kpm"] = sqlite3_column_double(s.p, 6);
   result["leaderboard"].push_back(std::move(item));
  } else {
   item["apps"] = Json::parse(text(7));
   result["soundtracks"].push_back(std::move(item));
  }
 }
 return result;
}
}
