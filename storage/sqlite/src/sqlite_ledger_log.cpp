#include "pixcake/storage/sqlite_ledger_log.hpp"

#include <sqlite3.h>

#include <climits>
#include <string>
#include <utility>
#include <vector>
#include <set>

namespace pixcake {
namespace {

// 这里的语句都是源码里的常量。任务编号、导出编号和其它调用方字符串只走 sqlite3_bind_text。
constexpr const char* kSchema = R"(CREATE TABLE ledger_event (
  seq INTEGER PRIMARY KEY,
  kind TEXT NOT NULL,
  task_id TEXT NOT NULL,
  tool_id TEXT NOT NULL,
  stroke_id TEXT NOT NULL,
  origin TEXT NOT NULL,
  original_id TEXT NOT NULL,
  export_id TEXT NOT NULL,
  high_compute INTEGER NOT NULL,
  batch_generation INTEGER NOT NULL,
  member INTEGER NOT NULL,
  subscribed INTEGER NOT NULL,
  consumed_master_free INTEGER NOT NULL,
  quoted_points INTEGER NOT NULL,
  acceleration_points INTEGER NOT NULL,
  points_delta INTEGER NOT NULL,
  sheets_delta INTEGER NOT NULL
);
CREATE TABLE ledger_cache (
  id INTEGER PRIMARY KEY CHECK (id = 1),
  creative_points INTEGER NOT NULL,
  export_sheets INTEGER NOT NULL
);
CREATE TABLE ledger_task_cache (
  task_id TEXT PRIMARY KEY,
  status TEXT NOT NULL
);
CREATE TABLE ledger_export_batch (
  export_id TEXT PRIMARY KEY,
  batch_id TEXT NOT NULL,
  original_id TEXT NOT NULL
);
)";

constexpr const char* kInsertEvent =
    "INSERT INTO ledger_event("
    "kind, task_id, tool_id, stroke_id, origin, original_id, export_id, "
    "high_compute, batch_generation, member, subscribed, consumed_master_free, "
    "quoted_points, acceleration_points, points_delta, sheets_delta) "
    "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";

constexpr const char* kSelectEvents =
    "SELECT e.kind, e.task_id, e.tool_id, e.stroke_id, e.origin, e.original_id, e.export_id, "
    "e.high_compute, e.batch_generation, e.member, e.subscribed, e.consumed_master_free, "
    "e.quoted_points, e.acceleration_points, e.points_delta, e.sheets_delta, b.batch_id "
    "FROM ledger_event AS e "
    "LEFT JOIN ledger_export_batch AS b "
    "ON b.export_id = e.export_id AND b.original_id = e.original_id "
    "ORDER BY e.seq;";
constexpr const char* kBatchUpsert =
    "INSERT INTO ledger_export_batch(export_id, batch_id, original_id) VALUES (?, ?, ?) "
    "ON CONFLICT(export_id) DO UPDATE SET batch_id = excluded.batch_id, original_id = excluded.original_id;";
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

ResultCode bind_int(sqlite3_stmt* stmt, int index, int value) {
  return map_sqlite(sqlite3_bind_int(stmt, index, value));
}

ResultCode bind_int64(sqlite3_stmt* stmt, int index, std::int64_t value) {
  return map_sqlite(sqlite3_bind_int64(stmt, index, value));
}

std::string column_text(sqlite3_stmt* stmt, int index) {
  const auto* text = sqlite3_column_text(stmt, index);
  if (text == nullptr) {
    return {};
  }
  const int size = sqlite3_column_bytes(stmt, index);
  return std::string(reinterpret_cast<const char*>(text), static_cast<std::size_t>(size));
}

ResultCode read_user_version(sqlite3* db, int& version) {
  Statement stmt;
  const ResultCode prepared = stmt.prepare(db, "PRAGMA user_version;");
  if (prepared != ResultCode::Ok) {
    return prepared;
  }
  const int rc = sqlite3_step(stmt.get());
  if (rc != SQLITE_ROW) {
    return map_sqlite(rc);
  }
  version = sqlite3_column_int(stmt.get(), 0);
  return ResultCode::Ok;
}

std::string read_journal_mode(sqlite3* db) {
  Statement stmt;
  if (stmt.prepare(db, "PRAGMA journal_mode;") != ResultCode::Ok) {
    return {};
  }
  if (sqlite3_step(stmt.get()) != SQLITE_ROW) {
    return {};
  }
  return column_text(stmt.get(), 0);
}

LedgerEvent read_event(sqlite3_stmt* stmt) {
  LedgerEvent event;
  event.kind = column_text(stmt, 0);
  event.task_id = column_text(stmt, 1);
  event.tool_id = column_text(stmt, 2);
  event.stroke_id = column_text(stmt, 3);
  event.origin = column_text(stmt, 4);
  event.original_id = column_text(stmt, 5);
  event.export_id = column_text(stmt, 6);
  event.high_compute = sqlite3_column_int(stmt, 7) != 0;
  event.batch_generation = sqlite3_column_int(stmt, 8) != 0;
  event.member = sqlite3_column_int(stmt, 9) != 0;
  event.subscribed = sqlite3_column_int(stmt, 10) != 0;
  event.consumed_master_free = sqlite3_column_int(stmt, 11) != 0;
  event.quoted_points = sqlite3_column_int64(stmt, 12);
  event.acceleration_points = sqlite3_column_int64(stmt, 13);
  event.points_delta = sqlite3_column_int64(stmt, 14);
  event.sheets_delta = sqlite3_column_int64(stmt, 15);
  return event;
}

ResultCode bind_event(sqlite3_stmt* stmt, const LedgerEvent& event) {
  if (const ResultCode code = bind_text(stmt, 1, event.kind); code != ResultCode::Ok) {
    return code;
  }
  if (const ResultCode code = bind_text(stmt, 2, event.task_id); code != ResultCode::Ok) {
    return code;
  }
  if (const ResultCode code = bind_text(stmt, 3, event.tool_id); code != ResultCode::Ok) {
    return code;
  }
  if (const ResultCode code = bind_text(stmt, 4, event.stroke_id); code != ResultCode::Ok) {
    return code;
  }
  if (const ResultCode code = bind_text(stmt, 5, event.origin); code != ResultCode::Ok) {
    return code;
  }
  if (const ResultCode code = bind_text(stmt, 6, event.original_id); code != ResultCode::Ok) {
    return code;
  }
  if (const ResultCode code = bind_text(stmt, 7, event.export_id); code != ResultCode::Ok) {
    return code;
  }
  if (const ResultCode code = bind_int(stmt, 8, event.high_compute ? 1 : 0); code != ResultCode::Ok) {
    return code;
  }
  if (const ResultCode code = bind_int(stmt, 9, event.batch_generation ? 1 : 0); code != ResultCode::Ok) {
    return code;
  }
  if (const ResultCode code = bind_int(stmt, 10, event.member ? 1 : 0); code != ResultCode::Ok) {
    return code;
  }
  if (const ResultCode code = bind_int(stmt, 11, event.subscribed ? 1 : 0); code != ResultCode::Ok) {
    return code;
  }
  if (const ResultCode code = bind_int(stmt, 12, event.consumed_master_free ? 1 : 0); code != ResultCode::Ok) {
    return code;
  }
  if (const ResultCode code = bind_int64(stmt, 13, event.quoted_points); code != ResultCode::Ok) {
    return code;
  }
  if (const ResultCode code = bind_int64(stmt, 14, event.acceleration_points); code != ResultCode::Ok) {
    return code;
  }
  if (const ResultCode code = bind_int64(stmt, 15, event.points_delta); code != ResultCode::Ok) {
    return code;
  }
  return bind_int64(stmt, 16, event.sheets_delta);
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
  return exec_sql(db, "CREATE TABLE IF NOT EXISTS ledger_export_batch (export_id TEXT PRIMARY KEY, batch_id TEXT NOT NULL, original_id TEXT NOT NULL);");
}

}  // namespace

struct SqliteLedgerLog::Impl {
  sqlite3* db = nullptr;
  int tx_depth = 0;
};

SqliteLedgerLog::SqliteLedgerLog(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

SqliteLedgerLog::~SqliteLedgerLog() {
  if (impl_ == nullptr || impl_->db == nullptr) {
    return;
  }
  if (impl_->tx_depth > 0) {
    sqlite3_exec(impl_->db, "ROLLBACK;", nullptr, nullptr, nullptr);
  }
  sqlite3_close(impl_->db);
  impl_->db = nullptr;
}

ResultCode SqliteLedgerLog::open(const std::string& path, std::unique_ptr<SqliteLedgerLog>& out) {
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
  if (read_journal_mode(db) != "wal") {
    sqlite3_close(db);
    return ResultCode::IoFailed;
  }

  auto impl = std::make_unique<Impl>();
  impl->db = db;
  out = std::unique_ptr<SqliteLedgerLog>(new SqliteLedgerLog(std::move(impl)));
  return ResultCode::Ok;
}

ResultCode SqliteLedgerLog::load(std::vector<LedgerEvent>& out) {
  Statement stmt;
  const ResultCode prepared = stmt.prepare(impl_->db, kSelectEvents);
  if (prepared != ResultCode::Ok) {
    return prepared;
  }
  std::vector<LedgerEvent> events;
  while (true) {
    const int rc = sqlite3_step(stmt.get());
    if (rc == SQLITE_DONE) {
      out = std::move(events);
      return ResultCode::Ok;
    }
    if (rc != SQLITE_ROW) {
      return map_sqlite(rc);
    }
    LedgerEvent event = read_event(stmt.get());
    event.batch_id = column_text(stmt.get(), 16);
    events.push_back(std::move(event));
  }
}

ResultCode SqliteLedgerLog::event_count(std::size_t& out) {
  Statement stmt;
  const ResultCode prepared = stmt.prepare(impl_->db, "SELECT COUNT(*) FROM ledger_event;");
  if (prepared != ResultCode::Ok) return prepared;
  if (sqlite3_step(stmt.get()) != SQLITE_ROW) return ResultCode::IoFailed;
  out = static_cast<std::size_t>(sqlite3_column_int64(stmt.get(), 0));
  return ResultCode::Ok;
}

ResultCode SqliteLedgerLog::append(const std::vector<LedgerEvent>& events) {
  for (const LedgerEvent& event : events) {
    Statement stmt;
    const ResultCode prepared = stmt.prepare(impl_->db, kInsertEvent);
    if (prepared != ResultCode::Ok) {
      return prepared;
    }
    const ResultCode bound = bind_event(stmt.get(), event);
    if (bound != ResultCode::Ok) {
      return bound;
    }
    const int rc = sqlite3_step(stmt.get());
    if (rc != SQLITE_DONE) {
      return map_sqlite(rc);
    }
    if (!event.export_id.empty() && !event.batch_id.empty()) {
      Statement batch;
      if (batch.prepare(impl_->db, kBatchUpsert) != ResultCode::Ok) return ResultCode::IoFailed;
      if (bind_text(batch.get(), 1, event.export_id) != ResultCode::Ok ||
          bind_text(batch.get(), 2, event.batch_id) != ResultCode::Ok ||
          bind_text(batch.get(), 3, event.original_id) != ResultCode::Ok ||
          sqlite3_step(batch.get()) != SQLITE_DONE) return map_sqlite(sqlite3_errcode(impl_->db));
    }
  }
  return ResultCode::Ok;
}

ResultCode SqliteLedgerLog::read_cache(std::optional<LedgerCache>& out) {
  out.reset();
  Statement balance;
  const ResultCode prepared =
      balance.prepare(impl_->db, "SELECT creative_points, export_sheets FROM ledger_cache WHERE id = 1;");
  if (prepared != ResultCode::Ok) {
    return prepared;
  }
  const int rc = sqlite3_step(balance.get());
  if (rc == SQLITE_DONE) {
    return ResultCode::Ok;
  }
  if (rc != SQLITE_ROW) {
    return map_sqlite(rc);
  }
  LedgerCache cache;
  cache.creative_points = sqlite3_column_int64(balance.get(), 0);
  cache.export_sheets = sqlite3_column_int64(balance.get(), 1);

  Statement tasks;
  const ResultCode tasks_prepared =
      tasks.prepare(impl_->db, "SELECT task_id, status FROM ledger_task_cache ORDER BY task_id;");
  if (tasks_prepared != ResultCode::Ok) {
    return tasks_prepared;
  }
  while (true) {
    const int step = sqlite3_step(tasks.get());
    if (step == SQLITE_DONE) {
      out = std::move(cache);
      return ResultCode::Ok;
    }
    if (step != SQLITE_ROW) {
      return map_sqlite(step);
    }
    cache.task_statuses.emplace_back(column_text(tasks.get(), 0), column_text(tasks.get(), 1));
  }
}

ResultCode SqliteLedgerLog::write_cache(const LedgerCache& cache) {
  Statement balance;
  const ResultCode prepared =
      balance.prepare(impl_->db, "INSERT INTO ledger_cache(id, creative_points, export_sheets) VALUES (1, ?, ?) "
                               "ON CONFLICT(id) DO UPDATE SET creative_points=excluded.creative_points, export_sheets=excluded.export_sheets;");
  if (prepared != ResultCode::Ok) {
    return prepared;
  }
  if (const ResultCode code = bind_int64(balance.get(), 1, cache.creative_points); code != ResultCode::Ok) {
    return code;
  }
  if (const ResultCode code = bind_int64(balance.get(), 2, cache.export_sheets); code != ResultCode::Ok) {
    return code;
  }
  const int inserted = sqlite3_step(balance.get());
  if (inserted != SQLITE_DONE) {
    return map_sqlite(inserted);
  }

  Statement task;
  const ResultCode task_prepared =
      task.prepare(impl_->db, "INSERT INTO ledger_task_cache(task_id, status) VALUES (?, ?) "
                             "ON CONFLICT(task_id) DO UPDATE SET status=excluded.status;");
  if (task_prepared != ResultCode::Ok) return task_prepared;
  std::set<std::string> seen;
  for (const auto& item : cache.task_statuses) {
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

ResultCode SqliteLedgerLog::in_transaction(const std::function<ResultCode()>& body) {
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
