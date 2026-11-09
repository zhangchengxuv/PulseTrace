#pragma once
#include "pulsetrace/json.hpp"
#include <atomic>
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>
namespace pulsetrace {
struct SemVersion {
    std::vector<std::string> numbers, pre;
    static SemVersion parse(std::string text);
    int compare(const SemVersion &other) const;
};
struct UpdateAsset { std::string source, version, name, url, checksum_url, sha256, notes, page; std::uint64_t size{}, latency_ms{}; };
std::optional<UpdateAsset> github_release(const Json &releases, const std::string &channel);
// Public Atom fallback only establishes a version; it never authorizes a download.
std::string github_feed_version(const std::string &xml, const std::string &channel);
std::optional<UpdateAsset> gitee_release(const Json &releases, const std::string &channel,
                                       const std::function<Json(std::int64_t)> &attachments);
std::string parse_checksum(const std::string &text, const std::string &filename);
std::vector<UpdateAsset> choose_updates(std::vector<UpdateAsset> assets, const std::string &current);
bool trusted_update_url(const std::string &url, bool redirect = false);
std::string sha256_file(const std::filesystem::path &file);
// No installer execution is permitted without both Authenticode trust and a build-pinned publisher.
bool trusted_installer(const std::filesystem::path &file, const std::string &publisher_sha256);
class UpdateService {
  public:
    using Transport=std::function<void(const std::string&,std::uint64_t,const std::atomic<bool>&,const std::function<void(const char*,std::size_t)>&)>;
    explicit UpdateService(std::filesystem::path data,Transport transport={});
    ~UpdateService();
    Json status() const;
    void check(std::string channel, std::string source);
    void download();
    void cancel();
    std::filesystem::path verified_installer() const;
    std::string release_page() const;
  private:
    void start(std::function<void()> work);
    void patch(Json values);
    void read(const std::string &url,std::uint64_t limit,const std::function<void(const char*,std::size_t)> &sink);
    std::string fetch(const std::string &url,const std::atomic<bool> &,std::uint64_t limit=2*1024*1024);
    Transport transport_;
    std::filesystem::path data_, installer_;
    mutable std::mutex mutex_;
    Json state_;
    std::vector<UpdateAsset> candidates_;
    std::atomic<bool> cancelled_{false};
    std::thread worker_;
};
}
