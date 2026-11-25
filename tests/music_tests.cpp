#include "pulsetrace/music.hpp"
#include "pulsetrace/core_service.hpp"
#include "pulsetrace/protocol.hpp"
#include "pulsetrace/pending_store.hpp"
#include <sqlite3.h>
#include <future>
#include <fstream>
#include <iostream>
using namespace pulsetrace;
namespace {
int checks{};void check(bool ok,const char *m){++checks;if(!ok)throw std::runtime_error(m);}
template<class F>void rejects(F f,const char *m){bool rejected=false;try{f();}catch(...){rejected=true;}check(rejected,m);}
std::int64_t total(const Json &rows,const char *field){std::int64_t n=0;for(const auto &r:rows)n+=r.at(field).get<std::int64_t>();return n;}
Json rpc(CoreService &core,const char *type,Json params=Json::object()){
 auto p=std::make_shared<std::promise<Json>>();auto future=p->get_future();
 auto message=parse_message(Json{{"type",type},{"request_id",1},{"params",params}}.dump());
 core.request(message,[p](Json r){p->set_value(r);});if(future.wait_for(std::chrono::seconds(15))!=std::future_status::ready)throw std::runtime_error("Music RPC timeout");auto r=future.get();if(!r.value("ok",false))throw std::runtime_error(r["error"]["message"]);return r["data"];
}
void test_insights(const std::filesystem::path &root) {
 const auto dir = root / "insights";
 std::filesystem::create_directories(dir);
 const auto first = (utc_now_ms() / 60000 / 1440 - 1) * 1440 + 60;
 Json options = {{"from", first}, {"to", first + 12}, {"time_zone", "UTC"}, {"bucket", 1},
                 {"player", "all"}, {"group", "songs"}, {"merge", false}};
 const auto track = [](auto minute, int ms, bool short_track = false) {
  return Json{{"minute", minute}, {"player", "qq"},
              {"key", Json::array({short_track ? "short" : "long", "artist"}).dump()},
              {"title", short_track ? "Short track" : "Long track"}, {"artist", short_track ? "artist" : "Artist"},
              {"album", "Album"}, {"duration_ms", 180000}, {"played_ms", ms}, {"plays", 0}};
 };
 const auto count = [](auto minute, const char *module, const char *metric, int value) {
  return Json{{"minute", minute}, {"module", module}, {"metric", metric}, {"value", value}};
 };
 const auto app = [](auto minute, const char *name, int ms) {
  return Json{{"minute", minute}, {"key", name}, {"name", name}, {"path", "private-fixture.exe"},
              {"foreground_ms", ms}, {"active_ms", 0}};
 };
 {
  DatabaseManager db(dir / "monitor.db");
  Json music = Json::array(), input = Json::array(), apps = Json::array();
  for (int i = 0; i < 11; ++i) {
   const auto minute = first + i;
   music.push_back(track(minute, i == 5 ? 30000 : i == 10 ? 0 : 60000, i >= 6));
   input.push_back(count(minute, "coverage", "observed_ms", 60000));
   if (i != 10) {
    input.push_back(count(minute, "keyboard", "30", i < 6 ? 120 : 900));
    input.push_back(count(minute, "repeat", "30", 10000));
    apps.push_back(app(minute, "Editor", 45000));
    apps.push_back(app(minute, "Browser", 15000));
   }
  }
  db.save_music("work", music);
  db.save("work", input);
  db.save_usage("work", apps);
  db.save("other-session", Json::array({count(first, "keyboard", "30", 20000)}));
  db.save_usage("other-session", Json::array({app(first, "Unrelated", 60000)}));
  auto second_player = track(first, 60000);
  second_player["player"] = "netease";
  db.save_music("work", Json::array({second_player}));
  MusicInsightLive live;
  const auto report = db.music_insights(options, live);
  const auto &leaders = report.at("leaderboard");
  check(leaders.size() == 1 && leaders[0]["title"] == "Long track" &&
        leaders[0]["kpm"] == 120.0 && leaders[0]["observed_ms"] == 330000,
        "KPM uses weighted same-session coverage and excludes repeat and short plays");
  const auto &pairs = report["soundtracks"][0]["apps"];
  check(pairs.size() == 2 && pairs[0]["name"] == "Editor" && pairs[0]["overlap_ms"] == 247500.0 &&
        pairs[1]["name"] == "Browser" && pairs[1]["overlap_ms"] == 82500.0 && report.dump().find("private-fixture") == std::string::npos,
        "foreground overlap is proportional and returns names without paths");
  options["merge"] = true;
  auto merged = db.music_insights(options, live);
  check(merged["leaderboard"][0]["played_ms"] == 330000 && merged["soundtracks"].size() == 2,
        "merged player observations are capped to one minute per song");
  options["group"] = "artists";
  const auto artists = db.music_insights(options, live);
  check(artists["leaderboard"].size() == 1 && artists["leaderboard"][0]["observed_ms"] == 570000,
        "canonical artist combines its songs");
  options["group"] = "songs"; options["merge"] = false;
  options["player"] = "netease";
  auto one_player = db.music_insights(options, live);
  check(one_player["leaderboard"].empty() && one_player["soundtracks"].size() == 1 &&
        one_player["soundtracks"][0]["player"] == "netease", "explicit player scope");
  options["player"] = "qq";
  live.session = "work";
  live.input_floor = live.music_floor = live.usage_floor = first + 5;
  live.music = Json::array({track(first + 5, 60000)});
  live.input = Json::array({Json::array({first + 5, 240, 60000})});
  live.usage = Json::array({app(first + 5, "Editor", 60000)});
  const auto latest = db.music_insights(options, live);
  check(latest["leaderboard"].size() == 1 && latest["leaderboard"][0]["observed_ms"] == 360000 &&
        latest["leaderboard"][0]["kpm"] == 140.0 &&
        latest["soundtracks"][0]["apps"][0]["overlap_ms"] == 285000.0,
        "live snapshot replaces the persisted tail of every stream");
  live.music = Json::array({track(first + 5, 30000)});
  live.input = Json::array({Json::array({first + 5, 40, 20000})});
  const auto partial = db.music_insights(options, live);
  check(partial["leaderboard"][0]["observed_ms"] == 310000 &&
        partial["leaderboard"][0]["kpm"] == 120.0, "partial playback and coverage are weighted within the minute");
 }
 CoreService core(dir);
 core.start();
 auto disabled = rpc(core, "get_music_insights", options);
 check(disabled["leaderboard"].empty() && disabled["soundtracks"].empty() && disabled["trend"].empty(),
       "disabled opt-in hides stored correlations");
 rpc(core, "set_config", {{"music_enabled", true}});
 const auto report = rpc(core, "get_music_insights", options);
 check(report["leaderboard"][0]["kpm"] == 120.0 && report["trend"].size() == 11 &&
       report["trend"][10]["presses"] == 0, "minute trend represents input and observed zero, excluding gaps");
 options["bucket"] = 60;
 check(rpc(core, "get_music_insights", options)["trend"].size() == 1, "single-day hourly trend");
 options["to"] = first + 1440;
 check(rpc(core, "get_music_insights", options)["trend"].empty(), "multi-day range has no marker trend");
 options["player"] = "anything";
 rejects([&] { rpc(core, "get_music_insights", options); }, "unknown insight filter rejected");
 core.stop();
}
}
int main(){try{
 check(music_player("QQMusic.exe")=="qq"&&music_player("cloudmusic.exe")=="netease","source identities");check(music_player("notqqmusic.exe").empty(),"unrelated players excluded");
 MusicObservation song{"one","qq","Track","Artist","Album",1,0,120000,true};
 auto other=song;other.album="Live";check(music_identity(song)!=music_identity(other),"same name different album");other=song;other.duration_ms++;check(music_identity(song)!=music_identity(other),"different duration version");other=song;other.title="  TRACK  ";check(music_identity(song)==music_identity(other),"normalized title");other=song;other.artist="";auto missing=other;missing.session="another";check(music_identity(other)!=music_identity(missing),"ambiguous metadata never merged across sessions");
 MusicTimeline t;std::uint64_t tick=1000;std::int64_t utc=600000;
 auto observe=[&](int elapsed=5000){tick+=elapsed;utc+=elapsed;t.observe({song},tick,utc,30);};
 {
  MusicTimeline continuous;auto s=song;continuous.observe({s},1000,600000,10);continuous.observe({s},6000,605000,10);continuous.observe({s},6000,605000,10);continuous.observe({s},11000,610000,10);
  check(total(continuous.rows(),"plays")==1,"duplicate notifications keep continuous threshold");
  continuous.clear();continuous.observe({s},1000,600000,10);continuous.observe({s},6000,605000,10);s.playing=false;continuous.observe({s},7000,606000,10);s.playing=true;continuous.observe({s},8000,607000,10);continuous.observe({s},13000,612000,10);
  check(total(continuous.rows(),"plays")==0,"pause resets unqualified continuous time");continuous.observe({s},18000,617000,10);check(total(continuous.rows(),"plays")==1,"custom threshold after resume");
  s.title="Next";continuous.observe({s},23000,622000,10);check(total(continuous.rows(),"plays")==1,"track change starts a new run");
  continuous.observe({s},28000,627000,10);continuous.observe({s},33000,632000,10);check(total(continuous.rows(),"plays")==2,"playlist track counts separately");
  continuous.clear();s.position_ms=-1;s.duration_ms=-1;continuous.observe({s},1000,600000,10);continuous.observe({s},6000,605000,10);continuous.observe({s},11000,610000,10);check(total(continuous.rows(),"plays")==1,"missing timeline still counts reported playing time");
 }
 t.observe({song},tick,utc,30);for(int i=0;i<6;i++){song.position_ms+=5000;observe();}
 check(total(t.rows(),"played_ms")==30000&&total(t.rows(),"plays")==1,"30s detected playback qualifies once");
 t.observe({song,song},tick,utc,30);check(total(t.rows(),"played_ms")==30000,"duplicate session notification");
 song.playing=false;observe();observe();check(total(t.rows(),"played_ms")==30000,"pause never credits elapsed gap");song.playing=true;observe();for(int i=0;i<6;i++){song.position_ms+=5000;observe();}check(total(t.rows(),"plays")==1,"resume of qualified song not counted again");
 auto before=total(t.rows(),"played_ms");song.position_ms=100000;observe();check(total(t.rows(),"played_ms")==before,"forward seek never credits jump");song.position_ms=20000;observe();check(total(t.rows(),"played_ms")==before,"rewind never credits jump");
 observe(3600000);check(total(t.rows(),"played_ms")==before,"sleep gap excluded");t.reset();observe();check(total(t.rows(),"played_ms")==before,"reset establishes new baseline");for(int i=0;i<6;i++)observe();check(total(t.rows(),"plays")==1,"lock wake does not duplicate qualified play");
 t.clear();tick=1000;utc=600000;song.position_ms=0;t.observe({song},tick,utc,30);for(int i=0;i<23;i++){song.position_ms+=5000;observe();}song.position_ms=0;observe();for(int i=0;i<6;i++){song.position_ms+=5000;observe();}check(total(t.rows(),"plays")==2,"observed end to start loop qualifies again");
 t.clear();tick=1000;utc=659000;song.position_ms=0;t.observe({song},tick,utc,30);song.position_ms=5000;observe();check(t.rows().size()==2&&total(t.rows(),"played_ms")==5000,"minute splitting has no loss");
 t.clear();tick=1000;utc=600000;other=song;other.session="two";other.player="netease";t.observe({song,other},tick,utc,30);for(int i=0;i<6;i++){tick+=5000;utc+=5000;t.observe({song,other},tick,utc,30);}check(total(t.rows(),"played_ms")==60000&&total(t.rows(),"plays")==2,"simultaneous players count independently");
 before=total(t.rows(),"played_ms");tick+=5000;utc-=3600000;t.observe({song,other},tick,utc,30);check(total(t.rows(),"played_ms")==before,"wall clock rollback excluded");
 t.observe({},tick+1000,utc+1000,30);t.observe({song},tick+5000,utc+5000,30);check(total(t.rows(),"played_ms")==before,"exit and reopen creates baseline");
 const auto root=std::filesystem::absolute("data/tests/music-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64()));std::filesystem::create_directories(root);
 const auto rows=t.rows();Json batch={{"format",1},{"session","123-456"},{"revision",1},{"created_ms",utc_now_ms()},{"rows",Json::array()},{"events",Json::array()},{"usage",Json::array()},{"rankings",Json::array()},{"music",rows}};
 {DatabaseManager db(root/"monitor.db");check(db.migration_info()["to"]==9,"schema migration to 9");check(db.save_batch(batch),"music saved in aggregate transaction");check(!db.save_batch(batch),"repeated revision idempotent");check(total(db.music(0,4223371680LL),"played_ms")==before,"saved aggregates consistent");check(db.music(0,4223371680LL,"123-456",0).empty(),"live persisted rows excluded");db.backup(root/"copy.db");}
 {DatabaseManager db(root/"monitor.db"),backup(root/"copy.db");check(db.music(0,4223371680LL)==backup.music(0,4223371680LL),"online backup preserves music");check(total(db.music(0,4223371680LL),"plays")==2,"restart does not replay plays");auto bad=rows;bad[0]["played_ms"]=-1;rejects([&]{db.save_music("other",bad);},"invalid durations rejected atomically");db.clear();check(db.music(0,4223371680LL).empty(),"data clear includes music");}
 {sqlite3 *legacy{};sqlite3_open((root/"monitor.db").string().c_str(),&legacy);sqlite3_exec(legacy,"DROP TABLE music_minutes;INSERT INTO counts VALUES('legacy',100,'keyboard','30',42);PRAGMA user_version=8",nullptr,nullptr,nullptr);sqlite3_close(legacy);}
 {DatabaseManager migrated(root/"monitor.db");check(migrated.migration_info()["from"]==8&&migrated.migration_info()["to"]==9,"additive schema 8 migration");check(migrated.query(100,101,1)[0]["value"]==42,"schema 8 records retained");check(migrated.music(0,101).empty(),"migration creates no invented music history");}
 PendingStore pending(root/"pending");pending.stage(batch);check(pending.read(pending.files()[0])["music"]==rows,"crash journal includes music");
 std::filesystem::create_directories(root/"config");
 {CoreService core(root/"config");check(!core.status()["config"]["music_enabled"].get<bool>(),"music opt-in default");core.start();rpc(core,"set_config",{{"music_threshold_seconds",45},{"music_qq",false}});rejects([&]{rpc(core,"set_config",{{"music_threshold_seconds",0}});},"threshold validation");auto result=rpc(core,"get_music",{{"from",0},{"to",4223371680LL}});check(result["rows"].empty()&&!result["status"]["active"].get<bool>(),"disabled music has empty state");auto file=rpc(core,"export_music",{{"from",0},{"to",4223371680LL}});check(std::filesystem::exists(root/"config"/file["file"].get<std::string>()),"CSV export created");core.stop();}
 std::filesystem::create_directories(root/"config");
 {CoreService core(root/"config");check(core.status()["config"]["music_threshold_seconds"]==45&&!core.status()["config"]["music_qq"].get<bool>(),"configuration survives restart");}
 test_insights(root);
 std::cout<<checks<<" music timeline, database, recovery and bridge checks passed\n";return 0;
 }catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
