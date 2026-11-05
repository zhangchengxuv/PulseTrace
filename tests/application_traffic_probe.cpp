#include "pulsetrace/traffic_rankings.hpp"
#include <windows.h>
#include <psapi.h>
#include <iostream>
static std::uint64_t private_bytes(){PROCESS_MEMORY_COUNTERS_EX p{};GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&p),sizeof(p));return p.PrivateUsage;}
static std::uint64_t cpu_ticks(){FILETIME c{},e{},k{},u{};GetProcessTimes(GetCurrentProcess(),&c,&e,&k,&u);return ((std::uint64_t(k.dwHighDateTime)<<32)|k.dwLowDateTime)+((std::uint64_t(u.dwHighDateTime)<<32)|u.dwLowDateTime);}
int main(int argc,char **argv){
 try{unsigned seconds=5;bool etw=false,domains=false;std::string origin;
  for(int i=1;i<argc;++i){const std::string arg=argv[i];if(arg=="--etw")etw=true;else if(arg=="--domains")domains=true;else if(arg=="--mihomo"&&i+1<argc)origin=argv[++i];else if(arg=="--seconds"&&i+1<argc){const std::string n=argv[++i];if(n.empty()||n.find_first_not_of("0123456789")!=std::string::npos)throw std::invalid_argument("Invalid duration");seconds=std::stoul(n);if(!seconds||seconds>60)throw std::invalid_argument("Duration must be 1..60 seconds");}else throw std::invalid_argument("Invalid option");}
  if(!etw&&origin.empty()){std::cout<<"Usage: application_traffic_probe --etw [--seconds 1..60]\n       application_traffic_probe --mihomo http://127.0.0.1:PORT [--domains] [--seconds 1..60]\nMihomo authentication: explicitly set PULSETRACE_MIHOMO_SECRET for this process.\nOnly aggregate results are printed; protect output containing application paths/domains.\n";return 0;}
  if(etw){pulsetrace::RankingAccumulator bins;pulsetrace::ProcessTrafficCapture capture;capture.gate(true);capture.configure(true);Sleep(seconds*1000);auto result=capture.drain(bins);capture.configure(false);capture.drain(bins);result["rows"]=bins.rows();std::cout<<result.dump(2)<<'\n';if(result["state"]!="observing")return 2;}
  if(!origin.empty()){
   pulsetrace::validate_controller(origin);const auto directory=std::filesystem::temp_directory_path()/("PulseTrace-ranking-probe-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64()));std::filesystem::create_directories(directory);
   pulsetrace::TrafficRankings source(directory);char secret[1025]{};const auto length=GetEnvironmentVariableA("PULSETRACE_MIHOMO_SECRET",secret,sizeof(secret));if(length>=sizeof(secret))throw std::invalid_argument("Credential too long");if(length)source.set_secret(secret);SecureZeroMemory(secret,sizeof(secret));
   const auto begin=GetTickCount64(),start_cpu=cpu_ticks(),initial=private_bytes();auto peak=initial;
   source.configure({{"ranking_mihomo_enabled",true},{"ranking_domains_enabled",domains},{"mihomo_controller",origin}},true);const auto end=begin+seconds*1000ULL;bool ready=false;while(GetTickCount64()<end){if(!ready&&source.status().value("mihomo",pulsetrace::Json::object()).value("state","")=="observing"){ready=true;std::cerr<<"PROBE_READY\n";}peak=std::max(peak,private_bytes());Sleep(50);}source.stop();auto result=source.status();result["rows"]=source.rows();result["probe_resources"]={{"elapsed_ms",GetTickCount64()-begin},{"cpu_core_percent",100.0*(cpu_ticks()-start_cpu)/((GetTickCount64()-begin)*10000.0)},{"private_bytes_initial",initial},{"private_bytes_final",private_bytes()},{"private_bytes_peak",peak}};std::cout<<result.dump(2)<<'\n';source.set_secret("");std::filesystem::remove(directory);if(result.value("mihomo",pulsetrace::Json::object()).value("state","")!="observing")return 2;
  }return 0;
 }catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}
}
