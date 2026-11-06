#pragma once
#include "pulsetrace/database.hpp"
#include "pulsetrace/app_usage.hpp"
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
    Json campus_portal_context() const;
    bool prepare_stop();
    void cancel_stop() noexcept;
    bool flush_now(std::chrono::milliseconds timeout=std::chrono::seconds(5));
    void on_flush_requested() noexcept override {++flush_requested_;wake_.notify_all();}
    void request(Json request, std::function<void(Json)> reply);
    void save_campus_snapshot(Json snapshot, std::function<void(Json)> reply);
    bool capture_enabled() const noexcept override { return enabled_.load() && !session_locked_.load() && !suspended_.load() && !quiescing_.load(); }
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
        on_flush_requested();
    }
    void on_power_suspended(bool suspended) noexcept override;
    void on_registration_error(unsigned long error) noexcept override;
    void on_session_status(bool registered,unsigned long error) noexcept override;

  private:
    void worker();
    Json execute(const Json &request, DatabaseManager *connection);
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
    std::unique_ptr<AppUsageMonitor> usage_;
    Json unsaved_usage_=Json::array();
    Json unsaved_rankings_=Json::array();
    std::map<std::string,std::string> ranking_icons_;
    std::string session_;
    std::atomic<bool> enabled_{false}, stopping_{false};
    std::atomic<bool> session_locked_{false}, suspended_{false}, quiescing_{false}, network_rebaseline_{false};
    std::atomic<std::uint64_t> flush_requested_{};
    std::uint64_t flush_completed_{}, persistence_revision_{};
    Json unsaved_batch_=Json::object();
    std::atomic<HWND> input_window_{};
    std::atomic<unsigned> reset_generation_{};
    unsigned keyboard_generation_{};
    KeyboardMonitor keyboard_;
    CaptureWindow capture_window_;
    std::thread input_thread_, worker_thread_;
    mutable std::mutex mutex_;
    std::condition_variable wake_;
    Json state_, input_rows_ = Json::array(), config_;
    Json campus_rules_=Json::object();
    std::map<std::string,Json> input_pending_;
    std::deque<std::pair<Json, std::function<void(Json)>>> requests_;
    bool input_done_{}, worker_done_{}, clear_requested_{};
    Json network_rows_ = Json::array();
    Json network_events_ = Json::array();
    Json unsaved_rows_ = Json::array(), unsaved_events_ = Json::array();
    std::map<std::int64_t, std::uint64_t> coverage_;
    std::int64_t last_coverage_ms_{};
};
} // namespace pulsetrace
