#include "pixcake/persist/atomic_file.hpp"

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

}  // namespace

ResultCode atomic_replace(FileSystem& files, const std::string& formal_path,
                          const std::vector<std::uint8_t>& bytes) {
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
      files.rename_file(backup, formal_path);
    }
    files.remove_file(temp);
    return published.code;
  }

  const IoResult directory = files.sync_directory(parent_directory(formal_path));
  if (!directory.ok) {
    if (had_formal && files.exists(backup)) {
      files.rename_file(formal_path, temp);
      files.rename_file(backup, formal_path);
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
  const std::string backup = formal_path + ".bak";
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
  return loaded;
}

}  // namespace pixcake
