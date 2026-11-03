#include "pixcake/storage/sqlite_export_log.hpp"

#include <sqlite3.h>

#include <climits>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <utility>

namespace pixcake {
namespace {

constexpr const char* kSchema =
    "CREATE TABLE export_event ("
    "seq INTEGER PRIMARY KEY,"
    "kind TEXT NOT NULL,"
    "export_id TEXT NOT NULL,"
    "original_id TEXT NOT NULL,"
    "document_revision INTEGER NOT NULL,"
    "destination_path TEXT NOT NULL,"
    "settings_snapshot TEXT NOT NULL,"
    "watermark_free_second INTEGER NOT NULL,"
    "print_requested INTEGER NOT NULL,"
    "checksum TEXT NOT NULL);"
    "CREATE TABLE export_cache ("
    "id INTEGER PRIMARY KEY CHECK (id = 1),"
    "fast_export INTEGER NOT NULL);"
    "CREATE TABLE export_task_cache ("
    "export_id TEXT PRIMARY KEY,"
    "status TEXT NOT NULL);";

ResultCode map_sqlite(int rc) {
  if (rc == SQLITE_OK || rc == SQLITE_DONE || rc == SQLITE_ROW) {
    return ResultCode::Ok;
  }
  if (rc == SQLITE_FULL) {
    return ResultCode::DiskFull;
  }
  if (rc == SQLITE_PERM || rc == SQLITE_READONLY || rc == SQLITE_AUTH) {
    return ResultCode::PermissionDenied;
  }
  if (rc == SQLITE_CONSTRAINT) {
    return ResultCode::StateConflict;
  }
  return ResultCode::IoFailed;
}

ResultCode exec_sql(sqlite3* db, const char* sql) {
  char* error = nullptr;
  const int rc = sqlite3_exec(db, sql, nullptr, nullptr, &error);
  if (error != nullptr) {
    sqlite3_free(error);
  }
  return map_sqlite(rc);
}

class Statement {
 public:
  Statement() = default;
  Statement(const Statement&) = delete;
  Statement& operator=(const Statement&) = delete;
  ~Statement() { reset(); }

  ResultCode prepare(sqlite3* db, const char* sql) {
    reset();
    return map_sqlite(sqlite3_prepare_v2(db, sql, -1, &stmt_, nullptr));
  }

  sqlite3_stmt* get() const { return stmt_; }

  void reset() {
    if (stmt_ != nullptr) {
      sqlite3_finalize(stmt_);
      stmt_ = nullptr;
    }
  }

 private:
  sqlite3_stmt* stmt_ = nullptr;
};

ResultCode bind_text(sqlite3_stmt* stmt, int index, const std::string& value) {
  if (value.size() > static_cast<std::size_t>(INT_MAX)) {
    return ResultCode::IoFailed;
  }
  return map_sqlite(sqlite3_bind_text(stmt, index, value.data(), static_cast<int>(value.size()), SQLITE_TRANSIENT));
}

std::string column_text(sqlite3_stmt* stmt, int index) {
  const auto* text = sqlite3_column_text(stmt, index);
  if (text == nullptr) {
    return {};
  }
  const int size = sqlite3_column_bytes(stmt, index);
  return std::string(reinterpret_cast<const char*>(text), static_cast<std::size_t>(size));
}

std::string hex16(std::uint64_t value) {
  char buffer[17];
  std::snprintf(buffer, sizeof(buffer), "%016llx", static_cast<unsigned long long>(value));
  return buffer;
}

std::uint64_t parse_hex16(const std::string& text) {
  if (text.size() != 16) {
    return 0;
  }
  return std::strtoull(text.c_str(), nullptr, 16);
}

ResultCode read_user_version(sqlite3* db, int& version) {
  Statement stmt;
  const ResultCode prepared = stmt.prepare(db, "PRAGMA user_version;");
  if (prepared != ResultCode::Ok) {
    return prepared;
  }
  if (sqlite3_step(stmt.get()) != SQLITE_ROW) {
    return ResultCode::IoFailed;
  }
  version = sqlite3_column_int(stmt.get(), 0);
  return ResultCode::Ok;
}

ResultCode create_schema(sqlite3* db) {
  const ResultCode began = exec_sql(db, "BEGIN IMMEDIATE;");
  if (began != ResultCode::Ok) {
    return began;
  }
  const ResultCode schema = exec_sql(db, kSchema);
  if (schema != ResultCode::Ok) {
    exec_sql(db, "ROLLBACK;");
    return schema;
  }
  const ResultCode stamp = exec_sql(db, "PRAGMA user_version=1;");
  if (stamp != ResultCode::Ok) {
    exec_sql(db, "ROLLBACK;");
    return stamp;
  }
  const ResultCode committed = exec_sql(db, "COMMIT;");
  if (committed != ResultCode::Ok) {
    exec_sql(db, "ROLLBACK;");
    return committed;
  }
  return ResultCode::Ok;
}

}  // namespace

struct SqliteExportLog::Impl {
  sqlite3* db = nullptr;
  int tx_depth = 0;
};

SqliteExportLog::SqliteExportLog(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

SqliteExportLog::~SqliteExportLog() {
  if (impl_ == nullptr || impl_->db == nullptr) {
    return;
  }
  if (impl_->tx_depth > 0) {
    sqlite3_exec(impl_->db, "ROLLBACK;", nullptr, nullptr, nullptr);
  }
  sqlite3_close(impl_->db);
  impl_->db = nullptr;
}

ResultCode SqliteExportLog::open(const std::string& path, std::unique_ptr<SqliteExportLog>& out) {
  out.reset();
  sqlite3* db = nullptr;
  const int opened = sqlite3_open(path.c_str(), &db);
  if (opened != SQLITE_OK) {
    const ResultCode code = map_sqlite(opened);
    if (db != nullptr) {
      sqlite3_close(db);
    }
    return code == ResultCode::Ok ? ResultCode::IoFailed : code;
  }
  sqlite3_busy_timeout(db, 1000);
  int version = 0;
  const ResultCode version_code = read_user_version(db, version);
  if (version_code != ResultCode::Ok) {
    sqlite3_close(db);
    return version_code;
  }
  if (version != 0 && version != 1) {
    sqlite3_close(db);
    return ResultCode::StateConflict;
  }
  const ResultCode wal = exec_sql(db, "PRAGMA journal_mode=WAL;");
  if (wal != ResultCode::Ok) {
    sqlite3_close(db);
    return wal;
  }
  if (version == 0) {
    const ResultCode schema = create_schema(db);
    if (schema != ResultCode::Ok) {
      sqlite3_close(db);
      return schema;
    }
  }
  auto impl = std::make_unique<Impl>();
  impl->db = db;
  out.reset(new SqliteExportLog(std::move(impl)));
  return ResultCode::Ok;
}

ResultCode SqliteExportLog::load(std::vector<ExportEvent>& out) {
  out.clear();
  Statement stmt;
  const ResultCode prepared = stmt.prepare(
      impl_->db,
      "SELECT kind, export_id, original_id, document_revision, destination_path, settings_snapshot, "
      "watermark_free_second, print_requested, checksum FROM export_event ORDER BY seq;");
  if (prepared != ResultCode::Ok) {
    return prepared;
  }
  int stepped = SQLITE_DONE;
  while ((stepped = sqlite3_step(stmt.get())) == SQLITE_ROW) {
    ExportEvent event;
    event.kind = column_text(stmt.get(), 0);
    event.export_id = column_text(stmt.get(), 1);
    event.original_id = column_text(stmt.get(), 2);
    event.document_revision = static_cast<std::uint64_t>(sqlite3_column_int64(stmt.get(), 3));
    event.destination_path = column_text(stmt.get(), 4);
    event.settings_snapshot = column_text(stmt.get(), 5);
    event.watermark_free_second = sqlite3_column_int(stmt.get(), 6) != 0;
    event.print_requested = sqlite3_column_int(stmt.get(), 7) != 0;
    event.checksum = parse_hex16(column_text(stmt.get(), 8));
    out.push_back(std::move(event));
  }
  return stepped == SQLITE_DONE ? ResultCode::Ok : ResultCode::IoFailed;
}

ResultCode SqliteExportLog::append(const std::vector<ExportEvent>& events) {
  Statement stmt;
  const ResultCode prepared = stmt.prepare(
      impl_->db,
      "INSERT INTO export_event(kind, export_id, original_id, document_revision, destination_path, "
      "settings_snapshot, watermark_free_second, print_requested, checksum) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?);");
  if (prepared != ResultCode::Ok) {
    return prepared;
  }
  for (const auto& event : events) {
    if (event.document_revision > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
      return ResultCode::IoFailed;
    }
    sqlite3_reset(stmt.get());
    sqlite3_clear_bindings(stmt.get());
    if (const ResultCode code = bind_text(stmt.get(), 1, event.kind); code != ResultCode::Ok) {
      return code;
    }
    if (const ResultCode code = bind_text(stmt.get(), 2, event.export_id); code != ResultCode::Ok) {
      return code;
    }
    if (const ResultCode code = bind_text(stmt.get(), 3, event.original_id); code != ResultCode::Ok) {
      return code;
    }
    if (const ResultCode code = map_sqlite(sqlite3_bind_int64(
            stmt.get(), 4, static_cast<sqlite3_int64>(event.document_revision)));
        code != ResultCode::Ok) {
      return code;
    }
    if (const ResultCode code = bind_text(stmt.get(), 5, event.destination_path); code != ResultCode::Ok) {
      return code;
    }
    if (const ResultCode code = bind_text(stmt.get(), 6, event.settings_snapshot); code != ResultCode::Ok) {
      return code;
    }
    if (const ResultCode code = map_sqlite(sqlite3_bind_int(stmt.get(), 7, event.watermark_free_second ? 1 : 0));
        code != ResultCode::Ok) {
      return code;
    }
    if (const ResultCode code = map_sqlite(sqlite3_bind_int(stmt.get(), 8, event.print_requested ? 1 : 0));
        code != ResultCode::Ok) {
      return code;
    }
    if (const ResultCode code = bind_text(stmt.get(), 9, hex16(event.checksum)); code != ResultCode::Ok) {
      return code;
    }
    const int inserted = sqlite3_step(stmt.get());
    if (inserted != SQLITE_DONE) {
      return map_sqlite(inserted);
    }
  }
  return ResultCode::Ok;
}

ResultCode SqliteExportLog::read_cache(std::optional<ExportCache>& out) {
  out.reset();
  Statement cache;
  const ResultCode prepared = cache.prepare(impl_->db, "SELECT fast_export FROM export_cache WHERE id = 1;");
  if (prepared != ResultCode::Ok) {
    return prepared;
  }
  const int cache_row = sqlite3_step(cache.get());
  if (cache_row == SQLITE_DONE) {
    return ResultCode::Ok;
  }
  if (cache_row != SQLITE_ROW) {
    return ResultCode::IoFailed;
  }
  ExportCache value;
  value.fast_export = sqlite3_column_int(cache.get(), 0) != 0;
  Statement tasks;
  const ResultCode tasks_prepared =
      tasks.prepare(impl_->db, "SELECT export_id, status FROM export_task_cache ORDER BY export_id;");
  if (tasks_prepared != ResultCode::Ok) {
    return tasks_prepared;
  }
  while (true) {
    const int step = sqlite3_step(tasks.get());
    if (step == SQLITE_DONE) {
      out = std::move(value);
      return ResultCode::Ok;
    }
    if (step != SQLITE_ROW) {
      return map_sqlite(step);
    }
    value.statuses.emplace_back(column_text(tasks.get(), 0), column_text(tasks.get(), 1));
  }
}

ResultCode SqliteExportLog::write_cache(const ExportCache& cache) {
  const ResultCode cleared = exec_sql(impl_->db, "DELETE FROM export_task_cache;");
  if (cleared != ResultCode::Ok) {
    return cleared;
  }
  Statement row;
  const ResultCode prepared = row.prepare(
      impl_->db, "INSERT INTO export_cache(id, fast_export) VALUES (1, ?) ON CONFLICT(id) DO UPDATE SET "
                 "fast_export = excluded.fast_export;");
  if (prepared != ResultCode::Ok) {
    return prepared;
  }
  if (const ResultCode code = map_sqlite(sqlite3_bind_int(row.get(), 1, cache.fast_export ? 1 : 0));
      code != ResultCode::Ok) {
    return code;
  }
  const int inserted = sqlite3_step(row.get());
  if (inserted != SQLITE_DONE) {
    return map_sqlite(inserted);
  }
  Statement task;
  const ResultCode task_prepared =
      task.prepare(impl_->db, "INSERT INTO export_task_cache(export_id, status) VALUES (?, ?);");
  if (task_prepared != ResultCode::Ok) {
    return task_prepared;
  }
  for (const auto& item : cache.statuses) {
    sqlite3_reset(task.get());
    sqlite3_clear_bindings(task.get());
    if (const ResultCode code = bind_text(task.get(), 1, item.first); code != ResultCode::Ok) {
      return code;
    }
    if (const ResultCode code = bind_text(task.get(), 2, item.second); code != ResultCode::Ok) {
      return code;
    }
    const int wrote = sqlite3_step(task.get());
    if (wrote != SQLITE_DONE) {
      return map_sqlite(wrote);
    }
  }
  return ResultCode::Ok;
}

ResultCode SqliteExportLog::in_transaction(const std::function<ResultCode()>& body) {
  const bool outer = impl_->tx_depth == 0;
  if (outer) {
    const ResultCode began = exec_sql(impl_->db, "BEGIN IMMEDIATE;");
    if (began != ResultCode::Ok) {
      return began;
    }
  }
  ++impl_->tx_depth;
  const ResultCode code = body();
  --impl_->tx_depth;
  if (!outer) {
    return code;
  }
  if (code != ResultCode::Ok) {
    exec_sql(impl_->db, "ROLLBACK;");
    return code;
  }
  const ResultCode committed = exec_sql(impl_->db, "COMMIT;");
  if (committed != ResultCode::Ok) {
    exec_sql(impl_->db, "ROLLBACK;");
    return committed;
  }
  return ResultCode::Ok;
}

}  // namespace pixcake
