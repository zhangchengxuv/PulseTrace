#pragma once
#include "pulsetrace/json.hpp"
#include <map>
#include <memory>
#include <string>
#include <vector>
namespace pulsetrace {
struct MusicObservation {
    std::string session,player,title,artist,album;
    int track{};
    std::int64_t position_ms{-1},duration_ms{-1};
    bool playing{};
};
std::string music_identity(const MusicObservation &song);
std::string music_player(std::string source);
// Only aggregated minutes leave this state machine; observations remain in memory.
class MusicTimeline {
 public:
    void observe(const std::vector<MusicObservation>&,std::uint64_t tick,std::int64_t utc_ms,unsigned threshold);
    void reset();
    void clear();
    Json rows()const;
    void persisted_before(std::int64_t minute);
 private:
    struct Session {MusicObservation song;std::string key;std::uint64_t tick{},continuous{};std::int64_t utc{};bool counted{};};
    std::map<std::string,Session> sessions_;
    std::map<std::string,Json> buckets_;
};
class MusicMonitor {
 public:
    MusicMonitor();~MusicMonitor();
    void configure(const Json&,bool monitoring);
    void invalidate() noexcept;
    void stop();void clear();
    Json rows()const;Json status()const;
    void persisted_before(std::int64_t minute);
 private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
}
