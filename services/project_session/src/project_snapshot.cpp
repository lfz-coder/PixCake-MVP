#include "pixcake/project_session/project_session.hpp"
#include "pixcake/editdoc/edit_document.hpp"
#include "pixcake/layerdoc/layer_document.hpp"
#include "pixcake/persist/atomic_file.hpp"

#include <filesystem>
#include <map>
#include <set>

namespace pixcake::project_session {
namespace fs = std::filesystem;
std::string ProjectSession::data_directory() const { return fs::path(document_directory_).parent_path().string(); }
ResultCode ProjectSession::mark_uploaded(std::int64_t id) {
  return commands_.mark_project_origin(*library_, id, kOriginCloudUploaded);
}
ResultCode ProjectSession::describe_snapshot(std::int64_t id, ProjectSnapshot& out) {
  out = {};
  if (!commands_.signed_in()) return ResultCode::StateConflict;
  std::optional<ProjectRow> project;
  auto code = library_->load_project(id, project);
  if (code != ResultCode::Ok) return code;
  if (!project || project->deleted_at) return ResultCode::StateConflict;
  std::vector<PhotoRow> rows;
  code = list_photos(id, rows);
  if (code != ResultCode::Ok) return code;
  ProjectSnapshot snapshot; snapshot.name = project->name;
  std::map<std::int64_t, std::size_t> positions;
  for (const auto& row : rows) {
    const auto source = source_photo(row.id);
    if (!positions.count(source)) {
      if (snapshot.photos.size() >= 2000) return ResultCode::StateConflict;
      SnapshotPhoto photo;
      std::string diagnostic;
      code = resolve_photo(source, photo.source_file, diagnostic);
      if (code != ResultCode::Ok) return code;
      photo.name = row.file_name; photo.rating = row.rating; photo.flag = row.flag; photo.color = row.color_label;
      photo.source_access = row.access_credential;
      positions[source] = snapshot.photos.size(); snapshot.photos.push_back(std::move(photo));
    }
    auto& photo = snapshot.photos[positions[source]];
    if (photo.items.size() >= 100) return ResultCode::StateConflict;
    SnapshotItem item;
    const auto edit = document_directory_ + "/" + document_name(row.id) + ".pxedit";
    const auto layer = layer_document_path(row.id);
    if (files_.exists(edit)) item.edit_file = edit;
    if (files_.exists(layer)) item.layer_file = layer;
    photo.items.push_back(std::move(item));
  }
  if (snapshot.photos.empty()) return ResultCode::StateConflict;
  out = std::move(snapshot);
  return ResultCode::Ok;
}

ResultCode ProjectSession::restore_snapshot(const ProjectSnapshot& snapshot, const std::string& owned_root,
                                           const std::string& receipt, std::int64_t& project_id) {
  project_id = 0;
  if (!commands_.signed_in() || snapshot.name.empty() || snapshot.photos.empty() || snapshot.photos.size() > 2000 ||
      receipt.empty() || receipt.size() > 1024) return ResultCode::StateConflict;
  std::vector<std::string> created;
  ResultCode code = ResultCode::IoFailed;
  try {
    const fs::path root = fs::canonical(owned_root);
    const auto owned = [&](const std::string& path) {
      return !path.empty() && !fs::is_symlink(fs::symlink_status(path)) && fs::is_regular_file(path) &&
             fs::canonical(path).parent_path() == root;
    };
    std::set<std::string> source_files;
    std::size_t item_count = 0;
    for (const auto& photo : snapshot.photos) {
      if (!owned(photo.source_file) || !source_files.insert(photo.source_file).second || photo.name.empty() ||
          photo.name.find_first_of("/\\") != std::string::npos || photo.items.empty() || photo.items.size() > 100)
        return ResultCode::DocumentCorrupt;
      item_count += photo.items.size();
      if (item_count > 8000) return ResultCode::DocumentCorrupt;
      for (const auto& item : photo.items) {
        for (const auto& path : {item.edit_file, item.layer_file}) {
          if (!path.empty() && (!owned(path) || fs::file_size(path) > 16 * 1024 * 1024)) return ResultCode::DocumentCorrupt;
        }
      }
    }
    code = store_->in_transaction([&] {
      auto result = store_->load_import_receipt(receipt, project_id);
      if (result != ResultCode::Ok || project_id) return result;
      const auto project = commands_.create_project(*library_, snapshot.name, kOriginCloudDownloaded, true);
      if (project.code != ResultCode::Ok) return project.code;
      project_id = project.id;
      for (const auto& photo : snapshot.photos) {
        image_import::ImportCandidate candidate;
        candidate.absolute_path = photo.source_file; candidate.file_name = photo.name;
        result = make_bookmark_(photo.source_file, candidate.access_credential);
        if (result != ResultCode::Ok) return result;
        const auto imported = commands_.import_into(*importer_, project_id, {candidate});
        if (imported.overall_code != ResultCode::Ok) return imported.overall_code;
        if (imported.items.size() != 1 || imported.accepted_count != 1) return ResultCode::ImageCorrupt;
        const auto original = imported.items.front().photo_id;
        result = commands_.set_photo_ratings(*library_, {original}, photo.rating).code;
        if (result == ResultCode::Ok) result = commands_.set_photo_flags(*library_, {original}, photo.flag).code;
        if (result == ResultCode::Ok) result = commands_.set_photo_colors(*library_, {original}, photo.color).code;
        if (result != ResultCode::Ok) return result;
        for (std::size_t index = 0; index < photo.items.size(); ++index) {
          const auto name = document_id(original) + (index ? "-copy-" + std::to_string(index) : "");
          if (index) {
            const auto copy = commands_.create_virtual_copy(*library_, original, name);
            if (copy.code != ResultCode::Ok) return copy.code;
          }
          const auto& item = photo.items[index];
          if (!item.edit_file.empty()) {
            std::vector<std::uint8_t> bytes;
            result = files_.read_file(item.edit_file, bytes).code;
            if (result != ResultCode::Ok) return result;
            auto document = EditDocument::decode(bytes);
            if (!document || document->recovered_empty()) return ResultCode::DocumentCorrupt;
            document = document->copy_for_image(document_id(original));
            if (!document) return ResultCode::DocumentCorrupt;
            const auto destination = document_directory_ + "/" + name + ".pxedit";
            if (files_.exists(destination)) {
              // A crash can roll back SQLite after a new document was already
              // durable. Adopt only identical bytes; never overwrite an orphan
              // that might belong to another project or an older local edit.
              if (fs::is_symlink(destination) || fs::file_size(destination) > 16 * 1024 * 1024) return ResultCode::StateConflict;
              std::vector<std::uint8_t> existing;
              result = files_.read_file(destination, existing).code;
              if (result != ResultCode::Ok || existing != document->encode()) return ResultCode::StateConflict;
            } else {
              created.push_back(destination);
              result = document->write_snapshot(files_, destination);
            }
            if (result != ResultCode::Ok) return result;
            result = mark_edited(original);
            if (result != ResultCode::Ok) return result;
          }
          if (!item.layer_file.empty()) {
            std::vector<std::uint8_t> bytes;
            result = files_.read_file(item.layer_file, bytes).code;
            if (result != ResultCode::Ok) return result;
            auto layers = LayerDocument::decode(bytes);
            if (!layers || layers->recovered_empty()) return ResultCode::DocumentCorrupt;
            layers = layers->relocated(document_id(original), [&](const std::string& pixel) {
              return owned(pixel) ? pixel : std::string{};
            });
            if (!layers) return ResultCode::DocumentCorrupt;
            const auto destination = document_directory_ + "/" + name + ".pxlayer";
            if (files_.exists(destination)) {
              if (fs::is_symlink(destination) || fs::file_size(destination) > 16 * 1024 * 1024) return ResultCode::StateConflict;
              std::vector<std::uint8_t> existing;
              result = files_.read_file(destination, existing).code;
              if (result != ResultCode::Ok || existing != layers->encode()) return ResultCode::StateConflict;
            } else {
              created.push_back(destination);
              result = atomic_replace(files_, destination, layers->encode());
            }
            if (result != ResultCode::Ok) return result;
          }
        }
      }
      if (!commands_.signed_in()) return ResultCode::Cancelled;
      return store_->insert_import_receipt(receipt, project_id);
    });
  } catch (const fs::filesystem_error&) { code = ResultCode::IoFailed; }
  if (code != ResultCode::Ok) {
    project_id = 0;
    for (const auto& path : created) { files_.remove_file(path); files_.remove_file(path + ".bak"); }
  }
  return code;
}
}  // namespace pixcake::project_session
