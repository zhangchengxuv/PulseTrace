#pragma once
#include "pulsetrace/desktop/widget_ipc.hpp"
#include "pulsetrace/json.hpp"
#include <filesystem>
namespace pulsetrace {
class TaskbarWidgetHost {
 public:
  TaskbarWidgetHost(HWND owner,const std::filesystem::path &directory);
  ~TaskbarWidgetHost();
  void update(const Json &status);
  Json status() const;
  void stop() noexcept;
 private:
  HWND owner_{};HANDLE mapping_{},process_{};WidgetShared *shared_{};std::string error_;
  std::string last_interface_;
  std::int64_t last_heartbeat_{};
  unsigned stable_samples_{};
};
}
