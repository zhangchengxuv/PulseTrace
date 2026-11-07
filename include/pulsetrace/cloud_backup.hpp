#pragma once
#include "pulsetrace/json.hpp"
#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>
#include <span>
#include <vector>
namespace pulsetrace {
using CloudBytes=std::vector<unsigned char>;
struct CloudRequest{std::string method,url,account,password;CloudBytes body;bool create_only{};unsigned depth{};};
struct CloudResponse{unsigned status{};CloudBytes body;};
using CloudTransport=std::function<CloudResponse(const CloudRequest &,std::atomic_bool &)>;
CloudResponse cloud_https_request(const CloudRequest &,std::atomic_bool &);
std::string cloud_sha256(std::span<const unsigned char>);
CloudBytes cloud_encrypt(std::span<const unsigned char>,const std::string &password);
CloudBytes cloud_decrypt(std::span<const unsigned char>,const std::string &password);
CloudBytes cloud_pack(const std::filesystem::path &snapshot);
Json cloud_unpack(std::span<const unsigned char> payload,const std::filesystem::path &destination);
// Must run with the exclusive application data lock and BEFORE opening the core.
Json cloud_apply_pending_restore(const std::filesystem::path &data,const std::function<void()> &after_database_install={});
class CloudBackupService{
 public:
  CloudBackupService(std::filesystem::path data,std::function<std::filesystem::path()> capture,CloudTransport transport=cloud_https_request,bool start_worker=true);
  ~CloudBackupService();
  Json status() const;
  Json command(const std::string &kind,const Json &params=Json::object());
  void stop();
 private:
  struct Impl;std::unique_ptr<Impl> impl_;
};
}
