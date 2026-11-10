#include "pixcake/storage/sqlite_project_store.hpp"

#include <sqlite3.h>

#include <cctype>
#include <cstring>
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
    "source_identity_valid INTEGER NOT NULL DEFAULT 0,"
    "source_device INTEGER NOT NULL DEFAULT 0,"
    "source_inode INTEGER NOT NULL DEFAULT 0,"
    "source_size INTEGER NOT NULL DEFAULT 0,"
    "rating INTEGER NOT NULL DEFAULT 0,"
    "flag TEXT NOT NULL DEFAULT '',"
    "color_label TEXT NOT NULL DEFAULT '',"
    "format TEXT NOT NULL DEFAULT '',"
    "capture_time INTEGER NOT NULL DEFAULT 0,"
    "edit_status TEXT NOT NULL DEFAULT '',"
    "edited_at INTEGER NOT NULL DEFAULT 0,"
    "export_status TEXT NOT NULL DEFAULT '',"
    "FOREIGN KEY(project_id) REFERENCES project(id));"
    "CREATE TABLE IF NOT EXISTS trash ("
    "id INTEGER PRIMARY KEY,"
    "subject TEXT NOT NULL,"
    "subject_id INTEGER NOT NULL,"
    "deleted_at INTEGER NOT NULL);"
    "CREATE INDEX IF NOT EXISTS idx_photo_project ON photo(project_id);"
    "CREATE INDEX IF NOT EXISTS idx_photo_rating ON photo(rating);"
    "CREATE INDEX IF NOT EXISTS idx_photo_flag ON photo(flag);"
    "CREATE INDEX IF NOT EXISTS idx_photo_color ON photo(color_label);"
    "CREATE INDEX IF NOT EXISTS idx_trash_subject ON trash(subject, subject_id);"
    "CREATE TABLE IF NOT EXISTS project_import_receipt ("
    "receipt_key TEXT PRIMARY KEY, project_id INTEGER NOT NULL REFERENCES project(id) ON DELETE CASCADE);";

// 图库筛选和排序（需求 §6.2、§6.3，性能清单 P-12）：按项目取数据，再按排序列走索引，
// 同值按编号，避免临时排序。
constexpr const char* kPhotoQueryIndexes =
    "CREATE INDEX IF NOT EXISTS idx_photo_project_file_name ON photo(project_id, file_name, id);"
    "CREATE INDEX IF NOT EXISTS idx_photo_project_capture_time ON photo(project_id, capture_time, id);"
    "CREATE INDEX IF NOT EXISTS idx_photo_project_edited_at ON photo(project_id, edited_at, id);"
    "CREATE INDEX IF NOT EXISTS idx_photo_project_rating ON photo(project_id, rating, id);"
    "CREATE INDEX IF NOT EXISTS idx_photo_project_format ON photo(project_id, format, id);"
    "CREATE INDEX IF NOT EXISTS idx_photo_project_size ON photo(project_id, source_size, id);"
    "CREATE INDEX IF NOT EXISTS idx_photo_project_edit_status ON photo(project_id, edit_status, id);"
    "CREATE INDEX IF NOT EXISTS idx_photo_project_export_status ON photo(project_id, export_status, id);";

constexpr const char* kPhotoColumns =
    "id, project_id, absolute_path, file_name, access_credential, presence, source_identity_valid, source_device, "
    "source_inode, source_size, rating, flag, color_label, format, capture_time, edit_status, edited_at, "
    "export_status";

constexpr const char* kVirtualCopySchema =
    "CREATE TABLE IF NOT EXISTS virtual_copy ("
    "id INTEGER PRIMARY KEY,"
    "project_id INTEGER NOT NULL,"
    "source_photo_id INTEGER NOT NULL,"
    "sequence INTEGER NOT NULL,"
    "document_id TEXT NOT NULL,"
    "FOREIGN KEY(project_id) REFERENCES project(id),"
    "FOREIGN KEY(source_photo_id) REFERENCES photo(id));"
    "CREATE UNIQUE INDEX IF NOT EXISTS idx_virtual_copy_document ON virtual_copy(document_id);"
    "CREATE UNIQUE INDEX IF NOT EXISTS idx_virtual_copy_sequence "
    "ON virtual_copy(source_photo_id, sequence);";

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

bool has_column(sqlite3* db, const char* table, const char* column) {
  const char* sql = nullptr;
  if (std::strcmp(table, "photo") == 0) {
    sql = "PRAGMA table_info(photo);";
  } else if (std::strcmp(table, "project") == 0) {
    sql = "PRAGMA table_info(project);";
  } else if (std::strcmp(table, "virtual_copy") == 0) {
    sql = "PRAGMA table_info(virtual_copy);";
  } else if (std::strcmp(table, "trash") == 0) {
    sql = "PRAGMA table_info(trash);";
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

// 旧库没有准入时的内容格式，只能按扩展名回填；认不出的扩展名留空。
std::string format_from_extension(const std::string& file_name) {
  const auto dot = file_name.find_last_of('.');
  if (dot == std::string::npos) return "";
  std::string ext = file_name.substr(dot + 1);
  for (char& ch : ext) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  if (ext == "jpg" || ext == "jpeg") return "JPEG";
  if (ext == "png") return "PNG";
  if (ext == "tif" || ext == "tiff") return "TIFF";
  if (ext == "bmp") return "BMP";
  return "";
}

ResultCode backfill_formats(sqlite3* db) {
  std::vector<std::pair<std::int64_t, std::string>> rows;
  {
    Statement select;
    const ResultCode prepared = select.prepare(db, "SELECT id, file_name FROM photo WHERE format = '';");
    if (prepared != ResultCode::Ok) return prepared;
    int rc = SQLITE_ROW;
    while ((rc = sqlite3_step(select.get())) == SQLITE_ROW) {
      rows.emplace_back(sqlite3_column_int64(select.get(), 0), column_text(select.get(), 1));
    }
    if (rc != SQLITE_DONE) return map_sqlite(rc);
  }
  for (const auto& row : rows) {
    const std::string format = format_from_extension(row.second);
    if (format.empty()) continue;
    Statement update;
    const ResultCode prepared = update.prepare(db, "UPDATE photo SET format = ? WHERE id = ?;");
    if (prepared != ResultCode::Ok) return prepared;
    if (const ResultCode code = bind_text(update.get(), 1, format); code != ResultCode::Ok) return code;
    if (const ResultCode code = bind_int64(update.get(), 2, row.first); code != ResultCode::Ok) return code;
    const int rc = sqlite3_step(update.get());
    if (rc != SQLITE_DONE) return map_sqlite(rc);
  }
  return ResultCode::Ok;
}

ResultCode ensure_photo_columns(sqlite3* db) {
  struct Column { const char* name; const char* ddl; };
  const Column columns[] = {
      {"rating", "ALTER TABLE photo ADD COLUMN rating INTEGER NOT NULL DEFAULT 0;"},
      {"flag", "ALTER TABLE photo ADD COLUMN flag TEXT NOT NULL DEFAULT '';"},
      {"color_label", "ALTER TABLE photo ADD COLUMN color_label TEXT NOT NULL DEFAULT '';"},
      {"source_identity_valid", "ALTER TABLE photo ADD COLUMN source_identity_valid INTEGER NOT NULL DEFAULT 0;"},
      {"source_device", "ALTER TABLE photo ADD COLUMN source_device INTEGER NOT NULL DEFAULT 0;"},
      {"source_inode", "ALTER TABLE photo ADD COLUMN source_inode INTEGER NOT NULL DEFAULT 0;"},
      {"source_size", "ALTER TABLE photo ADD COLUMN source_size INTEGER NOT NULL DEFAULT 0;"},
      {"format", "ALTER TABLE photo ADD COLUMN format TEXT NOT NULL DEFAULT '';"},
      {"capture_time", "ALTER TABLE photo ADD COLUMN capture_time INTEGER NOT NULL DEFAULT 0;"},
      {"edit_status", "ALTER TABLE photo ADD COLUMN edit_status TEXT NOT NULL DEFAULT '';"},
      {"edited_at", "ALTER TABLE photo ADD COLUMN edited_at INTEGER NOT NULL DEFAULT 0;"},
      {"export_status", "ALTER TABLE photo ADD COLUMN export_status TEXT NOT NULL DEFAULT '';"},
  };
  bool format_added = false;
  for (const Column& column : columns) {
    if (!has_column(db, "photo", column.name)) {
      const ResultCode added = exec_sql(db, column.ddl);
      if (added != ResultCode::Ok) return added;
      format_added = format_added || std::string(column.name) == "format";
    }
  }
  if (format_added) {
    const ResultCode filled = backfill_formats(db);
    if (filled != ResultCode::Ok) return filled;
  }
  const ResultCode marks = exec_sql(db, "CREATE INDEX IF NOT EXISTS idx_photo_rating ON photo(rating);"
                                        "CREATE INDEX IF NOT EXISTS idx_photo_flag ON photo(flag);"
                                        "CREATE INDEX IF NOT EXISTS idx_photo_color ON photo(color_label);");
  if (marks != ResultCode::Ok) return marks;
  return exec_sql(db, kPhotoQueryIndexes);
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
  row.source_identity.valid = sqlite3_column_int(stmt, 6) != 0;
  row.source_identity.device = static_cast<std::uint64_t>(sqlite3_column_int64(stmt, 7));
  row.source_identity.inode = static_cast<std::uint64_t>(sqlite3_column_int64(stmt, 8));
  row.source_identity.size = static_cast<std::uint64_t>(sqlite3_column_int64(stmt, 9));
  row.rating = sqlite3_column_int(stmt, 10);
  row.flag = column_text(stmt, 11);
  row.color_label = column_text(stmt, 12);
  row.format = column_text(stmt, 13);
  row.capture_time = sqlite3_column_int64(stmt, 14);
  row.edit_status = column_text(stmt, 15);
  row.edited_at = sqlite3_column_int64(stmt, 16);
  row.export_status = column_text(stmt, 17);
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

VirtualCopyRow read_virtual_copy(sqlite3_stmt* stmt) {
  VirtualCopyRow row;
  row.id = sqlite3_column_int64(stmt, 0);
  row.project_id = sqlite3_column_int64(stmt, 1);
  row.source_photo_id = sqlite3_column_int64(stmt, 2);
  row.sequence = sqlite3_column_int64(stmt, 3);
  row.document_id = column_text(stmt, 4);
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
  if (version != 0 && version != 1 && version != 2 && version != 3) {
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
    const ResultCode copies = exec_sql(db, kVirtualCopySchema);
    if (copies != ResultCode::Ok) {
      sqlite3_close(db);
      return copies;
    }
    const ResultCode indexes = exec_sql(db, kPhotoQueryIndexes);
    if (indexes != ResultCode::Ok) {
      sqlite3_close(db);
      return indexes;
    }
    const ResultCode stamp = exec_sql(db, "PRAGMA user_version=3;");
    if (stamp != ResultCode::Ok) {
      sqlite3_close(db);
      return stamp;
    }
  } else {
    const ResultCode began = exec_sql(db, "BEGIN IMMEDIATE;");
    if (began != ResultCode::Ok) {
      sqlite3_close(db);
      return began;
    }
    ResultCode migrated = ResultCode::Ok;
    if (version == 1) migrated = exec_sql(db, kVirtualCopySchema);
    if (migrated == ResultCode::Ok) migrated = ensure_photo_columns(db);
    if (migrated == ResultCode::Ok && version != 3) migrated = exec_sql(db, "PRAGMA user_version=3;");
    if (migrated != ResultCode::Ok) {
      exec_sql(db, "ROLLBACK;");
      sqlite3_close(db);
      return migrated;
    }
    const ResultCode committed = exec_sql(db, "COMMIT;");
    if (committed != ResultCode::Ok) {
      exec_sql(db, "ROLLBACK;");
      sqlite3_close(db);
      return committed;
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
  ResultCode code = ResultCode::IoFailed;
  try { code = body(); }
  catch (...) { code = ResultCode::IoFailed; }
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

ResultCode SqliteProjectStore::update_project_origin(std::int64_t id, const std::string& origin) {
  Statement statement;
  auto code = statement.prepare(impl_->db, "UPDATE project SET origin=?,cloud_marked=1 WHERE id=?;");
  if (code != ResultCode::Ok) return code;
  sqlite3_bind_text(statement.get(), 1, origin.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int64(statement.get(), 2, id);
  return map_sqlite(sqlite3_step(statement.get()));
}

ResultCode SqliteProjectStore::load_import_receipt(const std::string& key, std::int64_t& id) {
  id = 0;
  Statement statement;
  auto code = statement.prepare(impl_->db, "SELECT project_id FROM project_import_receipt WHERE receipt_key=?;");
  if (code != ResultCode::Ok) return code;
  sqlite3_bind_text(statement.get(), 1, key.c_str(), -1, SQLITE_TRANSIENT);
  const int result = sqlite3_step(statement.get());
  if (result == SQLITE_ROW) id = sqlite3_column_int64(statement.get(), 0);
  return map_sqlite(result);
}

ResultCode SqliteProjectStore::insert_import_receipt(const std::string& key, std::int64_t id) {
  Statement statement;
  auto code = statement.prepare(impl_->db, "INSERT INTO project_import_receipt VALUES(?,?);");
  if (code != ResultCode::Ok) return code;
  sqlite3_bind_text(statement.get(), 1, key.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int64(statement.get(), 2, id);
  return map_sqlite(sqlite3_step(statement.get()));
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
    Statement copy_trash;
    ResultCode code = copy_trash.prepare(
        impl_->db,
        "DELETE FROM trash WHERE subject = ? AND subject_id IN (SELECT id FROM virtual_copy WHERE project_id = ?);");
    if (code != ResultCode::Ok) {
      return code;
    }
    code = bind_text(copy_trash.get(), 1, kTrashVirtualCopy);
    if (code != ResultCode::Ok) {
      return code;
    }
    code = bind_int64(copy_trash.get(), 2, project_id);
    if (code != ResultCode::Ok) {
      return code;
    }
    const int removed_copy_trash = sqlite3_step(copy_trash.get());
    if (removed_copy_trash != SQLITE_DONE) {
      return map_sqlite(removed_copy_trash);
    }

    Statement copy_rows;
    code = copy_rows.prepare(impl_->db, "DELETE FROM virtual_copy WHERE project_id = ?;");
    if (code != ResultCode::Ok) {
      return code;
    }
    code = bind_int64(copy_rows.get(), 1, project_id);
    if (code != ResultCode::Ok) {
      return code;
    }
    const int removed_copies = sqlite3_step(copy_rows.get());
    if (removed_copies != SQLITE_DONE) {
      return map_sqlite(removed_copies);
    }

    Statement photos;
    code = photos.prepare(
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
      "INSERT INTO photo(project_id, absolute_path, file_name, access_credential, presence, source_identity_valid, source_device, source_inode, source_size, format, capture_time) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);");
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
  if (const ResultCode code = map_sqlite(sqlite3_bind_int(stmt.get(), 6, draft.source_identity.valid ? 1 : 0)); code != ResultCode::Ok) return code;
  if (const ResultCode code = bind_int64(stmt.get(), 7, static_cast<std::int64_t>(draft.source_identity.device)); code != ResultCode::Ok) return code;
  if (const ResultCode code = bind_int64(stmt.get(), 8, static_cast<std::int64_t>(draft.source_identity.inode)); code != ResultCode::Ok) return code;
  if (const ResultCode code = bind_int64(stmt.get(), 9, static_cast<std::int64_t>(draft.source_identity.size)); code != ResultCode::Ok) return code;
  if (const ResultCode code = bind_text(stmt.get(), 10, draft.format); code != ResultCode::Ok) return code;
  if (const ResultCode code = bind_int64(stmt.get(), 11, draft.capture_time); code != ResultCode::Ok) return code;
  const int rc = sqlite3_step(stmt.get());
  if (rc != SQLITE_DONE) {
    return map_sqlite(rc);
  }
  id = sqlite3_last_insert_rowid(impl_->db);
  return ResultCode::Ok;
}

ResultCode SqliteProjectStore::update_photo_rating(std::int64_t id, int rating) {
  Statement stmt;
  const ResultCode prepared = stmt.prepare(impl_->db, "UPDATE photo SET rating = ? WHERE id = ?;");
  if (prepared != ResultCode::Ok) {
    return prepared;
  }
  if (const ResultCode code = bind_int64(stmt.get(), 1, rating); code != ResultCode::Ok) {
    return code;
  }
  if (const ResultCode code = bind_int64(stmt.get(), 2, id); code != ResultCode::Ok) {
    return code;
  }
  const int rc = sqlite3_step(stmt.get());
  return rc == SQLITE_DONE ? ResultCode::Ok : map_sqlite(rc);
}

ResultCode SqliteProjectStore::update_photo_flag(std::int64_t id, const std::string& flag) {
  Statement stmt;
  const ResultCode prepared = stmt.prepare(impl_->db, "UPDATE photo SET flag = ? WHERE id = ?;");
  if (prepared != ResultCode::Ok) {
    return prepared;
  }
  if (const ResultCode code = bind_text(stmt.get(), 1, flag); code != ResultCode::Ok) {
    return code;
  }
  if (const ResultCode code = bind_int64(stmt.get(), 2, id); code != ResultCode::Ok) {
    return code;
  }
  const int rc = sqlite3_step(stmt.get());
  return rc == SQLITE_DONE ? ResultCode::Ok : map_sqlite(rc);
}

ResultCode SqliteProjectStore::update_photo_color(std::int64_t id, const std::string& color) {
  Statement stmt;
  const ResultCode prepared = stmt.prepare(impl_->db, "UPDATE photo SET color_label = ? WHERE id = ?;");
  if (prepared != ResultCode::Ok) {
    return prepared;
  }
  if (const ResultCode code = bind_text(stmt.get(), 1, color); code != ResultCode::Ok) {
    return code;
  }
  if (const ResultCode code = bind_int64(stmt.get(), 2, id); code != ResultCode::Ok) {
    return code;
  }
  const int rc = sqlite3_step(stmt.get());
  return rc == SQLITE_DONE ? ResultCode::Ok : map_sqlite(rc);
}

ResultCode SqliteProjectStore::update_photo_ratings(const std::vector<std::int64_t>& ids, int rating) {
  if (ids.empty()) return ResultCode::StateConflict;
  Statement stmt;
  const ResultCode prepared = stmt.prepare(impl_->db, "UPDATE photo SET rating = ? WHERE id = ?;");
  if (prepared != ResultCode::Ok) return prepared;
  return in_transaction([&] {
    for (const auto id : ids) {
      sqlite3_reset(stmt.get()); sqlite3_clear_bindings(stmt.get());
      if (bind_int64(stmt.get(), 1, rating) != ResultCode::Ok || bind_int64(stmt.get(), 2, id) != ResultCode::Ok) return ResultCode::IoFailed;
      if (sqlite3_step(stmt.get()) != SQLITE_DONE || sqlite3_changes(impl_->db) == 0) return ResultCode::StateConflict;
    }
    return ResultCode::Ok;
  });
}

ResultCode SqliteProjectStore::update_photo_flags(const std::vector<std::int64_t>& ids, const std::string& flag) {
  if (ids.empty()) return ResultCode::StateConflict;
  Statement stmt;
  const ResultCode prepared = stmt.prepare(impl_->db, "UPDATE photo SET flag = ? WHERE id = ?;");
  if (prepared != ResultCode::Ok) return prepared;
  return in_transaction([&] {
    for (const auto id : ids) {
      sqlite3_reset(stmt.get()); sqlite3_clear_bindings(stmt.get());
      if (bind_text(stmt.get(), 1, flag) != ResultCode::Ok || bind_int64(stmt.get(), 2, id) != ResultCode::Ok) return ResultCode::IoFailed;
      if (sqlite3_step(stmt.get()) != SQLITE_DONE || sqlite3_changes(impl_->db) == 0) return ResultCode::StateConflict;
    }
    return ResultCode::Ok;
  });
}

ResultCode SqliteProjectStore::update_photo_colors(const std::vector<std::int64_t>& ids, const std::string& color) {
  if (ids.empty()) return ResultCode::StateConflict;
  Statement stmt;
  const ResultCode prepared = stmt.prepare(impl_->db, "UPDATE photo SET color_label = ? WHERE id = ?;");
  if (prepared != ResultCode::Ok) return prepared;
  return in_transaction([&] {
    for (const auto id : ids) {
      sqlite3_reset(stmt.get()); sqlite3_clear_bindings(stmt.get());
      if (bind_text(stmt.get(), 1, color) != ResultCode::Ok || bind_int64(stmt.get(), 2, id) != ResultCode::Ok) return ResultCode::IoFailed;
      if (sqlite3_step(stmt.get()) != SQLITE_DONE || sqlite3_changes(impl_->db) == 0) return ResultCode::StateConflict;
    }
    return ResultCode::Ok;
  });
}

ResultCode SqliteProjectStore::update_photo_access_credential(std::int64_t id,
                                                              const std::vector<std::uint8_t>& credential) {
  Statement stmt;
  const ResultCode prepared = stmt.prepare(impl_->db, "UPDATE photo SET access_credential = ? WHERE id = ?;");
  if (prepared != ResultCode::Ok) {
    return prepared;
  }
  if (const ResultCode code = bind_blob(stmt.get(), 1, credential); code != ResultCode::Ok) {
    return code;
  }
  if (const ResultCode code = bind_int64(stmt.get(), 2, id); code != ResultCode::Ok) {
    return code;
  }
  const int rc = sqlite3_step(stmt.get());
  return rc == SQLITE_DONE ? ResultCode::Ok : map_sqlite(rc);
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
  const std::string sql = std::string("SELECT ") + kPhotoColumns + " FROM photo WHERE id = ?;";
  const ResultCode prepared = stmt.prepare(impl_->db, sql.c_str());
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
  const std::string sql = std::string("SELECT ") + kPhotoColumns + " FROM photo" +
                          (project_id.has_value() ? " WHERE project_id = ?" : "") + " ORDER BY id;";
  const ResultCode prepared = stmt.prepare(impl_->db, sql.c_str());
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

namespace {

// 排序列只从这张源码常量表里取，不拼接调用方给的文字。
const char* sort_column(PhotoSortKey key) {
  switch (key) {
    case PhotoSortKey::ImportOrder: return nullptr;
    case PhotoSortKey::CaptureTime: return "capture_time";
    case PhotoSortKey::EditTime: return "edited_at";
    case PhotoSortKey::Rating: return "rating";
    case PhotoSortKey::FileType: return "format";
    case PhotoSortKey::FileSize: return "source_size";
    case PhotoSortKey::FileName: return "file_name";
  }
  return nullptr;
}

struct PhotoQuerySql {
  std::string text;
  std::vector<std::int64_t> numbers;  // 先绑定数值，再绑定文字
  std::vector<std::string> texts;
  std::vector<bool> order;            // true 为数值
};

void add_in(PhotoQuerySql& sql, const char* column, const std::vector<std::string>& values) {
  if (values.empty()) return;
  sql.text += std::string(" AND ") + column + " IN (";
  for (std::size_t index = 0; index < values.size(); ++index) {
    sql.text += index == 0 ? "?" : ", ?";
    sql.texts.push_back(values[index]);
    sql.order.push_back(false);
  }
  sql.text += ")";
}

PhotoQuerySql photo_query_sql(const PhotoQuery& query) {
  PhotoQuerySql sql;
  sql.text = std::string("SELECT ") + kPhotoColumns + " FROM photo WHERE 1 = 1";
  if (query.project_id.has_value()) {
    sql.text += " AND project_id = ?";
    sql.numbers.push_back(*query.project_id);
    sql.order.push_back(true);
  }
  if (query.rating_min != kRatingMin || query.rating_max != kRatingMax) {
    sql.text += " AND rating BETWEEN ? AND ?";
    sql.numbers.push_back(query.rating_min);
    sql.order.push_back(true);
    sql.numbers.push_back(query.rating_max);
    sql.order.push_back(true);
  }
  add_in(sql, "flag", query.flags);
  add_in(sql, "color_label", query.colors);
  add_in(sql, "format", query.formats);
  add_in(sql, "edit_status", query.edit_statuses);
  add_in(sql, "export_status", query.export_statuses);
  const char* direction = query.descending ? " DESC" : " ASC";
  sql.text += " ORDER BY ";
  if (const char* column = sort_column(query.sort)) {
    sql.text += std::string(column) + direction + ", ";
  }
  sql.text += std::string("id") + direction + ";";
  return sql;
}

ResultCode bind_photo_query(sqlite3_stmt* stmt, const PhotoQuerySql& sql) {
  std::size_t number = 0;
  std::size_t text = 0;
  for (std::size_t index = 0; index < sql.order.size(); ++index) {
    const int slot = static_cast<int>(index) + 1;
    const ResultCode code =
        sql.order[index] ? bind_int64(stmt, slot, sql.numbers[number++]) : bind_text(stmt, slot, sql.texts[text++]);
    if (code != ResultCode::Ok) return code;
  }
  return ResultCode::Ok;
}

}  // namespace

ResultCode SqliteProjectStore::query_photos(const PhotoQuery& query, std::vector<PhotoRow>& out) {
  out.clear();
  const PhotoQuerySql sql = photo_query_sql(query);
  Statement stmt;
  if (const ResultCode prepared = stmt.prepare(impl_->db, sql.text.c_str()); prepared != ResultCode::Ok) {
    return prepared;
  }
  if (const ResultCode bound = bind_photo_query(stmt.get(), sql); bound != ResultCode::Ok) return bound;
  while (true) {
    const int rc = sqlite3_step(stmt.get());
    if (rc == SQLITE_DONE) return ResultCode::Ok;
    if (rc != SQLITE_ROW) return map_sqlite(rc);
    out.push_back(read_photo(stmt.get()));
  }
}

ResultCode SqliteProjectStore::explain_photo_query(const PhotoQuery& query, std::vector<std::string>& plan) {
  plan.clear();
  const PhotoQuerySql sql = photo_query_sql(query);
  Statement stmt;
  const std::string text = "EXPLAIN QUERY PLAN " + sql.text;
  if (const ResultCode prepared = stmt.prepare(impl_->db, text.c_str()); prepared != ResultCode::Ok) {
    return prepared;
  }
  if (const ResultCode bound = bind_photo_query(stmt.get(), sql); bound != ResultCode::Ok) return bound;
  while (true) {
    const int rc = sqlite3_step(stmt.get());
    if (rc == SQLITE_DONE) return ResultCode::Ok;
    if (rc != SQLITE_ROW) return map_sqlite(rc);
    plan.push_back(column_text(stmt.get(), 3));
  }
}

ResultCode SqliteProjectStore::update_photo_edit_state(std::int64_t id, const std::string& edit_status,
                                                       std::int64_t edited_at) {
  Statement stmt;
  const ResultCode prepared = stmt.prepare(impl_->db, "UPDATE photo SET edit_status = ?, edited_at = ? WHERE id = ?;");
  if (prepared != ResultCode::Ok) return prepared;
  if (const ResultCode code = bind_text(stmt.get(), 1, edit_status); code != ResultCode::Ok) return code;
  if (const ResultCode code = bind_int64(stmt.get(), 2, edited_at); code != ResultCode::Ok) return code;
  if (const ResultCode code = bind_int64(stmt.get(), 3, id); code != ResultCode::Ok) return code;
  const int rc = sqlite3_step(stmt.get());
  return rc == SQLITE_DONE ? ResultCode::Ok : map_sqlite(rc);
}

ResultCode SqliteProjectStore::update_photo_export_status(std::int64_t id, const std::string& export_status) {
  Statement stmt;
  const ResultCode prepared = stmt.prepare(impl_->db, "UPDATE photo SET export_status = ? WHERE id = ?;");
  if (prepared != ResultCode::Ok) return prepared;
  if (const ResultCode code = bind_text(stmt.get(), 1, export_status); code != ResultCode::Ok) return code;
  if (const ResultCode code = bind_int64(stmt.get(), 2, id); code != ResultCode::Ok) return code;
  const int rc = sqlite3_step(stmt.get());
  return rc == SQLITE_DONE ? ResultCode::Ok : map_sqlite(rc);
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

ResultCode SqliteProjectStore::insert_virtual_copy(const VirtualCopyDraft& draft, std::int64_t& id) {
  Statement stmt;
  const ResultCode prepared = stmt.prepare(
      impl_->db,
      "INSERT INTO virtual_copy(project_id, source_photo_id, sequence, document_id) VALUES (?, ?, ?, ?);");
  if (prepared != ResultCode::Ok) {
    return prepared;
  }
  if (const ResultCode code = bind_int64(stmt.get(), 1, draft.project_id); code != ResultCode::Ok) {
    return code;
  }
  if (const ResultCode code = bind_int64(stmt.get(), 2, draft.source_photo_id); code != ResultCode::Ok) {
    return code;
  }
  if (const ResultCode code = bind_int64(stmt.get(), 3, draft.sequence); code != ResultCode::Ok) {
    return code;
  }
  if (const ResultCode code = bind_text(stmt.get(), 4, draft.document_id); code != ResultCode::Ok) {
    return code;
  }
  const int rc = sqlite3_step(stmt.get());
  if (rc != SQLITE_DONE) {
    return map_sqlite(rc);
  }
  id = sqlite3_last_insert_rowid(impl_->db);
  return ResultCode::Ok;
}

ResultCode SqliteProjectStore::load_virtual_copy(std::int64_t id, std::optional<VirtualCopyRow>& out) {
  out.reset();
  Statement stmt;
  const ResultCode prepared = stmt.prepare(
      impl_->db,
      "SELECT id, project_id, source_photo_id, sequence, document_id FROM virtual_copy WHERE id = ?;");
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
  out = read_virtual_copy(stmt.get());
  return ResultCode::Ok;
}

ResultCode SqliteProjectStore::load_virtual_copy_by_document(const std::string& document_id,
                                                            std::optional<VirtualCopyRow>& out) {
  out.reset();
  Statement stmt;
  const ResultCode prepared = stmt.prepare(
      impl_->db,
      "SELECT id, project_id, source_photo_id, sequence, document_id FROM virtual_copy WHERE document_id = ?;");
  if (prepared != ResultCode::Ok) {
    return prepared;
  }
  if (const ResultCode code = bind_text(stmt.get(), 1, document_id); code != ResultCode::Ok) {
    return code;
  }
  const int rc = sqlite3_step(stmt.get());
  if (rc == SQLITE_DONE) {
    return ResultCode::Ok;
  }
  if (rc != SQLITE_ROW) {
    return map_sqlite(rc);
  }
  out = read_virtual_copy(stmt.get());
  return ResultCode::Ok;
}

ResultCode SqliteProjectStore::list_virtual_copies(std::int64_t source_photo_id, std::vector<VirtualCopyRow>& out) {
  out.clear();
  Statement stmt;
  const ResultCode prepared = stmt.prepare(
      impl_->db,
      "SELECT id, project_id, source_photo_id, sequence, document_id FROM virtual_copy "
      "WHERE source_photo_id = ? ORDER BY sequence;");
  if (prepared != ResultCode::Ok) {
    return prepared;
  }
  if (const ResultCode code = bind_int64(stmt.get(), 1, source_photo_id); code != ResultCode::Ok) {
    return code;
  }
  while (true) {
    const int rc = sqlite3_step(stmt.get());
    if (rc == SQLITE_DONE) {
      return ResultCode::Ok;
    }
    if (rc != SQLITE_ROW) {
      return map_sqlite(rc);
    }
    out.push_back(read_virtual_copy(stmt.get()));
  }
}

ResultCode SqliteProjectStore::erase_virtual_copy(std::int64_t id) {
  Statement stmt;
  const ResultCode prepared = stmt.prepare(impl_->db, "DELETE FROM virtual_copy WHERE id = ?;");
  if (prepared != ResultCode::Ok) {
    return prepared;
  }
  if (const ResultCode code = bind_int64(stmt.get(), 1, id); code != ResultCode::Ok) {
    return code;
  }
  const int rc = sqlite3_step(stmt.get());
  return rc == SQLITE_DONE ? ResultCode::Ok : map_sqlite(rc);
}

ResultCode SqliteProjectStore::erase_virtual_copies_of_photo(std::int64_t photo_id) {
  return in_transaction([this, photo_id] {
    Statement trash;
    ResultCode code = trash.prepare(
        impl_->db,
        "DELETE FROM trash WHERE subject = ? AND subject_id IN (SELECT id FROM virtual_copy WHERE source_photo_id = ?);");
    if (code != ResultCode::Ok) {
      return code;
    }
    code = bind_text(trash.get(), 1, kTrashVirtualCopy);
    if (code != ResultCode::Ok) {
      return code;
    }
    code = bind_int64(trash.get(), 2, photo_id);
    if (code != ResultCode::Ok) {
      return code;
    }
    const int removed_trash = sqlite3_step(trash.get());
    if (removed_trash != SQLITE_DONE) {
      return map_sqlite(removed_trash);
    }

    Statement copies;
    code = copies.prepare(impl_->db, "DELETE FROM virtual_copy WHERE source_photo_id = ?;");
    if (code != ResultCode::Ok) {
      return code;
    }
    code = bind_int64(copies.get(), 1, photo_id);
    if (code != ResultCode::Ok) {
      return code;
    }
    const int removed = sqlite3_step(copies.get());
    return removed == SQLITE_DONE ? ResultCode::Ok : map_sqlite(removed);
  });
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
