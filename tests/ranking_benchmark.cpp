#include <windows.h>
#include <psapi.h>
#include "pulsetrace/traffic_rankings.hpp"
#include "pulsetrace/database.hpp"
#include <chrono>
#include <iostream>
using namespace pulsetrace;
static std::uint64_t ticks(FILETIME f){return(std::uint64_t(f.dwHighDateTime)<<32)|f.dwLowDateTime;}
static std::uint64_t cpu(){FILETIME c{},e{},k{},u{};GetProcessTimes(GetCurrentProcess(),&c,&e,&k,&u);return ticks(k)+ticks(u);}
static std::uint64_t memory(){PROCESS_MEMORY_COUNTERS_EX p{};GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&p),sizeof(p));return p.PrivateUsage;}
int main(int argc,char **argv){try{const auto seconds=argc==2?std::clamp(atoi(argv[1]),1,60):10;const auto dir=std::filesystem::current_path()/"data"/"tests"/("ranking-benchmark-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64()));std::filesystem::create_directories(dir);DatabaseManager db(dir/"fixture.db");RankingAccumulator bins;MihomoDeltaTracker tracker;Json doc={{"uploadTotal",1000000000},{"downloadTotal",1000000000},{"connections",Json::array()}};
 for(int i=0;i<32;++i)doc["connections"].push_back({{"id",std::to_string(i)},{"start","2026-01-01T00:00:00Z"},{"upload",0},{"download",0},{"metadata",{{"processPath","C:\\Fixture\\app-"+std::to_string(i%8)+".exe"},{"host","site-"+std::to_string(i%4)+".example"},{"destinationIP",i%2?"2001:db8::1":"127.0.0.1"}}}});
 const auto begin=GetTickCount64(),startcpu=cpu(),initial=memory();std::uint64_t peak=initial,warm=initial,last=begin,samples=0,bytes=0;std::int64_t virtualtime=1800000000000LL;
 tracker.consume(doc,virtualtime,true,bins);
 while(GetTickCount64()-begin<static_cast<std::uint64_t>(seconds)*1000){for(auto &c:doc["connections"]){c["upload"]=c["upload"].get<std::uint64_t>()+100;c["download"]=c["download"].get<std::uint64_t>()+200;}tracker.consume(doc,++virtualtime,true,bins);++samples;bytes+=32*300;
  if(GetTickCount64()-last>=1000){db.save_rankings("fixture",bins.rows());bins.discard_before(virtualtime/60000-119);peak=std::max(peak,memory());if(GetTickCount64()-begin<3000)warm=memory();last=GetTickCount64();}}
 db.save_rankings("fixture",bins.rows());const auto elapsed=GetTickCount64()-begin;auto rows=db.rankings(virtualtime/60000-119,virtualtime/60000+1,"mihomo","app");std::uint64_t sum=0;for(const auto &r:rows)sum+=r["upload"].get<std::uint64_t>()+r["download"].get<std::uint64_t>();if(sum!=bytes)throw std::runtime_error("Replay count mismatch");
 Json report={{"fixture_replay",true},{"samples",samples},{"connections_per_sample",32},{"count_match",true},{"elapsed_ms",elapsed},{"cpu_core_percent",100.0*(cpu()-startcpu)/(elapsed*10000.0)},{"private_bytes_initial",initial},{"private_bytes_warm",warm},{"private_bytes_final",memory()},{"private_bytes_peak",peak},{"aggregate_rows",bins.rows().size()},{"database_bytes",std::filesystem::file_size(dir/"fixture.db")},{"wal_bytes",std::filesystem::exists(dir/"fixture.db-wal")?std::filesystem::file_size(dir/"fixture.db-wal"):0},{"capacity_events",bins.capacity_events()}};std::cout<<report.dump(2)<<'\n';return 0;
 }catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
