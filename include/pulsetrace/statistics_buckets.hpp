#pragma once
#include <cstdint>
#include <windows.h>

namespace pulsetrace {
// Statistics retain UTC minute keys. Local buckets return the UTC instant at
// the beginning of the corresponding civil day or occurrence of a civil hour.
class StatisticsBuckets {
  public:
    explicit StatisticsBuckets(int width, bool local);
    StatisticsBuckets(int width, const DYNAMIC_TIME_ZONE_INFORMATION &zone);
    std::int64_t start(std::int64_t minute);

  private:
    int width_;
    bool local_;
    DYNAMIC_TIME_ZONE_INFORMATION zone_{};
    std::int64_t first_{1}, end_{0};
};
}
