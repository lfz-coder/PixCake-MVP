#include "pixcake/storage/sqlite_export_log.hpp"

#include <sqlite3.h>

#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <utility>
#include <vector>
#include <set>

namespace pixcake {
namespace {

constexpr const char* kSchema =
    "CREATE TABLE export_event ("
    "seq INTEGER PRIMARY KEY,"
    "kind TEXT NOT NULL,"
    "export_id TEXT NOT NULL,"
    "original_id TEXT NOT NULL,"
    "document_revision INTEGER NOT NULL,"
    "document_snapshot TEXT NOT NULL,"
    "layer_document_snapshot TEXT NOT NULL DEFAULT '',"
    "destination_path TEXT NOT NULL,"
    "destination_root TEXT NOT NULL DEFAULT '',"
    "destination_identity_valid INTEGER NOT NULL DEFAULT 0,"
    "destination_device INTEGER NOT NULL DEFAULT 0,"
    "destination_inode INTEGER NOT NULL DEFAULT 0,"
    "destination_size INTEGER NOT NULL DEFAULT 0,"
    "settings_snapshot TEXT NOT NULL,"
    "watermark_free_second INTEGER NOT NULL,"
    "print_requested INTEGER NOT NULL,"
    "checksum TEXT NOT NULL,"
    "failure_code INTEGER NOT NULL DEFAULT 0,"
    "phase TEXT NOT NULL DEFAULT '',"
    "duration_ms INTEGER NOT NULL DEFAULT 0,"
    "deduplicated INTEGER NOT NULL DEFAULT 0);"
    "CREATE TABLE export_cache ("
    "id INTEGER PRIMARY KEY CHECK (id = 1),"
    "fast_export INTEGER NOT NULL);"
    "CREATE TABLE export_task_cache ("
    "export_id TEXT PRIMARY KEY,"
    "status TEXT NOT NULL);"
    "CREATE TABLE export_batch (export_id TEXT PRIMARY KEY, batch_id TEXT NOT NULL, original_id TEXT NOT NULL);";

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

bool has_column(sqlite3* db, const char* table, const char* column) {
  const char* sql = nullptr;
  if (std::strcmp(table, "export_event") == 0) {
    sql = "PRAGMA table_info(export_event);";
  } else if (std::strcmp(table, "export_cache") == 0) {
    sql = "PRAGMA table_info(export_cache);";
  } else if (std::strcmp(table, "export_task_cache") == 0) {
    sql = "PRAGMA table_info(export_task_cache);";
  } else if (std::strcmp(table, "export_batch") == 0) {
    sql = "PRAGMA table_info(export_batch);";
  } else {
    return false;
  }
  Statement stmt;
  if (stmt.prepare(db, sql) != ResultCode::Ok) return false;
  while (sqlite3_step(stmt.get()) == SQLITE_ROW) {
    if (column_text(stmt.get(), 1) == column) return true;
  }
  return false;
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

std::string bytes_hex(const std::vector<std::uint8_t>& bytes) {
  static constexpr char digits[] = "0123456789abcdef";
  std::string text;
  text.reserve(bytes.size() * 2);
  for (const auto byte : bytes) {
    text.push_back(digits[(byte >> 4) & 0x0f]);
    text.push_back(digits[byte & 0x0f]);
  }
  return text;
}

std::vector<std::uint8_t> hex_bytes(const std::string& text) {
  if (text.size() % 2 != 0) return {};
  std::vector<std::uint8_t> bytes;
  bytes.reserve(text.size() / 2);
  for (std::size_t i = 0; i < text.size(); i += 2) {
    const auto digit = [](char value) -> int {
      if (value >= '0' && value <= '9') return value - '0';
      if (value >= 'a' && value <= 'f') return value - 'a' + 10;
      if (value >= 'A' && value <= 'F') return value - 'A' + 10;
      return -1;
    };
    const int high = digit(text[i]);
    const int low = digit(text[i + 1]);
    if (high < 0 || low < 0) return {};
    bytes.push_back(static_cast<std::uint8_t>((high << 4) | low));
  }
  return bytes;
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

ResultCode ensure_batch_schema(sqlite3* db) {
  const ResultCode batch = exec_sql(db, "CREATE TABLE IF NOT EXISTS export_batch (export_id TEXT PRIMARY KEY, batch_id TEXT NOT NULL, original_id TEXT NOT NULL);");
  if (batch != ResultCode::Ok) return batch;
  if (!has_column(db, "export_event", "destination_root")) {
    const ResultCode root = exec_sql(db, "ALTER TABLE export_event ADD COLUMN destination_root TEXT NOT NULL DEFAULT ''; ");
    if (root != ResultCode::Ok) return root;
  }
  if (!has_column(db, "export_event", "layer_document_snapshot")) {
    const ResultCode layers =
        exec_sql(db, "ALTER TABLE export_event ADD COLUMN layer_document_snapshot TEXT NOT NULL DEFAULT ''; ");
    if (layers != ResultCode::Ok) return layers;
  }
  if (!has_column(db, "export_event", "failure_code")) {
    const ResultCode failure = exec_sql(db, "ALTER TABLE export_event ADD COLUMN failure_code INTEGER NOT NULL DEFAULT 0;");
    if (failure != ResultCode::Ok) return failure;
  }
  if (!has_column(db, "export_event", "phase")) {
    if (exec_sql(db, "ALTER TABLE export_event ADD COLUMN phase TEXT NOT NULL DEFAULT '';" ) != ResultCode::Ok ||
        exec_sql(db, "ALTER TABLE export_event ADD COLUMN duration_ms INTEGER NOT NULL DEFAULT 0;") != ResultCode::Ok ||
        exec_sql(db, "ALTER TABLE export_event ADD COLUMN deduplicated INTEGER NOT NULL DEFAULT 0;") != ResultCode::Ok) return ResultCode::IoFailed;
  }
  if (!has_column(db, "export_event", "destination_identity_valid")) {
    const ResultCode identity = exec_sql(db, "ALTER TABLE export_event ADD COLUMN destination_identity_valid INTEGER NOT NULL DEFAULT 0;");
    if (identity != ResultCode::Ok) return identity;
    if (exec_sql(db, "ALTER TABLE export_event ADD COLUMN destination_device INTEGER NOT NULL DEFAULT 0;") != ResultCode::Ok ||
        exec_sql(db, "ALTER TABLE export_event ADD COLUMN destination_inode INTEGER NOT NULL DEFAULT 0;") != ResultCode::Ok ||
        exec_sql(db, "ALTER TABLE export_event ADD COLUMN destination_size INTEGER NOT NULL DEFAULT 0;") != ResultCode::Ok) return ResultCode::IoFailed;
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
  if (const ResultCode batches = ensure_batch_schema(db); batches != ResultCode::Ok) {
    sqlite3_close(db);
    return batches;
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
      "SELECT e.kind, e.export_id, e.original_id, e.document_revision, e.document_snapshot, e.layer_document_snapshot, e.destination_path, e.destination_root, "
      "e.destination_identity_valid, e.destination_device, e.destination_inode, e.destination_size, e.settings_snapshot, "
      "e.watermark_free_second, e.print_requested, e.checksum, e.failure_code, e.phase, e.duration_ms, e.deduplicated, b.batch_id "
      "FROM export_event AS e "
      "LEFT JOIN export_batch AS b ON b.export_id = e.export_id AND b.original_id = e.original_id "
      "ORDER BY e.seq;");
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
    event.document_snapshot = hex_bytes(column_text(stmt.get(), 4));
    event.layer_document_snapshot = hex_bytes(column_text(stmt.get(), 5));
    event.destination_path = column_text(stmt.get(), 6);
    event.destination_root = column_text(stmt.get(), 7);
    event.destination_identity.valid = sqlite3_column_int(stmt.get(), 8) != 0;
    event.destination_identity.device = static_cast<std::uint64_t>(sqlite3_column_int64(stmt.get(), 9));
    event.destination_identity.inode = static_cast<std::uint64_t>(sqlite3_column_int64(stmt.get(), 10));
    event.destination_identity.size = static_cast<std::uint64_t>(sqlite3_column_int64(stmt.get(), 11));
    event.settings_snapshot = column_text(stmt.get(), 12);
    event.watermark_free_second = sqlite3_column_int(stmt.get(), 13) != 0;
    event.print_requested = sqlite3_column_int(stmt.get(), 14) != 0;
    event.checksum = parse_hex16(column_text(stmt.get(), 15));
    event.failure_code = static_cast<ResultCode>(sqlite3_column_int(stmt.get(), 16));
    event.phase = column_text(stmt.get(), 17);
    event.duration_ms = static_cast<std::uint64_t>(sqlite3_column_int64(stmt.get(), 18));
    event.deduplicated = sqlite3_column_int(stmt.get(), 19) != 0;
    event.batch_id = column_text(stmt.get(), 20);
    out.push_back(std::move(event));
  }
  return stepped == SQLITE_DONE ? ResultCode::Ok : ResultCode::IoFailed;
}

ResultCode SqliteExportLog::append(const std::vector<ExportEvent>& events) {
  Statement stmt;
  const ResultCode prepared = stmt.prepare(
      impl_->db,
      "INSERT INTO export_event(kind, export_id, original_id, document_revision, document_snapshot, layer_document_snapshot, destination_path, destination_root, "
      "destination_identity_valid, destination_device, destination_inode, destination_size, settings_snapshot, "
      "watermark_free_second, print_requested, checksum, failure_code, phase, duration_ms, deduplicated) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);");
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
    if (const ResultCode code = bind_text(stmt.get(), 5, bytes_hex(event.document_snapshot)); code != ResultCode::Ok) {
      return code;
    }
    if (const ResultCode code = bind_text(stmt.get(), 6, bytes_hex(event.layer_document_snapshot)); code != ResultCode::Ok) {
      return code;
    }
    if (const ResultCode code = bind_text(stmt.get(), 7, event.destination_path); code != ResultCode::Ok) {
      return code;
    }
    if (const ResultCode code = bind_text(stmt.get(), 8, event.destination_root); code != ResultCode::Ok) {
      return code;
    }
    if (const ResultCode code = map_sqlite(sqlite3_bind_int(stmt.get(), 9, event.destination_identity.valid ? 1 : 0)); code != ResultCode::Ok) return code;
    if (const ResultCode code = map_sqlite(sqlite3_bind_int64(stmt.get(), 10, static_cast<sqlite3_int64>(event.destination_identity.device))); code != ResultCode::Ok) return code;
    if (const ResultCode code = map_sqlite(sqlite3_bind_int64(stmt.get(), 11, static_cast<sqlite3_int64>(event.destination_identity.inode))); code != ResultCode::Ok) return code;
    if (const ResultCode code = map_sqlite(sqlite3_bind_int64(stmt.get(), 12, static_cast<sqlite3_int64>(event.destination_identity.size))); code != ResultCode::Ok) return code;
    if (const ResultCode code = bind_text(stmt.get(), 13, event.settings_snapshot); code != ResultCode::Ok) {
      return code;
    }
    if (const ResultCode code = map_sqlite(sqlite3_bind_int(stmt.get(), 14, event.watermark_free_second ? 1 : 0));
        code != ResultCode::Ok) {
      return code;
    }
    if (const ResultCode code = map_sqlite(sqlite3_bind_int(stmt.get(), 15, event.print_requested ? 1 : 0));
        code != ResultCode::Ok) {
      return code;
    }
    if (const ResultCode code = bind_text(stmt.get(), 16, hex16(event.checksum)); code != ResultCode::Ok) {
      return code;
    }
    if (const ResultCode code = map_sqlite(sqlite3_bind_int(stmt.get(), 17, static_cast<int>(event.failure_code))); code != ResultCode::Ok) {
      return code;
    }
    if (const ResultCode code = bind_text(stmt.get(), 18, event.phase); code != ResultCode::Ok) return code;
    if (const ResultCode code = map_sqlite(sqlite3_bind_int64(stmt.get(), 19, static_cast<sqlite3_int64>(event.duration_ms))); code != ResultCode::Ok) return code;
    if (const ResultCode code = map_sqlite(sqlite3_bind_int(stmt.get(), 20, event.deduplicated ? 1 : 0)); code != ResultCode::Ok) return code;
    const int inserted = sqlite3_step(stmt.get());
    if (inserted != SQLITE_DONE) {
      return map_sqlite(inserted);
    }
    if (!event.export_id.empty() && !event.batch_id.empty()) {
      Statement batch;
      if (batch.prepare(impl_->db, "INSERT INTO export_batch(export_id, batch_id, original_id) VALUES (?, ?, ?) ON CONFLICT(export_id) DO UPDATE SET batch_id = excluded.batch_id, original_id = excluded.original_id;") != ResultCode::Ok) return ResultCode::IoFailed;
      if (bind_text(batch.get(), 1, event.export_id) != ResultCode::Ok || bind_text(batch.get(), 2, event.batch_id) != ResultCode::Ok || bind_text(batch.get(), 3, event.original_id) != ResultCode::Ok || sqlite3_step(batch.get()) != SQLITE_DONE) return map_sqlite(sqlite3_errcode(impl_->db));
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
  // The event log is authoritative.  Update only the task rows supplied by
  // this cache; opening the log remains the explicit full-rebuild path.
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
      task.prepare(impl_->db, "INSERT INTO export_task_cache(export_id, status) VALUES (?, ?) "
                              "ON CONFLICT(export_id) DO UPDATE SET status = excluded.status;");
  if (task_prepared != ResultCode::Ok) {
    return task_prepared;
  }
  std::set<std::string> seen;
  for (const auto& item : cache.statuses) {
    if (!seen.insert(item.first).second) return ResultCode::StateConflict;
    sqlite3_reset(task.get()); sqlite3_clear_bindings(task.get());
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
