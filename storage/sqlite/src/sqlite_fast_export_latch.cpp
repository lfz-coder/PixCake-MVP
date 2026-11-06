#include "pixcake/storage/sqlite_fast_export_latch.hpp"

#include <sqlite3.h>

#include <cstring>

namespace pixcake {

ResultCode SqliteFastExportLatch::open(const std::string& path, std::unique_ptr<SqliteFastExportLatch>& out) {
  out.reset();
  sqlite3* db = nullptr;
  if (sqlite3_open_v2(path.c_str(), &db, SQLITE_OPEN_READONLY | SQLITE_OPEN_FULLMUTEX, nullptr) != SQLITE_OK) {
    if (db != nullptr) sqlite3_close(db);
    return ResultCode::IoFailed;
  }
  sqlite3_busy_timeout(db, 1000);
  out.reset(new SqliteFastExportLatch(db));
  return ResultCode::Ok;
}

SqliteFastExportLatch::SqliteFastExportLatch(sqlite3* db) : db_(db) {}

SqliteFastExportLatch::~SqliteFastExportLatch() {
  if (db_ != nullptr) sqlite3_close(db_);
}

ResultCode SqliteFastExportLatch::read(bool& on) const {
  on = false;
  std::lock_guard<std::mutex> lock(mutex_);
  sqlite3_stmt* statement = nullptr;
  constexpr const char* kSql =
      "SELECT kind FROM export_event WHERE kind IN ('fast-on', 'fast-off') ORDER BY seq DESC LIMIT 1;";
  if (sqlite3_prepare_v2(db_, kSql, -1, &statement, nullptr) != SQLITE_OK) {
    if (statement != nullptr) sqlite3_finalize(statement);
    return ResultCode::IoFailed;
  }
  const int step = sqlite3_step(statement);
  ResultCode code = ResultCode::Ok;
  if (step == SQLITE_ROW) {
    const unsigned char* kind = sqlite3_column_text(statement, 0);
    on = kind != nullptr && std::strcmp(reinterpret_cast<const char*>(kind), "fast-on") == 0;
  } else if (step != SQLITE_DONE) {
    code = ResultCode::IoFailed;
  }
  sqlite3_finalize(statement);
  return code;
}

bool SqliteFastExportLatch::enabled() const {
  bool on = false;
  return read(on) != ResultCode::Ok || on;
}

}  // namespace pixcake
