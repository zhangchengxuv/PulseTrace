#pragma once
#include "pulsetrace/json.hpp"
#include <cstdint>
#include <map>
#include <string>
namespace pulsetrace {
struct NetworkSample {
    Json status;
    Json rows = Json::array(), events = Json::array();
};
class NetworkMonitor {
  public:
    NetworkSample sample(std::int64_t minute, bool enabled, const std::string &selected);

  private:
    struct Previous {
        std::uint64_t in{}, out{};
        bool up{};
        std::string name;
    };
    std::map<std::string, Previous> previous_;
    std::map<std::pair<std::int64_t, std::string>, std::uint64_t> totals_;
    std::string last_selected_;
    std::uint64_t last_tick_{};
    bool was_enabled_{};
    std::uint64_t sequence_{};
};
} // namespace pulsetrace
