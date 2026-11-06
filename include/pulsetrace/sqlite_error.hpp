#pragma once
#include "pulsetrace/json.hpp"
#include <stdexcept>
struct sqlite3;
namespace pulsetrace {
class SQLiteError final : public std::runtime_error {
 public:
  SQLiteError(sqlite3 *db,const char *phase,const char *sql="",const std::string &path="");
  const Json &details() const noexcept {return details_;}
  bool transient() const noexcept;
 private:
  explicit SQLiteError(Json detail);
  Json details_;
};
}
