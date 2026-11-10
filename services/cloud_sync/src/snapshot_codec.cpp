#include "sync_internal.hpp"
#include "pixcake/editdoc/edit_document.hpp"
#include "pixcake/layerdoc/layer_document.hpp"
#include "pixcake/project/records.hpp"
#include <algorithm>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cstdio>
#include <set>

namespace pixcake::cloud_sync {
namespace {
void fields(const Json& value, std::initializer_list<const char*> names) {
  require(value.is_object() && value.size() == names.size());
  for (const auto name : names) require(value.contains(name));
}
std::string text(const Json& value) { require(value.is_string()); return value.get<std::string>(); }
std::uint64_t number(const Json& value, std::uint64_t maximum) {
  require(value.is_number_integer() && (value.is_number_unsigned() || value.get<std::int64_t>() >= 0));
  const auto n = value.get<std::uint64_t>(); require(n <= maximum); return n;
}
bool name_ok(const std::string& name) {
  return !name.empty() && name.size() <= 1024 &&
    std::none_of(name.begin(), name.end(), [](unsigned char c) { return c < 32 || c == 127; });
}
struct Descriptor { int fd; ~Descriptor() { if (fd >= 0) ::close(fd); } };
void put(int fd, const Bytes& bytes) {
  std::size_t offset = 0;
  while (offset < bytes.size()) {
    const auto count = ::write(fd, bytes.data() + offset, bytes.size() - offset);
    if (count < 0 && errno == EINTR) continue;
    require(count > 0, errno == ENOSPC ? ResultCode::DiskFull : ResultCode::IoFailed);
    offset += static_cast<std::size_t>(count);
  }
}
}
Json snapshot_json(const project_session::ProjectSnapshot& snapshot) {
  Json photos = Json::array();
  for (const auto& photo : snapshot.photos) {
    Json items = Json::array();
    for (const auto& item : photo.items) items.push_back({{"edit", item.edit_file}, {"layer", item.layer_file}});
    photos.push_back({{"source", photo.source_file}, {"name", photo.name}, {"rating", photo.rating},
                      {"flag", photo.flag}, {"color", photo.color}, {"items", std::move(items)}});
  }
  return {{"format", 1}, {"name", snapshot.name}, {"photos", std::move(photos)}, {"files", Json::array()}};
}
project_session::ProjectSnapshot snapshot_from_json(const Json& json) {
  project_session::ProjectSnapshot snapshot; snapshot.name = text(json.at("name"));
  for (const auto& row : json.at("photos")) {
    project_session::SnapshotPhoto photo;
    photo.source_file = text(row.at("source")); photo.name = text(row.at("name"));
    photo.rating = static_cast<int>(number(row.at("rating"), 5));
    photo.flag = text(row.at("flag")); photo.color = text(row.at("color"));
    for (const auto& item : row.at("items")) photo.items.push_back({text(item.at("edit")), text(item.at("layer"))});
    snapshot.photos.push_back(std::move(photo));
  }
  return snapshot;
}
std::map<std::string, std::size_t> chunks(const Json& manifest) {
  std::map<std::string, std::size_t> result;
  for (const auto& file : manifest.at("files")) {
    auto remaining = number(file.at("size"), kMaximum);
    for (const auto& entry : file.at("chunks")) {
      const auto hash = text(entry); require(digest_valid(hash) && remaining > 0);
      const auto length = static_cast<std::size_t>(std::min<std::uint64_t>(remaining, kChunk));
      require(!result.count(hash) || result.at(hash) == length);
      result[hash] = length; remaining -= length;
    }
    require(remaining == 0);
  }
  return result;
}
void validate_manifest(const Json& manifest) {
  fields(manifest, {"format", "name", "files", "photos"});
  require(number(manifest.at("format"), 1) == 1 && name_ok(text(manifest.at("name"))));
  const auto& files = manifest.at("files"); const auto& photos = manifest.at("photos");
  require(files.is_array() && !files.empty() && files.size() <= 8000);
  require(photos.is_array() && !photos.empty() && photos.size() <= 2000);
  std::uint64_t total = 0;
  std::map<std::string, std::string> kinds;
  for (const auto& file : files) {
    fields(file, {"key", "kind", "size", "chunks"});
    const auto key = text(file.at("key")); const auto kind = text(file.at("kind"));
    require(key_valid(key) && kinds.emplace(key, kind).second);
    require(kind == "original" || kind == "edit" || kind == "layer" || kind == "pixel");
    const auto size = number(file.at("size"), kind == "edit" || kind == "layer" ? 16 * kChunk : kMaximum);
    require(size > 0 && size <= kMaximum - total); total += size;
    require(file.at("chunks").is_array() && file.at("chunks").size() == (size + kChunk - 1) / kChunk);
  }
  std::set<std::string> used;
  const auto reference = [&](const std::string& key, const std::string& kind) {
    require(kinds.count(key) && kinds.at(key) == kind && used.insert(key).second);
  };
  std::size_t item_count = 0;
  for (const auto& photo : photos) {
    fields(photo, {"source", "name", "rating", "flag", "color", "items"});
    reference(text(photo.at("source")), "original");
    const auto name = text(photo.at("name"));
    require(name_ok(name) && name != "." && name != ".." && name.find_first_of("/\\") == std::string::npos);
    number(photo.at("rating"), 5);
    const auto flag = text(photo.at("flag")); const auto color = text(photo.at("color"));
    require(flag.empty() || flag == kFlagPick || flag == kFlagExclude);
    require(color.empty() || color == kColorRed || color == kColorYellow || color == kColorGreen ||
            color == kColorBlue || color == kColorPurple);
    const auto& items = photo.at("items");
    require(items.is_array() && !items.empty() && items.size() <= 100);
    item_count += items.size(); require(item_count <= 8000);
    for (const auto& item : items) {
      fields(item, {"edit", "layer"});
      for (const auto kind : {"edit", "layer"}) {
        const auto key = text(item.at(kind)); if (!key.empty()) reference(key, kind);
      }
    }
  }
  for (const auto& file : kinds) require(used.count(file.first) || file.second == "pixel");
  chunks(manifest);
}

Json capture(const project_session::ProjectSnapshot& source, const fs::path& job,
             const std::atomic_bool& cancelled, const std::function<void(Progress)>& progress) {
  require(name_ok(source.name) && !source.photos.empty() && source.photos.size() <= 2000, ResultCode::StateConflict);
  std::size_t items = 0;
  for (const auto& photo : source.photos) {
    require(name_ok(photo.name) && fs::path(photo.source_file).is_absolute() &&
            !photo.items.empty() && photo.items.size() <= 100, ResultCode::StateConflict);
    items += photo.items.size(); require(items <= 8000, ResultCode::StateConflict);
  }
  auto portable = source;
  Json files = Json::array();
  std::uint64_t total = 0;
  const auto allocate = [&] {
    require(files.size() < 8000);
    char key[16]; std::snprintf(key, sizeof(key), "f%08u", static_cast<unsigned>(files.size())); return std::string(key);
  };
  const auto cache = [&](const Bytes& bytes) {
    require(!cancelled.load(), ResultCode::Cancelled);
    const auto hash = digest(bytes); require(digest_valid(hash));
    const auto path = job / "chunks" / hash;
    if (!regular(path) || digest(read(path, kChunk)) != hash) save(path, bytes);
    return hash;
  };
  const auto add_bytes = [&](const Bytes& bytes, const std::string& kind) {
    require(!bytes.empty() && bytes.size() <= kMaximum - total); total += bytes.size();
    const auto key = allocate(); Json hashes = Json::array();
    for (std::size_t offset = 0; offset < bytes.size(); offset += kChunk) {
      hashes.push_back(cache(Bytes(bytes.begin() + offset, bytes.begin() + std::min(offset + kChunk, bytes.size()))));
    }
    files.push_back({{"key", key}, {"kind", kind}, {"size", bytes.size()}, {"chunks", std::move(hashes)}});
    return key;
  };
  const auto add_file = [&](const std::string& path, const std::string& kind) {
    Descriptor file{::open(path.c_str(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK)};
    require(file.fd >= 0, errno == ENOENT ? ResultCode::SourceMissing : ResultCode::IoFailed);
    struct stat before{}, after{};
    require(::fstat(file.fd, &before) == 0 && S_ISREG(before.st_mode) && before.st_size > 0, ResultCode::SourceMissing);
    const auto size = static_cast<std::uint64_t>(before.st_size);
    require(size <= kMaximum - total); total += size;
    const auto key = allocate(); Json hashes = Json::array();
    for (std::uint64_t offset = 0; offset < size; offset += kChunk) {
      Bytes bytes(static_cast<std::size_t>(std::min<std::uint64_t>(kChunk, size - offset)));
      std::size_t obtained = 0;
      while (obtained < bytes.size()) {
        const auto count = ::read(file.fd, bytes.data() + obtained, bytes.size() - obtained);
        if (count < 0 && errno == EINTR) continue;
        require(count > 0, ResultCode::IoFailed); obtained += static_cast<std::size_t>(count);
      }
      hashes.push_back(cache(bytes));
      if (progress) progress({Phase::Preparing, total - size + offset + bytes.size(), 0});
    }
    require(::fstat(file.fd, &after) == 0 && before.st_size == after.st_size &&
            before.st_mtimespec.tv_sec == after.st_mtimespec.tv_sec &&
            before.st_mtimespec.tv_nsec == after.st_mtimespec.tv_nsec &&
            before.st_ctimespec.tv_sec == after.st_ctimespec.tv_sec &&
            before.st_ctimespec.tv_nsec == after.st_ctimespec.tv_nsec, ResultCode::StateConflict);
    files.push_back({{"key", key}, {"kind", kind}, {"size", size}, {"chunks", std::move(hashes)}});
    return key;
  };
  std::map<std::string, std::string> pixels;
  for (auto& photo : portable.photos) {
    photo.source_file = add_file(photo.source_file, "original");
    for (auto& item : photo.items) {
      if (!item.edit_file.empty()) {
        auto document = EditDocument::decode(read(item.edit_file, 16 * kChunk));
        require(document.has_value() && !document->recovered_empty());
        document = document->copy_for_image(photo.source_file); require(document.has_value());
        item.edit_file = add_bytes(document->encode(), "edit");
      }
      if (!item.layer_file.empty()) {
        auto layers = LayerDocument::decode(read(item.layer_file, 16 * kChunk));
        require(layers.has_value() && !layers->recovered_empty());
        layers = layers->relocated(photo.source_file, [&](const std::string& path) {
          if (!pixels.count(path)) pixels[path] = add_file(path, "pixel");
          return pixels.at(path);
        });
        require(layers.has_value()); item.layer_file = add_bytes(layers->encode(), "layer");
      }
    }
  }
  auto manifest = snapshot_json(portable); manifest["files"] = std::move(files);
  validate_manifest(manifest); encode(manifest); return manifest;
}

project_session::ProjectSnapshot materialize(const Json& manifest, const fs::path& job,
                                             const std::atomic_bool& cancelled) {
  validate_manifest(manifest);
  std::set<std::string> pixels;
  for (const auto& file : manifest.at("files")) {
    require(!cancelled.load(), ResultCode::Cancelled);
    const auto key = text(file.at("key")); const auto path = job / "files" / key;
    const auto temporary = path.string() + ".part";
    Descriptor output{::open(temporary.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW, 0600)};
    require(output.fd >= 0, ResultCode::IoFailed);
    std::uint64_t remaining = number(file.at("size"), kMaximum);
    for (const auto& entry : file.at("chunks")) {
      require(!cancelled.load(), ResultCode::Cancelled);
      const auto hash = text(entry); auto bytes = read(job / "chunks" / hash, kChunk);
      require(bytes.size() == std::min<std::uint64_t>(remaining, kChunk) && digest(bytes) == hash);
      put(output.fd, bytes); remaining -= bytes.size();
    }
    require(::fsync(output.fd) == 0 && ::rename(temporary.c_str(), path.c_str()) == 0, ResultCode::IoFailed);
    if (text(file.at("kind")) == "pixel") pixels.insert(key);
  }
  sync_directory(job / "files");
  auto snapshot = snapshot_from_json(manifest);
  for (auto& photo : snapshot.photos) {
    const auto original_key = photo.source_file;
    photo.source_file = (job / "files" / original_key).string();
    for (auto& item : photo.items) {
      if (!item.edit_file.empty()) {
        item.edit_file = (job / "files" / item.edit_file).string();
        const auto document = EditDocument::decode(read(item.edit_file, 16 * kChunk));
        require(document && !document->recovered_empty() && document->image_id() == original_key);
      }
      if (!item.layer_file.empty()) {
        item.layer_file = (job / "files" / item.layer_file).string();
        auto layers = LayerDocument::decode(read(item.layer_file, 16 * kChunk));
        require(layers && !layers->recovered_empty() && layers->source_image_id() == original_key);
        layers = layers->relocated(original_key, [&](const std::string& key) {
          require(key_valid(key) && pixels.count(key)); return (job / "files" / key).string();
        });
        require(layers.has_value()); save(item.layer_file, layers->encode());
      }
    }
  }
  return snapshot;
}
}  // namespace pixcake::cloud_sync
