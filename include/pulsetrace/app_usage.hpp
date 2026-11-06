#pragma once
#include "pulsetrace/json.hpp"
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <limits>
namespace pulsetrace {
// Transient observations are never persisted. Only minute aggregates leave this module.
struct UsageObservation {
    std::uint64_t tick{},last_input{};
    std::int64_t utc_ms{};
    std::string identity,name,path;
    bool valid{};
};
class UsageTimeline {
 public:
    void observe(const UsageObservation &sample,unsigned idle_seconds);
    void reset(){previous_={};has_previous_=false;}
    void clear(){reset();buckets_.clear();last_credited_utc_=std::numeric_limits<std::int64_t>::min();}
    Json rows() const;
    void persisted_before(std::int64_t minute);
    std::uint64_t gaps()const{return gaps_;}
 private:
    UsageObservation previous_{};bool has_previous_{};std::uint64_t gaps_{};
    std::uint64_t anchor_tick_{};
    std::int64_t anchor_utc_{},last_credited_utc_{std::numeric_limits<std::int64_t>::min()};
    std::map<std::pair<std::int64_t,std::string>,Json> buckets_;
};
class AppUsageMonitor {
 public:
    AppUsageMonitor();~AppUsageMonitor();
    void configure(const Json &config,bool monitoring);
    void stop();void clear();
    Json rows()const;Json status()const;
    void persisted_before(std::int64_t minute);
 private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
}
