#include "pixcake/storage/sqlite_project_store.hpp"

#include <sqlite3.h>

#include <utility>

namespace pixcake {
namespace {

// 这里的语句都是源码里的常量。项目名、路径、文件名和凭证只走 sqlite3_bind_*。
constexpr const char* kSchema =
    "CREATE TABLE IF NOT EXISTS project ("
    "id INTEGER PRIMARY KEY,"
    "name TEXT NOT NULL,"
    "origin TEXT NOT NULL,"
    "cloud_marked INTEGER NOT NULL,"
    "deleted_at INTEGER);"
    "CREATE TABLE IF NOT EXISTS photo ("
    "id INTEGER PRIMARY KEY,"
    "project_id INTEGER NOT NULL,"
    "absolute_path TEXT NOT NULL,"
    "file_name TEXT NOT NULL,"
    "access_credential BLOB NOT NULL,"
    "presence TEXT NOT NULL,"
    "FOREIGN KEY(project_id) REFERENCES project(id));"
    "CREATE TABLE IF NOT EXISTS trash ("
    "id INTEGER PRIMARY KEY,"
    "subject TEXT NOT NULL,"
    "subject_id INTEGER NOT NULL,"
    "deleted_at INTEGER NOT NULL);"
    "CREATE INDEX IF NOT EXISTS idx_photo_project ON photo(project_id);"
    "CREATE INDEX IF NOT EXISTS idx_trash_subject ON trash(subject, subject_id);";

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

ResultCode bind_blob(sqlite3_stmt* stmt, int index, const std::vector<std::uint8_t>& value) {
  if (value.size() > static_cast<std::size_t>(INT_MAX)) {
    return ResultCode::IoFailed;
  }
  // 空指针会被绑成 NULL。凭证列不允许 NULL，空凭证用零长度字节。
  static const std::uint8_t kEmpty = 0;
  const void* data = value.empty() ? &kEmpty : value.data();
  return map_sqlite(sqlite3_bind_blob(stmt, index, data, static_cast<int>(value.size()), SQLITE_TRANSIENT));
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

std::vector<std::uint8_t> column_blob(sqlite3_stmt* stmt, int index) {
  const int size = sqlite3_column_bytes(stmt, index);
  const auto* data = static_cast<const std::uint8_t*>(sqlite3_column_blob(stmt, index));
  if (data == nullptr || size <= 0) {
    return {};
  }
  return std::vector<std::uint8_t>(data, data + size);
}

ProjectRow read_project(sqlite3_stmt* stmt) {
  ProjectRow row;
  row.id = sqlite3_column_int64(stmt, 0);
  row.name = column_text(stmt, 1);
  row.origin = column_text(stmt, 2);
  row.cloud_marked = sqlite3_column_int(stmt, 3) != 0;
  if (sqlite3_column_type(stmt, 4) != SQLITE_NULL) {
    row.deleted_at = sqlite3_column_int64(stmt, 4);
  }
  return row;
}

PhotoRow read_photo(sqlite3_stmt* stmt) {
  PhotoRow row;
  row.id = sqlite3_column_int64(stmt, 0);
  row.project_id = sqlite3_column_int64(stmt, 1);
  row.absolute_path = column_text(stmt, 2);
  row.file_name = column_text(stmt, 3);
  row.access_credential = column_blob(stmt, 4);
  row.presence = column_text(stmt, 5);
  return row;
}

TrashRow read_trash(sqlite3_stmt* stmt) {
  TrashRow row;
  row.id = sqlite3_column_int64(stmt, 0);
  row.subject = column_text(stmt, 1);
  row.subject_id = sqlite3_column_int64(stmt, 2);
  row.deleted_at = sqlite3_column_int64(stmt, 3);
  return row;
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

}  // namespace

struct SqliteProjectStore::Impl {
  sqlite3* db = nullptr;
  int tx_depth = 0;
};

SqliteProjectStore::SqliteProjectStore(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

SqliteProjectStore::~SqliteProjectStore() {
  if (impl_ == nullptr || impl_->db == nullptr) {
    return;
  }
  if (impl_->tx_depth > 0) {
    sqlite3_exec(impl_->db, "ROLLBACK;", nullptr, nullptr, nullptr);
  }
  sqlite3_close(impl_->db);
  impl_->db = nullptr;
}

ResultCode SqliteProjectStore::open(const std::string& path, std::unique_ptr<SqliteProjectStore>& out) {
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

  const ResultCode keys = exec_sql(db, "PRAGMA foreign_keys=ON;");
  if (keys != ResultCode::Ok) {
    sqlite3_close(db);
    return keys;
  }
  const ResultCode wal = exec_sql(db, "PRAGMA journal_mode=WAL;");
  if (wal != ResultCode::Ok) {
    sqlite3_close(db);
    return wal;
  }
  if (version == 0) {
    const ResultCode schema = exec_sql(db, kSchema);
    if (schema != ResultCode::Ok) {
      sqlite3_close(db);
      return schema;
    }
    const ResultCode stamp = exec_sql(db, "PRAGMA user_version=1;");
    if (stamp != ResultCode::Ok) {
      sqlite3_close(db);
      return stamp;
    }
  }

  auto impl = std::make_unique<Impl>();
  impl->db = db;
  auto store = std::unique_ptr<SqliteProjectStore>(new SqliteProjectStore(std::move(impl)));
  if (store->journal_mode() != "wal") {
    return ResultCode::IoFailed;
  }
  out = std::move(store);
  return ResultCode::Ok;
}

std::string SqliteProjectStore::journal_mode() const {
  Statement stmt;
  if (stmt.prepare(impl_->db, "PRAGMA journal_mode;") != ResultCode::Ok) {
    return {};
  }
  if (sqlite3_step(stmt.get()) != SQLITE_ROW) {
    return {};
  }
  return column_text(stmt.get(), 0);
}

ResultCode SqliteProjectStore::in_transaction(const std::function<ResultCode()>& body) {
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

ResultCode SqliteProjectStore::insert_project(const ProjectDraft& draft, std::int64_t& id) {
  Statement stmt;
  const ResultCode prepared =
      stmt.prepare(impl_->db, "INSERT INTO project(name, origin, cloud_marked, deleted_at) VALUES (?, ?, ?, NULL);");
  if (prepared != ResultCode::Ok) {
    return prepared;
  }
  if (const ResultCode code = bind_text(stmt.get(), 1, draft.name); code != ResultCode::Ok) {
    return code;
  }
  if (const ResultCode code = bind_text(stmt.get(), 2, draft.origin); code != ResultCode::Ok) {
    return code;
  }
  if (const ResultCode code = map_sqlite(sqlite3_bind_int(stmt.get(), 3, draft.cloud_marked ? 1 : 0));
      code != ResultCode::Ok) {
    return code;
  }
  const int rc = sqlite3_step(stmt.get());
  if (rc != SQLITE_DONE) {
    return map_sqlite(rc);
  }
  id = sqlite3_last_insert_rowid(impl_->db);
  return ResultCode::Ok;
}

ResultCode SqliteProjectStore::update_project_deleted_at(std::int64_t id,
                                                         const std::optional<std::int64_t>& deleted_at) {
  Statement stmt;
  const ResultCode prepared = stmt.prepare(impl_->db, "UPDATE project SET deleted_at = ? WHERE id = ?;");
  if (prepared != ResultCode::Ok) {
    return prepared;
  }
  const int bound = deleted_at.has_value() ? sqlite3_bind_int64(stmt.get(), 1, *deleted_at)
                                            : sqlite3_bind_null(stmt.get(), 1);
  if (const ResultCode code = map_sqlite(bound); code != ResultCode::Ok) {
    return code;
  }
  if (const ResultCode code = bind_int64(stmt.get(), 2, id); code != ResultCode::Ok) {
    return code;
  }
  const int rc = sqlite3_step(stmt.get());
  return rc == SQLITE_DONE ? ResultCode::Ok : map_sqlite(rc);
}

ResultCode SqliteProjectStore::load_project(std::int64_t id, std::optional<ProjectRow>& out) {
  out.reset();
  Statement stmt;
  const ResultCode prepared = stmt.prepare(
      impl_->db, "SELECT id, name, origin, cloud_marked, deleted_at FROM project WHERE id = ?;");
  if (prepared != ResultCode::Ok) {
    return prepared;
  }
  if (const ResultCode code = bind_int64(stmt.get(), 1, id); code != ResultCode::Ok) {
    return code;
  }
  const int rc = sqlite3_step(stmt.get());
  if (rc == SQLITE_DONE) {
    return ResultCode::Ok;
  }
  if (rc != SQLITE_ROW) {
    return map_sqlite(rc);
  }
  out = read_project(stmt.get());
  return ResultCode::Ok;
}

ResultCode SqliteProjectStore::list_projects(std::vector<ProjectRow>& out) {
  out.clear();
  Statement stmt;
  const ResultCode prepared =
      stmt.prepare(impl_->db, "SELECT id, name, origin, cloud_marked, deleted_at FROM project ORDER BY id;");
  if (prepared != ResultCode::Ok) {
    return prepared;
  }
  while (true) {
    const int rc = sqlite3_step(stmt.get());
    if (rc == SQLITE_DONE) {
      return ResultCode::Ok;
    }
    if (rc != SQLITE_ROW) {
      return map_sqlite(rc);
    }
    out.push_back(read_project(stmt.get()));
  }
}

ResultCode SqliteProjectStore::erase_project_index(std::int64_t project_id) {
  return in_transaction([this, project_id] {
    Statement photos;
    ResultCode code = photos.prepare(
        impl_->db,
        "DELETE FROM trash WHERE subject = ? AND subject_id IN (SELECT id FROM photo WHERE project_id = ?);");
    if (code != ResultCode::Ok) {
      return code;
    }
    code = bind_text(photos.get(), 1, kTrashPhoto);
    if (code != ResultCode::Ok) {
      return code;
    }
    code = bind_int64(photos.get(), 2, project_id);
    if (code != ResultCode::Ok) {
      return code;
    }
    const int removed_photos = sqlite3_step(photos.get());
    if (removed_photos != SQLITE_DONE) {
      return map_sqlite(removed_photos);
    }

    Statement photo_rows;
    code = photo_rows.prepare(impl_->db, "DELETE FROM photo WHERE project_id = ?;");
    if (code != ResultCode::Ok) {
      return code;
    }
    code = bind_int64(photo_rows.get(), 1, project_id);
    if (code != ResultCode::Ok) {
      return code;
    }
    const int removed_rows = sqlite3_step(photo_rows.get());
    if (removed_rows != SQLITE_DONE) {
      return map_sqlite(removed_rows);
    }

    code = erase_trash(kTrashProject, project_id);
    if (code != ResultCode::Ok) {
      return code;
    }
    Statement project;
    code = project.prepare(impl_->db, "DELETE FROM project WHERE id = ?;");
    if (code != ResultCode::Ok) {
      return code;
    }
    code = bind_int64(project.get(), 1, project_id);
    if (code != ResultCode::Ok) {
      return code;
    }
    const int removed_project = sqlite3_step(project.get());
    return removed_project == SQLITE_DONE ? ResultCode::Ok : map_sqlite(removed_project);
  });
}

ResultCode SqliteProjectStore::insert_photo(const PhotoDraft& draft, std::int64_t& id) {
  Statement stmt;
  const ResultCode prepared = stmt.prepare(
      impl_->db,
      "INSERT INTO photo(project_id, absolute_path, file_name, access_credential, presence) VALUES (?, ?, ?, ?, ?);");
  if (prepared != ResultCode::Ok) {
    return prepared;
  }
  if (const ResultCode code = bind_int64(stmt.get(), 1, draft.project_id); code != ResultCode::Ok) {
    return code;
  }
  if (const ResultCode code = bind_text(stmt.get(), 2, draft.absolute_path); code != ResultCode::Ok) {
    return code;
  }
  if (const ResultCode code = bind_text(stmt.get(), 3, draft.file_name); code != ResultCode::Ok) {
    return code;
  }
  if (const ResultCode code = bind_blob(stmt.get(), 4, draft.access_credential); code != ResultCode::Ok) {
    return code;
  }
  if (const ResultCode code = bind_text(stmt.get(), 5, draft.presence); code != ResultCode::Ok) {
    return code;
  }
  const int rc = sqlite3_step(stmt.get());
  if (rc != SQLITE_DONE) {
    return map_sqlite(rc);
  }
  id = sqlite3_last_insert_rowid(impl_->db);
  return ResultCode::Ok;
}

ResultCode SqliteProjectStore::update_photo_presence(std::int64_t id, const std::string& presence) {
  Statement stmt;
  const ResultCode prepared = stmt.prepare(impl_->db, "UPDATE photo SET presence = ? WHERE id = ?;");
  if (prepared != ResultCode::Ok) {
    return prepared;
  }
  if (const ResultCode code = bind_text(stmt.get(), 1, presence); code != ResultCode::Ok) {
    return code;
  }
  if (const ResultCode code = bind_int64(stmt.get(), 2, id); code != ResultCode::Ok) {
    return code;
  }
  const int rc = sqlite3_step(stmt.get());
  return rc == SQLITE_DONE ? ResultCode::Ok : map_sqlite(rc);
}

ResultCode SqliteProjectStore::load_photo(std::int64_t id, std::optional<PhotoRow>& out) {
  out.reset();
  Statement stmt;
  const ResultCode prepared = stmt.prepare(
      impl_->db,
      "SELECT id, project_id, absolute_path, file_name, access_credential, presence FROM photo WHERE id = ?;");
  if (prepared != ResultCode::Ok) {
    return prepared;
  }
  if (const ResultCode code = bind_int64(stmt.get(), 1, id); code != ResultCode::Ok) {
    return code;
  }
  const int rc = sqlite3_step(stmt.get());
  if (rc == SQLITE_DONE) {
    return ResultCode::Ok;
  }
  if (rc != SQLITE_ROW) {
    return map_sqlite(rc);
  }
  out = read_photo(stmt.get());
  return ResultCode::Ok;
}

ResultCode SqliteProjectStore::list_photos(const std::optional<std::int64_t>& project_id,
                                           std::vector<PhotoRow>& out) {
  out.clear();
  Statement stmt;
  const char* sql = project_id.has_value()
                        ? "SELECT id, project_id, absolute_path, file_name, access_credential, presence "
                          "FROM photo WHERE project_id = ? ORDER BY id;"
                        : "SELECT id, project_id, absolute_path, file_name, access_credential, presence "
                          "FROM photo ORDER BY id;";
  const ResultCode prepared = stmt.prepare(impl_->db, sql);
  if (prepared != ResultCode::Ok) {
    return prepared;
  }
  if (project_id.has_value()) {
    if (const ResultCode code = bind_int64(stmt.get(), 1, *project_id); code != ResultCode::Ok) {
      return code;
    }
  }
  while (true) {
    const int rc = sqlite3_step(stmt.get());
    if (rc == SQLITE_DONE) {
      return ResultCode::Ok;
    }
    if (rc != SQLITE_ROW) {
      return map_sqlite(rc);
    }
    out.push_back(read_photo(stmt.get()));
  }
}

ResultCode SqliteProjectStore::erase_photo(std::int64_t id) {
  Statement stmt;
  const ResultCode prepared = stmt.prepare(impl_->db, "DELETE FROM photo WHERE id = ?;");
  if (prepared != ResultCode::Ok) {
    return prepared;
  }
  if (const ResultCode code = bind_int64(stmt.get(), 1, id); code != ResultCode::Ok) {
    return code;
  }
  const int rc = sqlite3_step(stmt.get());
  return rc == SQLITE_DONE ? ResultCode::Ok : map_sqlite(rc);
}

ResultCode SqliteProjectStore::insert_trash(const std::string& subject, std::int64_t subject_id,
                                            std::int64_t deleted_at) {
  Statement stmt;
  const ResultCode prepared =
      stmt.prepare(impl_->db, "INSERT INTO trash(subject, subject_id, deleted_at) VALUES (?, ?, ?);");
  if (prepared != ResultCode::Ok) {
    return prepared;
  }
  if (const ResultCode code = bind_text(stmt.get(), 1, subject); code != ResultCode::Ok) {
    return code;
  }
  if (const ResultCode code = bind_int64(stmt.get(), 2, subject_id); code != ResultCode::Ok) {
    return code;
  }
  if (const ResultCode code = bind_int64(stmt.get(), 3, deleted_at); code != ResultCode::Ok) {
    return code;
  }
  const int rc = sqlite3_step(stmt.get());
  return rc == SQLITE_DONE ? ResultCode::Ok : map_sqlite(rc);
}

ResultCode SqliteProjectStore::load_trash(const std::string& subject, std::int64_t subject_id,
                                          std::optional<TrashRow>& out) {
  out.reset();
  Statement stmt;
  const ResultCode prepared = stmt.prepare(
      impl_->db, "SELECT id, subject, subject_id, deleted_at FROM trash WHERE subject = ? AND subject_id = ?;");
  if (prepared != ResultCode::Ok) {
    return prepared;
  }
  if (const ResultCode code = bind_text(stmt.get(), 1, subject); code != ResultCode::Ok) {
    return code;
  }
  if (const ResultCode code = bind_int64(stmt.get(), 2, subject_id); code != ResultCode::Ok) {
    return code;
  }
  const int rc = sqlite3_step(stmt.get());
  if (rc == SQLITE_DONE) {
    return ResultCode::Ok;
  }
  if (rc != SQLITE_ROW) {
    return map_sqlite(rc);
  }
  out = read_trash(stmt.get());
  return ResultCode::Ok;
}

ResultCode SqliteProjectStore::list_trash(std::vector<TrashRow>& out) {
  out.clear();
  Statement stmt;
  const ResultCode prepared =
      stmt.prepare(impl_->db, "SELECT id, subject, subject_id, deleted_at FROM trash ORDER BY id;");
  if (prepared != ResultCode::Ok) {
    return prepared;
  }
  while (true) {
    const int rc = sqlite3_step(stmt.get());
    if (rc == SQLITE_DONE) {
      return ResultCode::Ok;
    }
    if (rc != SQLITE_ROW) {
      return map_sqlite(rc);
    }
    out.push_back(read_trash(stmt.get()));
  }
}

ResultCode SqliteProjectStore::erase_trash(const std::string& subject, std::int64_t subject_id) {
  Statement stmt;
  const ResultCode prepared = stmt.prepare(impl_->db, "DELETE FROM trash WHERE subject = ? AND subject_id = ?;");
  if (prepared != ResultCode::Ok) {
    return prepared;
  }
  if (const ResultCode code = bind_text(stmt.get(), 1, subject); code != ResultCode::Ok) {
    return code;
  }
  if (const ResultCode code = bind_int64(stmt.get(), 2, subject_id); code != ResultCode::Ok) {
    return code;
  }
  const int rc = sqlite3_step(stmt.get());
  return rc == SQLITE_DONE ? ResultCode::Ok : map_sqlite(rc);
}

}  // namespace pixcake
