#pragma once
#include "pulsetrace/json.hpp"
#include <map>
#include <memory>
#include <string>
#include <cstdint>
#include <filesystem>
namespace pulsetrace {
struct RankingIdentity {std::string key,name,path,attribution;};
RankingIdentity ranking_identity(const std::string &path,const std::string &name={});
std::string ranking_domain(const std::string &host);
bool process_lifetime_contains(std::uint64_t birth,std::uint64_t exit,std::uint64_t first,std::uint64_t last)noexcept;
void validate_controller(const std::string &url);
class RankingAccumulator {
 public:
  void add(std::int64_t minute,const std::string &source,const std::string &kind,const RankingIdentity &id,int family,std::uint64_t up,std::uint64_t down,unsigned quality=0);
  Json rows()const;
  void discard_before(std::int64_t minute);
  void clear_domains();void clear();
  std::uint64_t capacity_events()const noexcept{return capacity_events_;}
 private:std::map<std::string,Json> bins_;std::uint64_t capacity_events_{};
};
enum RankingQuality:unsigned {ranking_sampled=1,ranking_gap=2,ranking_reset=4,ranking_unresolved=8,ranking_loss=16,ranking_capacity=32};
class MihomoDeltaTracker {
 public:
  Json consume(const Json &document,std::int64_t time_ms,bool domains,RankingAccumulator &out);
  void interrupt();void reset();
 private:
  struct Connection {std::uint64_t up{},down{};std::string fingerprint;};
  std::map<std::string,Connection> previous_;
  std::int64_t last_ms_{};std::uint64_t upload_total_{},download_total_{};bool gap_{};
};
class ProcessTrafficCapture {
 public:
  ProcessTrafficCapture();~ProcessTrafficCapture();
  void configure(bool enabled);
  void gate(bool enabled) noexcept;
  Json drain(RankingAccumulator &out);
 private:struct Impl;std::unique_ptr<Impl> p_;
};
class TrafficRankings {
 public:
  explicit TrafficRankings(const std::filesystem::path &data);~TrafficRankings();
  void configure(const Json &config,bool monitoring);
  void stop();Json status()const;Json rows()const;Json rates()const;
  void persisted_before(std::int64_t minute);
  void clear(bool domains_only);void set_secret(const std::string &secret);
 private:struct Impl;std::unique_ptr<Impl> p_;
};
std::string application_icon(const std::string &local_path);
}
