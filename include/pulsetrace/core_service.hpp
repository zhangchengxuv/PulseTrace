#pragma once
#include "pulsetrace/database.hpp"
#include "pulsetrace/keyboard_monitor.hpp"
#include "pulsetrace/network_monitor.hpp"
#include "pulsetrace/monthly_traffic.hpp"
#include "pulsetrace/raw_input_host.hpp"
#include "pulsetrace/traffic_rankings.hpp"
#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
namespace pulsetrace {
class CoreService final : public InputCallbacks {
  public:
    explicit CoreService(std::filesystem::path data);
    ~CoreService();
    void start();
    void stop();
    Json status() const;
    void request(Json request, std::function<void(Json)> reply);
    bool capture_enabled() const noexcept override { return enabled_.load() && !session_locked_.load(); }
    void on_ready(HWND window) noexcept override;
    void on_keyboard(std::uintptr_t device, USHORT scan, USHORT flags, USHORT vk,
                     std::int64_t minute) noexcept override;
    void on_tick(const MouseMonitor &mouse) noexcept override;
    void on_reset() noexcept override;
    void on_input_metrics(std::uint64_t mouse, std::uint64_t keyboard,
                          std::uint64_t errors) noexcept override;
    void on_device_removed(std::uintptr_t device) noexcept override { keyboard_.remove_device(device); }
    void on_session_locked(bool locked) noexcept override {
        session_locked_.store(locked);
        keyboard_.reset_state();
        last_coverage_ms_ = 0;
    }

  private:
    void worker();
    Json execute(const Json &request, DatabaseManager &db);
    void save_config();
    void retry_final_flush();
    void disable_capture() noexcept {
        std::int64_t unset = 0;
        capture_window_.stop.compare_exchange_strong(unset, capture_clock());
        enabled_.store(false);
    }
    std::filesystem::path data_;
    std::unique_ptr<MonthlyTrafficStore> monthly_;
    std::unique_ptr<TrafficRankings> rankings_;
    Json unsaved_rankings_=Json::array();
    std::map<std::string,std::string> ranking_icons_;
    std::string session_;
    std::atomic<bool> enabled_{false}, stopping_{false};
    std::atomic<bool> session_locked_{false};
    std::atomic<HWND> input_window_{};
    std::atomic<unsigned> reset_generation_{};
    unsigned keyboard_generation_{};
    KeyboardMonitor keyboard_;
    CaptureWindow capture_window_;
    std::thread input_thread_, worker_thread_;
    mutable std::mutex mutex_;
    std::condition_variable wake_;
    Json state_, input_rows_ = Json::array(), config_;
    std::deque<std::pair<Json, std::function<void(Json)>>> requests_;
    bool input_done_{}, worker_done_{}, clear_requested_{};
    Json network_rows_ = Json::array();
    Json network_events_ = Json::array();
    Json unsaved_rows_ = Json::array(), unsaved_events_ = Json::array();
    std::map<std::int64_t, std::uint64_t> coverage_;
    std::int64_t last_coverage_ms_{};
};
} // namespace pulsetrace
