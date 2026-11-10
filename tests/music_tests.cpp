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
 std::cout<<checks<<" music timeline, database, recovery and bridge checks passed\n";return 0;
 }catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
