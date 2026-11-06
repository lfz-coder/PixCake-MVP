#include "pixcake/persist/atomic_file.hpp"

#include "pixcake/persist/write_lock.hpp"

#include <filesystem>

namespace pixcake {
namespace {

std::string parent_directory(const std::string& path) {
  const std::filesystem::path file(path);
  const auto parent = file.parent_path();
  if (parent.empty()) {
    return ".";
  }
  return parent.string();
}

std::string file_name(const std::string& path) {
  return std::filesystem::path(path).filename().string();
}

ResultCode quarantine_file(FileSystem& files, const std::string& formal_path) {
  if (!files.exists(formal_path)) {
    return ResultCode::Ok;
  }
  const std::string isolate = parent_directory(formal_path) + "/isolate";
  const IoResult made = files.create_directory(isolate);
  if (!made.ok) {
    return made.code;
  }
  const std::string base = file_name(formal_path);
  if (base.empty()) {
    return ResultCode::IoFailed;
  }
  std::string dest = isolate + "/" + base;
  for (int suffix = 0; files.exists(dest); ++suffix) {
    if (suffix == 10000) {
      return ResultCode::IoFailed;
    }
    dest = isolate + "/" + base + "." + std::to_string(suffix + 1);
  }
  const IoResult renamed = files.rename_file(formal_path, dest);
  if (renamed.not_found) {
    return ResultCode::Ok;
  }
  if (!renamed.ok) {
    return renamed.code;
  }
  return ResultCode::Ok;
}

bool retry_rename(FileSystem& files, const std::string& from, const std::string& to, IoResult& last) {
  for (int attempt = 0; attempt < 8; ++attempt) {
    last = files.rename_file(from, to);
    if (last.ok || last.not_found) return last.ok;
  }
  return false;
}

bool restore_backup(FileSystem& files, const std::string& formal, const std::string& backup) {
  IoResult restored;
  if (retry_rename(files, backup, formal, restored)) {
    return true;
  }
  // A rename can remain unavailable after the first error (for example when
  // a filesystem briefly rejects metadata changes).  The last good bytes are
  // still durable in the backup, so recreate the missing formal file without
  // truncating an existing path.  Keeping the backup is intentional: it is a
  // second recovery source if the final sync fails.
  if (files.exists(formal)) {
    return true;
  }
  std::vector<std::uint8_t> bytes;
  const IoResult read = files.read_file(backup, bytes);
  if (!read.ok) {
    return false;
  }
  const IoResult written = files.write_new(formal, bytes);
  if (!written.ok) {
    return false;
  }
  const IoResult synced = files.sync_file(formal);
  return synced.ok;
}

}  // namespace

ResultCode atomic_replace(FileSystem& files, const std::string& formal_path,
                          const std::vector<std::uint8_t>& bytes) {
  WriteScope scope(files, formal_path);
  if (!scope) {
    return scope.code();
  }
  const std::string temp = formal_path + ".tmp";
  const std::string backup = formal_path + ".bak";
  if (files.exists(temp)) {
    const IoResult removed = files.remove_file(temp);
    if (!removed.ok && !removed.not_found) {
      return removed.code;
    }
  }

  const IoResult written = files.write_new(temp, bytes);
  if (!written.ok) {
    files.remove_file(temp);
    return written.code;
  }
  const IoResult synced = files.sync_file(temp);
  if (!synced.ok) {
    files.remove_file(temp);
    return synced.code;
  }

  const bool had_formal = files.exists(formal_path);
  if (had_formal) {
    const IoResult moved = files.rename_file(formal_path, backup);
    if (!moved.ok) {
      files.remove_file(temp);
      return moved.code;
    }
  }

  const IoResult published = files.rename_file(temp, formal_path);
  if (!published.ok) {
    if (had_formal) {
      restore_backup(files, formal_path, backup);
    }
    files.remove_file(temp);
    return published.code;
  }

  const IoResult directory = files.sync_directory(parent_directory(formal_path));
  if (!directory.ok) {
    if (had_formal && files.exists(backup)) {
      IoResult moved;
      retry_rename(files, formal_path, temp, moved);
      restore_backup(files, formal_path, backup);
      files.remove_file(temp);
    } else if (!had_formal) {
      files.remove_file(formal_path);
    }
    return directory.code;
  }
  return ResultCode::Ok;
}

DurableLoad open_durable_file(FileSystem& files, const std::string& formal_path,
                              const std::function<bool(const std::vector<std::uint8_t>&)>& valid) {
  DurableLoad loaded;
  WriteScope scope(files, formal_path);
  if (!scope) {
    loaded.code = scope.code();
    return loaded;
  }
  const std::string backup = formal_path + ".bak";
  const std::string marker = formal_path + ".recovered";
  if (!files.exists(formal_path) && !files.exists(backup) && files.exists(marker)) {
    loaded.recovered_empty = true;
    loaded.recovery_marker = true;
    loaded.code = ResultCode::DocumentCorrupt;
    return loaded;
  }
  if (!files.exists(formal_path) && !files.exists(backup)) {
    loaded.absent = true;
    loaded.code = ResultCode::IoFailed;
    return loaded;
  }

  if (files.exists(formal_path)) {
    std::vector<std::uint8_t> bytes;
    const IoResult read = files.read_file(formal_path, bytes);
    if (!read.ok) {
      if (!read.not_found) {
        loaded.code = read.code;
        return loaded;
      }
    } else if (valid(bytes)) {
      loaded.code = ResultCode::Ok;
      loaded.bytes = std::move(bytes);
      return loaded;
    } else {
      const ResultCode quarantined = quarantine_file(files, formal_path);
      if (quarantined != ResultCode::Ok) {
        loaded.code = quarantined;
        return loaded;
      }
    }
  }

  if (files.exists(backup)) {
    std::vector<std::uint8_t> bytes;
    const IoResult read = files.read_file(backup, bytes);
    if (!read.ok) {
      if (!read.not_found) {
        loaded.code = read.code;
        return loaded;
      }
    } else if (valid(bytes)) {
      const IoResult restored = files.rename_file(backup, formal_path);
      if (!restored.ok) {
        loaded.code = restored.code;
        return loaded;
      }
      loaded.code = ResultCode::Ok;
      loaded.used_backup = true;
      loaded.bytes = std::move(bytes);
      return loaded;
    } else {
      const ResultCode quarantined = quarantine_file(files, backup);
      if (quarantined != ResultCode::Ok) {
        loaded.code = quarantined;
        return loaded;
      }
    }
  }

  loaded.recovered_empty = true;
  loaded.code = ResultCode::DocumentCorrupt;
  const IoResult marked = files.write_new(marker, {});
  if (!marked.ok && !marked.not_found) {
    loaded.code = marked.code;
  } else {
    loaded.recovery_marker = true;
  }
  return loaded;
}

ResultCode check_durable_revision(
    FileSystem& files, const std::string& formal_path, std::uint64_t expected_revision,
    const std::function<std::optional<std::uint64_t>(const std::vector<std::uint8_t>&)>& revision_of) {
  const DurableLoad durable = open_durable_file(
      files, formal_path, [&revision_of](const std::vector<std::uint8_t>& bytes) {
        return revision_of(bytes).has_value();
      });
  if (durable.absent || durable.recovered_empty) {
    return ResultCode::Ok;
  }
  if (durable.code != ResultCode::Ok) {
    return durable.code;
  }
  const std::optional<std::uint64_t> revision = revision_of(durable.bytes);
  if (!revision.has_value() || *revision != expected_revision) {
    return ResultCode::StateConflict;
  }
  return ResultCode::Ok;
}

}  // namespace pixcake
