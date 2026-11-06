#pragma once
#include "pulsetrace/json.hpp"
#include <filesystem>
#include <vector>
namespace pulsetrace {
// Aggregate-only, atomic write-ahead snapshots. No input callback performs this I/O.
class PendingStore {
 public:
  explicit PendingStore(std::filesystem::path directory):directory_(std::move(directory)){}
  void stage(const Json &batch) const;
  std::vector<std::filesystem::path> files() const;
  Json read(const std::filesystem::path &file) const;
  void acknowledge(const std::string &session) const;
 private:
  std::filesystem::path path(const std::string &session) const;
  std::filesystem::path directory_;
};
}
