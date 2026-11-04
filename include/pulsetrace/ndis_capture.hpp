#pragma once
#include "pulsetrace/json.hpp"
#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
namespace pulsetrace {
struct CaptureWindow {
    std::atomic<std::int64_t> start{}, stop{};
};
std::int64_t capture_clock() noexcept;
class NdisCapture {
  public:
    explicit NdisCapture(CaptureWindow *window = nullptr);
    ~NdisCapture();
    void configure(bool enabled, unsigned index, std::uint64_t luid, const std::array<std::uint8_t, 6> &mac);
    Json snapshot(std::int64_t minute, Json &rows);

  private:
    struct Impl;
    std::unique_ptr<Impl> p_;
};
} // namespace pulsetrace
