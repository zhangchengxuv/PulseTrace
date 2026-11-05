#include <winsock2.h>
#include <ws2tcpip.h>
#include "pulsetrace/traffic_rankings.hpp"
#include "pulsetrace/core_service.hpp"
#include "pulsetrace/protocol.hpp"
#include <sqlite3.h>
#include <atomic>
#include <fstream>
#include <future>
#include <iostream>
#include <thread>
using namespace pulsetrace;
namespace {
unsigned checks{};
void check(bool condition,const char *message){++checks;if(!condition)throw std::runtime_error(message);}
template<class F>void rejects(F f,const char *message){bool threw=false;try{f();}catch(...){threw=true;}check(threw,message);}
Json connection(std::string id,std::string path,std::string host,std::string ip,std::uint64_t up,std::uint64_t down){return {{"id",id},{"start","2026-01-01T00:00:00Z"},{"upload",up},{"download",down},{"metadata",{{"processPath",path},{"process","fixture.exe"},{"host",host},{"destinationIP",ip},{"network","tcp"}}}};}
Json document(Json connections,std::uint64_t total=100000000){return {{"uploadTotal",total},{"downloadTotal",total},{"connections",connections}};}
std::uint64_t total(const Json &rows,const std::string &kind,const char *field){std::uint64_t n=0;for(const auto &r:rows)if(r["kind"]==kind)n+=r[field].get<std::uint64_t>();return n;}
void model(){
 rejects([]{validate_request({{"type","get_rankings"},{"request_id",1},{"params",{{"from",9223372036854775808ULL},{"to",9223372036854775809ULL},{"source","mihomo"},{"kind","app"},{"family",0}}}});},"oversized integer range rejected before signed conversion");
 {MihomoDeltaTracker empty;RankingAccumulator out;auto doc=document(nullptr);check(empty.consume(doc,1000,true,out)["connections"]==0&&out.rows().empty(),"real Mihomo null connections means idle, not malformed");}
 {wchar_t exe[32768]{};GetModuleFileNameW(nullptr,exe,32768);const auto path=(std::filesystem::path(exe).parent_path()/"PulseTrace.exe").u8string();const auto icon=application_icon(std::string(path.begin(),path.end()));check(icon.starts_with("data:image/png;base64,")&&icon.find('\0')==std::string::npos,"real executable icon encoded without NUL");}
 {MihomoDeltaTracker fresh;RankingAccumulator live;fresh.consume(document(Json::array()),60000,true,live);auto c=connection("fresh","C:\\Fixture\\x.exe","fresh.example","127.0.0.1",10,20);c["start"]="1970-01-01T08:01:01.500000000+08:00";fresh.consume(document(Json::array({c})),62000,true,live);check(total(live.rows(),"app","upload")==10&&total(live.rows(),"app","download")==20,"verified new start counts observed lifetime from zero");c["id"]="unverified";c["start"]="not-a-date";const auto before=live.rows();fresh.consume(document(Json::array({c})),64000,true,live);check(live.rows()==before,"unverified new start is conservative baseline");}
 check(process_lifetime_contains(100,200,110,190),"verified process lifetime");check(!process_lifetime_contains(200,0,110,190),"reused PID cannot claim earlier traffic");check(!process_lifetime_contains(100,200,110,250),"minute spanning PID reuse unresolved");check(!process_lifetime_contains(0,0,110,190),"unverified process birth unresolved");
 check(ranking_identity("C:/Apps/Tool.exe").key==ranking_identity("c:\\Apps\\tool.exe").key,"case and slash identity");
 check(ranking_identity("C:\\Apps\\x\\..\\tool.exe").key==ranking_identity("C:\\Apps\\tool.exe").key,"lexical stable executable identity");
 check(ranking_identity("C:\\Windows\\System32\\svchost.exe").attribution=="shared_service_process","shared service attribution");check(ranking_identity("","tool.exe").attribution=="reported_name_only","name is ambiguous");check(ranking_identity("\\\\remote\\share\\tool.exe").key=="unknown","no remote icon identity");
 check(ranking_domain("EXAMPLE.COM.")=="example.com","canonical domain");check(ranking_domain("xn--bcher-kva.de")=="xn--bcher-kva.de","IDN ASCII");for(const char *s:{"","1.2.3.4","::1","https://example.com/x","example.com/path","user@example.com","bad..domain","-bad.example","bad.example:443"})check(ranking_domain(s).empty(),"reject unreliable domain");
 for(const char *s:{"http://127.0.0.1:9090","https://localhost:1","http://[::1]:65535"})validate_controller(s);
 for(const char *s:{"https://example.com:443","http://127.0.0.1:80/x","http://user@127.0.0.1:80","file://x","http://127.0.0.1","http://127.0.0.1:0","http://127.0.0.1:65536","http://127.0.0.1:80?x"})rejects([&]{validate_controller(s);},"restricted origin validation");
 MihomoDeltaTracker tracker;RankingAccumulator bins;auto a=connection("a","C:\\Apps\\one.exe","a.example","10.0.0.1",100,200),b=connection("b","c:/Apps/ONE.exe","a.example","2001:db8::1",5,10);
 auto d=document(Json::array({a,b}));tracker.consume(d,60000,true,bins);check(bins.rows().empty(),"first sample baseline excludes old totals");
 a["upload"]=130;a["download"]=250;b["upload"]=15;b["download"]=30;d=document(Json::array({a,b}));tracker.consume(d,62000,true,bins);
 check(total(bins.rows(),"app","upload")==40&&total(bins.rows(),"app","download")==70,"exact observed counter deltas");check(total(bins.rows(),"domain","download")==70,"reported domains only");check(bins.rows().size()==4,"two families and two kinds");
 a["upload"]=2;a["download"]=260;auto result=tracker.consume(document(Json::array({a,b})),64000,true,bins);check(result["counter_resets"]==1,"directional reset diagnostic");check(total(bins.rows(),"app","upload")==40&&total(bins.rows(),"app","download")==80,"reset never turns cumulative into transfer");
 result=tracker.consume(document(Json::array({b})),66000,true,bins);check(result["vanished_connections"]==1&&!result["closed_tail_observed"].get<bool>(),"closed tails unknown");
 tracker.interrupt();b["download"]=35;result=tracker.consume(document(Json::array({b})),80000,true,bins);check(result["gap"]==true,"outage marked incomplete");
 b["start"]="2026-01-02T00:00:00Z";b["download"]=400;const auto before=total(bins.rows(),"app","download");result=tracker.consume(document(Json::array({b})),82000,true,bins);check(result["baseline_connections"]==1&&total(bins.rows(),"app","download")==before,"ID reuse new start rebased");
 result=tracker.consume(document(Json::array({b}),1),84000,true,bins);check(result["core_restart"]==true&&result["baseline_connections"]==1,"global counters restarting rebased");
 auto malformed=document(Json::array({b,b}));const auto old=bins.rows();rejects([&]{tracker.consume(malformed,86000,true,bins);},"duplicate connection rejected atomically");check(bins.rows()==old,"invalid snapshot cannot partly accumulate");
 b["metadata"]["host"]="";b["metadata"]["processPath"]="";b["metadata"]["process"]="";tracker.consume(document(Json::array({b})),88000,true,bins);b["download"]=410;tracker.consume(document(Json::array({b})),90000,true,bins);bool unknown=false;for(const auto &r:bins.rows())unknown|=r["kind"]=="domain"&&r["key"]=="unknown";check(unknown,"unknown destination bucket");
 tracker.reset();const auto sum=total(bins.rows(),"app","download");tracker.consume(document(Json::array({b})),100000,true,bins);check(sum==total(bins.rows(),"app","download"),"pause/resume rebaseline");
 b["download"]=430;const auto domain_before=total(bins.rows(),"domain","download");tracker.consume(document(Json::array({b})),102000,false,bins);check(total(bins.rows(),"domain","download")==domain_before,"domain opt out stops history");
 bins.clear_domains();check(total(bins.rows(),"domain","download")==0&&total(bins.rows(),"app","download")>0,"separate domain clear");
 auto bad=b;bad["upload"]=-1;rejects([&]{tracker.consume(document(Json::array({bad})),104000,true,bins);},"negative byte counter rejected");bad["upload"]=9007199254740992ULL;rejects([&]{tracker.consume(document(Json::array({bad})),104000,true,bins);},"non-exact JSON counter rejected");
 for(const char *type:{"get_rankings","get_ranking_icon","set_mihomo_secret","get_ranking_status","clear_ranking_domains"}){
  Json p=type==std::string("get_rankings")?Json{{"from",1},{"to",2},{"source","mihomo"},{"kind","app"},{"family",0}}:type==std::string("get_ranking_icon")?Json{{"source","mihomo"},{"key","exe:c:\\fixture.exe"}}:type==std::string("set_mihomo_secret")?Json{{"secret","fixture"}}:type==std::string("clear_ranking_domains")?Json{{"confirmation","DELETE"}}:Json::object();validate_request({{"type",type},{"request_id",1},{"params",p}});p["unexpected"]=true;rejects([&]{validate_request({{"type",type},{"request_id",1},{"params",p}});},"strict ranking bridge parameters");}
}
struct Server {
 SOCKET listener{INVALID_SOCKET};unsigned short port{};std::thread worker;std::atomic<bool> quit{false};std::atomic<unsigned> calls{},value{0};std::atomic<int> mode{0};
 Server(){listener=socket(AF_INET,SOCK_STREAM,0);sockaddr_in address{};address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);check(bind(listener,reinterpret_cast<sockaddr*>(&address),sizeof(address))==0,"loopback fixture bind");int size=sizeof(address);getsockname(listener,reinterpret_cast<sockaddr*>(&address),&size);port=ntohs(address.sin_port);listen(listener,4);
  worker=std::thread([this]{while(!quit){fd_set set;FD_ZERO(&set);FD_SET(listener,&set);timeval timeout{0,100000};if(select(0,&set,nullptr,nullptr,&timeout)<=0)continue;SOCKET client=accept(listener,nullptr,nullptr);if(client==INVALID_SOCKET)continue;DWORD wait=1000;setsockopt(client,SOL_SOCKET,SO_RCVTIMEO,reinterpret_cast<char*>(&wait),sizeof(wait));char request[4096]{};int got=recv(client,request,sizeof(request)-1,0);++calls;
   const std::string text(request,got>0?got:0);const bool authorized=text.find("Authorization: Bearer fixture-secret")!=std::string::npos;const bool route=text.starts_with("GET /connections HTTP/");
   const auto current=value.fetch_add(100);auto c=connection("controlled","C:\\Fixture\\client.exe","controlled.example","127.0.0.1",current,current*2);const auto body=mode==2?std::string("bad-json"):document(Json::array({c})).dump();
   const int status=!authorized||mode==1?401:!route?404:mode==3?302:200;std::string response="HTTP/1.1 "+std::to_string(status)+" Fixture\r\nConnection: close\r\nContent-Type: application/json\r\nLocation: http://192.0.2.1/forbidden\r\nContent-Length: "+std::to_string(body.size())+"\r\n\r\n"+body;send(client,response.data(),static_cast<int>(response.size()),0);closesocket(client);
  }});
 }
 ~Server(){quit=true;if(worker.joinable())worker.join();closesocket(listener);}
};
template<class F>bool wait(F f,int timeout=6500){const auto end=GetTickCount64()+timeout;while(GetTickCount64()<end){if(f())return true;Sleep(50);}return false;}
Json rpc(CoreService &core,const std::string &type,const Json &p=Json::object()){
 auto result=std::make_shared<std::promise<Json>>();auto f=result->get_future();core.request({{"type",type},{"request_id",1},{"params",p}},[result](Json r){result->set_value(std::move(r));});if(f.wait_for(std::chrono::seconds(8))!=std::future_status::ready)throw std::runtime_error("ranking core request timeout");auto r=f.get();if(!r["ok"].get<bool>())throw std::runtime_error(r["error"]["message"]);return r["data"];
}
void persistence(const std::filesystem::path &root){
 const auto file=root/"monitor.db";RankingAccumulator bins;bins.add(100,"mihomo","app",ranking_identity("C:\\Fixture\\client.exe"),4,100,200,ranking_sampled);bins.add(100,"mihomo","domain",{"a.example","a.example","","mihomo_metadata_host"},4,100,200,ranking_sampled);bins.add(100,"windows_etw","app",ranking_identity("C:\\Fixture\\client.exe"),6,7,8);
 {DatabaseManager db(file);db.save("legacy",Json::array({{{"minute",100},{"module","mouse"},{"metric","left"},{"value",17}}}));}
 {sqlite3 *raw{};sqlite3_open(file.string().c_str(),&raw);check(sqlite3_exec(raw,"DROP TABLE traffic_rankings;DROP TABLE app_usage;PRAGMA user_version=3",nullptr,nullptr,nullptr)==SQLITE_OK,"schema v3 fixture");sqlite3_close(raw);}
 {DatabaseManager db(file);check(db.migration_info()["from"]==3&&db.migration_info()["to"]==DatabaseManager::supported_schema,"v3 to current migration");check(db.query(100,101,1)[0]["value"]==17,"old counts preserved");check(std::filesystem::exists(std::filesystem::u8path(db.migration_info()["backup"].get<std::string>())),"online migration backup");db.save_rankings("fixture",bins.rows());db.save_rankings("fixture",bins.rows());check(db.rankings(100,101,"mihomo","app")[0]["upload"]==100,"UPSERT idempotent");check(db.rankings(100,101,"windows_etw","app")[0]["upload"]==7,"sources remain independent");check(db.rankings(101,102,"mihomo","app").empty(),"half-open custom range");check(db.rankings(100,101,"mihomo","app","fixture",100).empty(),"live overlay exclusion");db.backup(root/"ranking-backup.db");}
 {CoreService core(root);core.start();const Json p={{"from",100},{"to",101},{"source","mihomo"},{"kind","app"},{"family",0}};auto result=rpc(core,"get_rankings",p);check(result["rows"].size()==1&&result["rows"][0]["total"]==300,"real bridge historical query");check(result["rows"][0]["up_bps"].is_null(),"paused rates unavailable, not zero");check(!rpc(core,"get_status")["monitoring"].get<bool>(),"default remains paused");rpc(core,"clear_ranking_domains",{{"confirmation","DELETE"}});check(rpc(core,"get_rankings",{{"from",100},{"to",101},{"source","mihomo"},{"kind","domain"},{"family",0}})["rows"].empty(),"domain history cleared through bridge");check(rpc(core,"get_rankings",p)["rows"].size()==1,"domain clear preserves apps");core.stop();}
 {DatabaseManager db(file);check(db.rankings(100,101,"mihomo","app").size()==1,"restart persistence");db.prune_rankings(101);check(db.rankings(100,101,"mihomo","app").empty()&&db.query(100,101,1).size()==1,"ranking retention independent of counts");}
}
void provider(const std::filesystem::path &root){
 Server server;TrafficRankings service(root);Json cfg={{"ranking_mihomo_enabled",true},{"ranking_domains_enabled",true},{"mihomo_controller","http://127.0.0.1:"+std::to_string(server.port)}};
 Sleep(100);check(server.calls==0,"disabled provider makes no request");service.set_secret("fixture-secret");std::ifstream encrypted(root/"mihomo.secret",std::ios::binary);const std::string ciphertext((std::istreambuf_iterator<char>(encrypted)),{});encrypted.close();check(!ciphertext.empty()&&ciphertext.find("fixture-secret")==std::string::npos,"DPAPI credential not plaintext");check(service.status().dump().find("fixture-secret")==std::string::npos,"status never leaks credential");
 service.configure(cfg,true);check(wait([&]{return total(service.rows(),"app","download")>=200;}),"real loopback HTTP polling delta");check(total(service.rows(),"domain","download")>0,"explicit domain history");check(!service.rates().empty(),"current sampled rates");
 service.configure(cfg,false);const auto paused=service.rows();Sleep(2200);check(service.rows()==paused&&service.rates().empty(),"pause excludes data and rates");
 server.mode=1;service.configure(cfg,true);check(wait([&]{return service.status().value("mihomo",Json::object()).value("state","")=="controller_auth";}),"authentication rejection surfaced");
 server.mode=3;check(wait([&]{return service.status().value("mihomo",Json::object()).value("state","")=="controller_http";}),"redirect never followed");
 server.mode=2;check(wait([&]{return service.status().value("mihomo",Json::object()).value("state","")=="controller_document";}),"malformed response surfaced");
 server.mode=0;check(wait([&]{return service.status().value("mihomo",Json::object()).value("state","")=="observing";}),"API recovers after outage");
 service.configure(cfg,false);service.clear(true);Sleep(2200);check(total(service.rows(),"domain","download")==0&&total(service.rows(),"app","download")>0,"clear cannot resurrect in-flight domain data");
 service.stop();service.set_secret("");check(!std::filesystem::exists(root/"mihomo.secret"),"explicit credential removal");
}
}
int main(){try{WSADATA w{};WSAStartup(MAKEWORD(2,2),&w);const auto root=std::filesystem::current_path()/"data"/"tests"/("rankings-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64()));std::filesystem::create_directories(root);model();persistence(root);const auto service=root/"provider";std::filesystem::create_directories(service);provider(service);WSACleanup();std::cout<<checks<<" ranking model, migration, bridge and loopback provider checks passed\n";return 0;}catch(const std::exception &e){std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';return 1;}}
