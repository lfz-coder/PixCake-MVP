#include "pixcake/project_session/project_session.hpp"

#include "pixcake/capabilities/module_set.hpp"
#include "pixcake/capability/capability_registry.hpp"
#include "pixcake/persist/atomic_file.hpp"
#include "pixcake/preset/preset_codec.hpp"
#include "pixcake/editdoc/edit_document.hpp"
#include "pixcake/storage/sqlite_project_store.hpp"

#include <dirent.h>
#include <sys/stat.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace pixcake::project_session {
namespace {

bool is_directory(const std::string& path) {
  struct stat info {};
  return ::stat(path.c_str(), &info) == 0 && S_ISDIR(info.st_mode);
}

std::string base_name(std::string path) {
  while (path.size() > 1 && path.back() == '/') path.pop_back();
  const auto slash = path.find_last_of('/');
  return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::string lower_extension(const std::string& name) {
  const auto dot = name.find_last_of('.');
  if (dot == std::string::npos || dot == 0) return {};
  std::string ext = name.substr(dot + 1);
  std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return ext;
}

bool image_extension(const std::string& name) {
  static const char* const kExtensions[] = {"jpg", "jpeg", "png", "tif", "tiff", "bmp"};
  const std::string ext = lower_extension(name);
  return std::any_of(std::begin(kExtensions), std::end(kExtensions), [&](const char* e) { return ext == e; });
}

}  // namespace

std::vector<std::string> expand_import_paths(const std::vector<std::string>& paths) {
  std::vector<std::string> files;
  for (const auto& path : paths) {
    if (!is_directory(path)) {
      files.push_back(path);
      continue;
    }
    std::vector<std::string> inside;
    if (DIR* dir = ::opendir(path.c_str())) {
      while (const dirent* entry = ::readdir(dir)) {
        const std::string name = entry->d_name;
        if (name.empty() || name[0] == '.' || !image_extension(name)) continue;
        const std::string full = (path.back() == '/' ? path : path + "/") + name;
        if (!is_directory(full)) inside.push_back(full);
      }
      ::closedir(dir);
    }
    std::sort(inside.begin(), inside.end());
    files.insert(files.end(), inside.begin(), inside.end());
  }
  return files;
}

std::string project_name_for(const std::vector<std::string>& paths) {
  for (const auto& path : paths) {
    if (is_directory(path)) return base_name(path);
  }
  if (paths.empty()) return {};
  const std::string name = base_name(paths.front());
  const auto dot = name.find_last_of('.');
  return dot == std::string::npos || dot == 0 ? name : name.substr(0, dot);
}

ProjectSession::ProjectSession(const SessionState& session, SourceAccess& access, BookmarkMaker make_bookmark)
    : access_(access), make_bookmark_(make_bookmark), commands_(session) {}

ProjectSession::~ProjectSession() = default;

ResultCode ProjectSession::open(const std::string& data_directory, const SessionState& session, SourceAccess& access,
                                BookmarkMaker make_bookmark, std::unique_ptr<ProjectSession>& out,
                                std::string& diagnostic) {
  out.reset();
  if (data_directory.empty() || make_bookmark == nullptr) {
    diagnostic = "project data directory or bookmark maker missing";
    return ResultCode::StateConflict;
  }
  ::mkdir(data_directory.c_str(), 0700);
  if (!is_directory(data_directory)) {
    diagnostic = "project data directory cannot be created: " + data_directory;
    return ResultCode::IoFailed;
  }
  std::unique_ptr<ProjectSession> made(new ProjectSession(session, access, make_bookmark));
  std::unique_ptr<SqliteProjectStore> store;
  const ResultCode opened = SqliteProjectStore::open(data_directory + "/projects.sqlite", store);
  if (opened != ResultCode::Ok) {
    diagnostic = "project library cannot be opened: " + data_directory + "/projects.sqlite";
    return opened;
  }
  made->store_ = std::move(store);
  made->deleter_ = std::make_unique<FileSourceDeleter>(made->files_);
  made->library_ = std::make_unique<ProjectLibrary>(*made->store_, made->files_, made->clock_, *made->deleter_, nullptr);
  if (const ResultCode started = made->library_->startup(); started != ResultCode::Ok) {
    diagnostic = "project library startup failed";
    return started;
  }
  made->importer_ = std::make_unique<image_import::ImportCoordinator>(*made->library_, made->files_);
  made->document_directory_ = data_directory + "/documents";
  ::mkdir(made->document_directory_.c_str(), 0700);
  made->preset_path_ = data_directory + "/presets.pxpresets";
  if (made->files_.exists(made->preset_path_)) {
    std::vector<std::uint8_t> bytes;
    if (made->files_.read_file(made->preset_path_, bytes).code == ResultCode::Ok) {
      if (auto stored = decode_presets(bytes)) made->presets_.load(std::move(*stored));  // 坏文件当作没有
    }
  }
  made->workspace_directory_ = data_directory + "/workspace";
  ::mkdir(made->workspace_directory_.c_str(), 0700);
  out = std::move(made);
  diagnostic.clear();
  return ResultCode::Ok;
}

std::string ProjectSession::document_id(std::int64_t photo_id) { return "photo-" + std::to_string(photo_id); }

ProjectSession::ImportOutcome ProjectSession::import_new_project(const std::vector<std::string>& paths) {
  ImportOutcome outcome;
  const auto files = expand_import_paths(paths);
  if (files.empty()) {
    outcome.code = ResultCode::UnsupportedFormat;
    outcome.notices.push_back("no image files were chosen");
    return outcome;
  }
  outcome.project_name = project_name_for(paths);
  const ProjectResult created = commands_.create_project(*library_, outcome.project_name, kOriginLocal, false);
  if (created.code != ResultCode::Ok) {
    outcome.code = created.code;
    if (!created.notice.empty()) outcome.notices.push_back(created.notice);
    return outcome;
  }
  return import_paths(created.id, files, std::move(outcome));
}

ProjectSession::ImportOutcome ProjectSession::import_into(std::int64_t project_id,
                                                          const std::vector<std::string>& paths) {
  return import_paths(project_id, expand_import_paths(paths), ImportOutcome{});
}

ProjectSession::ImportOutcome ProjectSession::import_paths(std::int64_t project_id,
                                                           const std::vector<std::string>& files,
                                                           ImportOutcome outcome) {
  outcome.project_id = project_id;
  std::vector<image_import::ImportCandidate> candidates;
  for (const auto& path : files) {
    image_import::ImportCandidate candidate;
    candidate.absolute_path = path;
    candidate.file_name = base_name(path);
    // No bookmark: the library refuses it with "permission denied" for this file.
    make_bookmark_(path, candidate.access_credential);
    candidates.push_back(std::move(candidate));
  }
  const auto batch = commands_.import_into(*importer_, project_id, candidates);
  outcome.code = batch.overall_code;
  outcome.accepted = batch.accepted_count;
  for (const auto& item : batch.items) {
    if (item.code == ResultCode::Ok && item.photo_id != 0) outcome.photo_ids.push_back(item.photo_id);
  }
  const auto notices = image_import::import_notices(batch);
  outcome.notices.insert(outcome.notices.end(), notices.begin(), notices.end());
  return outcome;
}

ResultCode ProjectSession::list_projects(std::vector<ProjectRow>& out) const {
  std::vector<ProjectRow> all;
  const ResultCode code = library_->list_projects(all);
  out.clear();
  for (auto& row : all) {
    if (!row.deleted_at) out.push_back(std::move(row));
  }
  return code;
}

namespace {

// 副本在条目列表里用原图的行，只换编号。
PhotoRow copy_row(const PhotoRow& source, std::int64_t copy_id) {
  PhotoRow row = source;
  row.id = -copy_id;
  return row;
}

}  // namespace

std::int64_t ProjectSession::source_photo(std::int64_t item) const {
  if (item >= 0) return item;
  std::optional<VirtualCopyRow> copy;
  if (library_->load_virtual_copy(-item, copy) != ResultCode::Ok || !copy) return 0;
  return copy->source_photo_id;
}

std::int64_t ProjectSession::copy_sequence(std::int64_t item) const {
  if (item >= 0) return 0;
  std::optional<VirtualCopyRow> copy;
  if (library_->load_virtual_copy(-item, copy) != ResultCode::Ok || !copy) return 0;
  return copy->sequence;
}

std::string ProjectSession::document_name(std::int64_t item) const {
  if (item >= 0) return document_id(item);
  std::optional<VirtualCopyRow> copy;
  if (library_->load_virtual_copy(-item, copy) != ResultCode::Ok || !copy) return "copy-" + std::to_string(-item);
  return copy->document_id;
}

std::string ProjectSession::image_id(std::int64_t item) const { return document_id(source_photo(item)); }

ResultCode ProjectSession::create_virtual_copy(std::int64_t item, std::int64_t& new_item, std::string& diagnostic) {
  new_item = 0;
  const std::int64_t source = source_photo(item);
  std::vector<VirtualCopyRow> copies;
  if (source == 0 || library_->list_virtual_copies(source, copies) != ResultCode::Ok) {
    diagnostic = "photo is not in the project library";
    return ResultCode::StateConflict;
  }
  const std::int64_t sequence = copies.empty() ? 1 : copies.back().sequence + 1;
  const std::string name = document_id(source) + "-copy-" + std::to_string(sequence);
  const std::string from = document_directory_ + "/" + document_name(item) + ".pxedit";
  if (files_.exists(from)) {
    auto copy = EditDocument::create(image_id(item));
    const LoadResult copied = EditDocument::copy_committed(files_, from, document_directory_ + "/" + name + ".pxedit",
                                                           phase_one_version_limits(), *copy);
    if (!copied.code || *copied.code != ResultCode::Ok) {
      diagnostic = "the edit document could not be copied";
      return copied.code ? *copied.code : ResultCode::IoFailed;
    }
  }
  const ProjectResult created = commands_.create_virtual_copy(*library_, source, name);
  if (created.code != ResultCode::Ok) {
    diagnostic = created.notice;
    return created.code;
  }
  new_item = -created.id;
  diagnostic.clear();
  return ResultCode::Ok;
}

ResultCode ProjectSession::list_photos(std::int64_t project_id, std::vector<PhotoRow>& out,
                                       const PhotoQuery* query) const {
  std::vector<PhotoRow> photos;
  ResultCode code;
  if (query != nullptr) {
    PhotoQuery scoped = *query;
    scoped.project_id = project_id;
    code = library_->query_photos(scoped, photos);
  } else {
    code = library_->list_photos(project_id, photos);
  }
  out.clear();
  for (auto& photo : photos) {
    std::optional<TrashRow> gone;
    if (library_->load_trash(kTrashPhoto, photo.id, gone) == ResultCode::Ok && gone) continue;
    const PhotoRow original = photo;
    out.push_back(std::move(photo));
    // 副本跟在原图右边，按序号；进了回收站的不显示。
    std::vector<VirtualCopyRow> copies;
    if (library_->list_virtual_copies(original.id, copies) != ResultCode::Ok) continue;
    for (const auto& copy : copies) {
      std::optional<TrashRow> trashed;
      if (library_->load_trash(kTrashVirtualCopy, copy.id, trashed) == ResultCode::Ok && trashed) continue;
      out.push_back(copy_row(original, copy.id));
    }
  }
  return code;
}

namespace {
std::vector<std::int64_t> sources_of(const ProjectSession& session, const std::vector<std::int64_t>& items) {
  std::vector<std::int64_t> sources;
  for (const std::int64_t item : items) {
    const std::int64_t source = session.source_photo(item);
    if (source != 0 && std::find(sources.begin(), sources.end(), source) == sources.end()) sources.push_back(source);
  }
  return sources;
}
}  // namespace

namespace {

// 目标的已提交文档（没编辑过就是空文档）和它的路径。
bool load_committed(ProjectSession& session, FileSystem& files, const std::string& directory, std::int64_t item,
                    std::optional<EditDocument>& document, std::string& path) {
  path = directory + "/" + session.document_name(item) + ".pxedit";
  document = EditDocument::create(session.image_id(item));
  if (!document) return false;
  if (!files.exists(path)) return true;
  const LoadResult loaded = EditDocument::load(*document, files, path, phase_one_version_limits());
  return loaded.code && *loaded.code == ResultCode::Ok && !loaded.recovered_empty;
}

}  // namespace

ResultCode ProjectSession::copy_effect(std::int64_t item, EffectClipboard& out) {
  out = {};
  if (!commands_.signed_in()) return ResultCode::StateConflict;
  std::optional<EditDocument> document;
  std::string path;
  if (!load_committed(*this, files_, document_directory_, item, document, path)) return ResultCode::DocumentCorrupt;
  out = copy_committed(*document);
  return ResultCode::Ok;
}

ResultCode ProjectSession::paste_effect(const EffectClipboard& clipboard, const std::vector<std::int64_t>& targets,
                                        std::vector<EffectOutcome>& outcomes) {
  outcomes.clear();
  if (!commands_.signed_in()) return ResultCode::StateConflict;
  ResultCode first = ResultCode::Ok;
  for (const std::int64_t item : targets) {
    EffectOutcome outcome;
    outcome.item = item;
    std::optional<EditDocument> document;
    std::string path;
    if (!load_committed(*this, files_, document_directory_, item, document, path)) {
      outcome.code = ResultCode::DocumentCorrupt;
    } else {
      outcome.code = paste_committed(*document, clipboard, document->revision(), files_, path);
      if (outcome.code == ResultCode::Ok) mark_edited(item);
    }
    if (outcome.code != ResultCode::Ok && first == ResultCode::Ok) first = outcome.code;
    outcomes.push_back(outcome);
  }
  return first;
}

ResultCode ProjectSession::sync_effect(std::int64_t source, const std::vector<std::string>& checked_ids,
                                       const std::vector<std::int64_t>& targets,
                                       std::vector<EffectOutcome>& outcomes) {
  outcomes.clear();
  if (!commands_.signed_in()) return ResultCode::StateConflict;
  std::optional<EditDocument> from;
  std::string from_path;
  if (!load_committed(*this, files_, document_directory_, source, from, from_path)) return ResultCode::DocumentCorrupt;
  std::vector<std::optional<EditDocument>> documents(targets.size());
  std::vector<SyncTarget> sync_targets;
  std::vector<std::int64_t> items;
  for (std::size_t i = 0; i < targets.size(); ++i) {
    if (targets[i] == source) continue;
    std::string path;
    EffectOutcome failed;
    failed.item = targets[i];
    if (!load_committed(*this, files_, document_directory_, targets[i], documents[i], path)) {
      failed.code = ResultCode::DocumentCorrupt;
      outcomes.push_back(failed);
      continue;
    }
    SyncTarget target;
    target.id = std::to_string(targets[i]);
    target.document = &*documents[i];
    target.expected_revision = documents[i]->revision();
    target.files = &files_;
    target.path = path;
    sync_targets.push_back(target);
    items.push_back(targets[i]);
  }
  const auto results = apply_checked(*from, from_path, checked_ids, sync_targets);
  ResultCode first = ResultCode::Ok;
  for (std::size_t i = 0; i < results.size() && i < items.size(); ++i) {
    outcomes.push_back({items[i], results[i].code});
    if (results[i].code == ResultCode::Ok) mark_edited(items[i]);
  }
  for (const auto& outcome : outcomes) {
    if (outcome.code != ResultCode::Ok) {
      first = outcome.code;
      break;
    }
  }
  return first;
}

std::vector<ProjectSession::PresetRow> ProjectSession::list_presets() const {
  std::vector<PresetRow> out;
  for (const auto& preset : presets_.presets()) out.push_back({preset.id, preset.name, preset.category, preset.type});
  return out;
}

std::vector<std::string> ProjectSession::adjusted_capabilities(std::int64_t item) {
  std::vector<std::string> out;
  std::optional<EditDocument> document;
  std::string path;
  if (!load_committed(*this, files_, document_directory_, item, document, path)) return out;
  static const auto registry = CapabilityRegistry::phase_one();
  for (const auto& descriptor : registry.descriptors()) {
    if (descriptor.can_use_in_preset && document->fragment(descriptor.id).has_value()) out.push_back(descriptor.id);
  }
  return out;
}

ResultCode ProjectSession::save_preset(std::int64_t item, const PresetDraft& draft, std::string& diagnostic) {
  diagnostic.clear();
  if (!commands_.signed_in()) return ResultCode::StateConflict;
  std::optional<EditDocument> document;
  std::string path;
  if (!load_committed(*this, files_, document_directory_, item, document, path)) return ResultCode::DocumentCorrupt;
  Preset preset;
  preset.name = draft.name;
  preset.category = draft.category;
  for (const auto& id : draft.capability_ids) {
    const auto fragment = document->fragment(id);
    if (!fragment) continue;  // 勾了但这张图没调：不存
    // 经能力模块过滤：自定义画笔蒙版不能存进预设（需求 §10.8），其余原样。
    const FragmentTransfer moved = transfer_fragment(id, FragmentBytes{fragment->version, fragment->inner}, std::nullopt);
    if (moved.code != ResultCode::Ok || !moved.fragment) {
      diagnostic = "a checked function cannot be saved into a preset";
      return moved.code != ResultCode::Ok ? moved.code : ResultCode::StateConflict;
    }
    preset.fragments.push_back({id, moved.fragment->version, moved.fragment->inner});
  }
  std::vector<std::string> ids;
  for (const auto& fragment : preset.fragments) ids.push_back(fragment.capability_id);
  std::vector<std::string> modules;
  const std::string inferred = PresetLibrary::infer_type(ids, modules);
  preset.type = draft.type.empty() ? inferred : draft.type;
  if (preset.type == kPresetCombo) {
    preset.modules = modules;
  }
  if (preset.type.empty() || preset.fragments.empty()) {
    diagnostic = "nothing adjustable is checked";
    return ResultCode::StateConflict;
  }
  const bool overwrite = !draft.overwrite_id.empty();
  if (overwrite) {
    preset.id = draft.overwrite_id;
  } else {
    long next = 1;
    for (const auto& existing : presets_.presets()) {
      if (existing.id.rfind("preset-", 0) == 0) next = std::max(next, std::atol(existing.id.c_str() + 7) + 1);
    }
    preset.id = "preset-" + std::to_string(next);
  }
  const ResultCode saved = presets_.save(std::move(preset), overwrite);
  if (saved != ResultCode::Ok) {
    diagnostic = "the preset is not valid (name, type or the checked functions)";
    return saved;
  }
  return atomic_replace(files_, preset_path_, encode_presets(presets_.presets()));
}

ResultCode ProjectSession::rename_preset(const std::string& id, const std::string& name) {
  if (!commands_.signed_in()) return ResultCode::StateConflict;
  const ResultCode code = presets_.rename(id, name);
  return code != ResultCode::Ok ? code : atomic_replace(files_, preset_path_, encode_presets(presets_.presets()));
}

ResultCode ProjectSession::delete_preset(const std::string& id) {
  if (!commands_.signed_in()) return ResultCode::StateConflict;
  const ResultCode code = presets_.remove(id);
  return code != ResultCode::Ok ? code : atomic_replace(files_, preset_path_, encode_presets(presets_.presets()));
}

bool ProjectSession::preset_overwrites_color(std::int64_t item, const std::string& id) {
  const Preset* preset = presets_.find(id);
  std::optional<EditDocument> document;
  std::string path;
  if (preset == nullptr || !load_committed(*this, files_, document_directory_, item, document, path)) return false;
  return presets_.check_apply(*document, *preset).overwrite_color;
}

ResultCode ProjectSession::apply_preset(const std::string& id, const std::vector<std::int64_t>& targets,
                                        std::vector<EffectOutcome>& outcomes) {
  outcomes.clear();
  if (!commands_.signed_in()) return ResultCode::StateConflict;
  const Preset* preset = presets_.find(id);
  if (preset == nullptr) return ResultCode::StateConflict;
  struct NoLatch final : FastExportLatch {
    bool enabled() const override { return false; }
  } latch;
  ResultCode first = ResultCode::Ok;
  for (const std::int64_t item : targets) {
    EffectOutcome outcome;
    outcome.item = item;
    std::optional<EditDocument> document;
    std::string path;
    if (!load_committed(*this, files_, document_directory_, item, document, path)) {
      outcome.code = ResultCode::DocumentCorrupt;
    } else {
      outcome.code = presets_.apply(*document, *preset, document->revision(), files_, path, latch);
      if (outcome.code == ResultCode::Ok) mark_edited(item);
    }
    if (outcome.code != ResultCode::Ok && first == ResultCode::Ok) first = outcome.code;
    outcomes.push_back(outcome);
  }
  return first;
}

ResultCode ProjectSession::set_rating(const std::vector<std::int64_t>& items, int rating) {
  const auto sources = sources_of(*this, items);
  return sources.empty() ? ResultCode::StateConflict : commands_.set_photo_ratings(*library_, sources, rating).code;
}

ResultCode ProjectSession::set_flag(const std::vector<std::int64_t>& items, const std::string& flag) {
  const auto sources = sources_of(*this, items);
  return sources.empty() ? ResultCode::StateConflict : commands_.set_photo_flags(*library_, sources, flag).code;
}

ResultCode ProjectSession::set_color(const std::vector<std::int64_t>& items, const std::string& color) {
  const auto sources = sources_of(*this, items);
  return sources.empty() ? ResultCode::StateConflict : commands_.set_photo_colors(*library_, sources, color).code;
}

ResultCode ProjectSession::trash_items(const std::vector<std::int64_t>& items, std::string& diagnostic) {
  diagnostic.clear();
  ResultCode first = ResultCode::Ok;
  for (const std::int64_t item : items) {
    std::optional<TrashRow> already;
    const bool copy = is_copy(item);
    const std::int64_t id = copy ? -item : item;
    if (library_->load_trash(copy ? kTrashVirtualCopy : kTrashPhoto, id, already) == ResultCode::Ok && already) continue;
    const ProjectResult result = copy ? commands_.trash_virtual_copy(*library_, id) : commands_.trash_photo(*library_, id);
    if (result.code != ResultCode::Ok && first == ResultCode::Ok) {
      first = result.code;
      diagnostic = result.notice;
    }
  }
  return first;
}

ResultCode ProjectSession::trash_project(std::int64_t project_id, std::string& diagnostic) {
  const ProjectResult result = commands_.trash_project(*library_, project_id);
  diagnostic = result.notice;
  return result.code;
}

ResultCode ProjectSession::list_trash(std::vector<TrashEntry>& out) const {
  out.clear();
  std::vector<TrashRow> rows;
  const ResultCode code = library_->list_trash(rows);
  if (code != ResultCode::Ok) return code;
  const std::int64_t now = clock_.unix_seconds();
  for (const auto& row : rows) {
    TrashEntry entry;
    entry.subject = row.subject;
    entry.id = row.subject_id;
    const std::int64_t left = row.deleted_at + kTrashRetentionSeconds - now;
    entry.days_left = left <= 0 ? 0 : (left + 86399) / 86400;
    std::optional<PhotoRow> photo;
    std::optional<ProjectRow> project;
    if (row.subject == kTrashProject) {
      if (library_->load_project(row.subject_id, project) != ResultCode::Ok || !project) continue;
      entry.project_id = project->id;
      entry.name = project->name;
      entry.project_name = project->name;
    } else {
      std::int64_t photo_id = row.subject_id;
      if (row.subject == kTrashVirtualCopy) {
        std::optional<VirtualCopyRow> copy;
        if (library_->load_virtual_copy(row.subject_id, copy) != ResultCode::Ok || !copy) continue;
        photo_id = copy->source_photo_id;
        entry.copy = copy->sequence;
      }
      if (library_->load_photo(photo_id, photo) != ResultCode::Ok || !photo) continue;
      if (library_->load_project(photo->project_id, project) != ResultCode::Ok || !project) continue;
      if (project->deleted_at) continue;  // 项目在回收站里，图片跟着项目走
      entry.project_id = photo->project_id;
      entry.name = photo->file_name;
      entry.project_name = project->name;
    }
    out.push_back(std::move(entry));
  }
  return ResultCode::Ok;
}

ResultCode ProjectSession::restore(const TrashEntry& entry, std::string& diagnostic) {
  ProjectResult result;
  if (entry.subject == kTrashProject) {
    result = library_->restore_project(entry.id);
  } else if (entry.subject == kTrashVirtualCopy) {
    result = library_->restore_virtual_copies({entry.id});
  } else {
    result = library_->restore_photos({entry.id});
  }
  diagnostic = result.notice;
  return result.code;
}

ResultCode ProjectSession::erase_trashed_project(std::int64_t project_id, std::string& diagnostic) {
  const ProjectResult result = library_->erase_trashed_project(project_id);
  diagnostic = result.notice;
  return result.code;
}

ResultCode ProjectSession::load_photo(std::int64_t photo_id, std::optional<PhotoRow>& out) const {
  if (photo_id >= 0) return library_->load_photo(photo_id, out);
  const std::int64_t source = source_photo(photo_id);
  const ResultCode code = library_->load_photo(source, out);
  if (code == ResultCode::Ok && out) out = copy_row(*out, -photo_id);
  return code;
}

ResultCode ProjectSession::resolve_photo(std::int64_t item, std::string& absolute_path, std::string& diagnostic) {
  absolute_path.clear();
  const std::int64_t photo_id = source_photo(item);
  std::optional<PhotoRow> photo;
  if (library_->load_photo(photo_id, photo) != ResultCode::Ok || !photo) {
    diagnostic = "photo is not in the project library";
    return ResultCode::StateConflict;
  }
  const ResolvedSource resolved = access_.resolve(photo->access_credential);
  if (resolved.code == ResultCode::Ok && resolved.absolute_path == photo->absolute_path) {
    absolute_path = resolved.absolute_path;
    diagnostic.clear();
    return ResultCode::Ok;
  }
  // Moved, renamed, deleted or no longer granted: record it the library's way
  // (presence "missing", path and edits kept) and refuse to open.
  library_->refresh_source_access(access_);
  diagnostic = (resolved.code == ResultCode::PermissionDenied ? "no permission to read the original: "
                                                               : "the original is missing: ") +
               photo->absolute_path;
  return resolved.code == ResultCode::PermissionDenied ? ResultCode::PermissionDenied : ResultCode::SourceMissing;
}

ResultCode ProjectSession::mark_edited(std::int64_t photo_id) {
  return library_->mark_photo_edited(source_photo(photo_id), clock_.unix_seconds()).code;
}

ResultCode ProjectSession::document_bytes(std::int64_t photo_id, std::vector<std::uint8_t>& out) {
  out.clear();
  const std::string path = document_directory_ + "/" + document_name(photo_id) + ".pxedit";
  if (!files_.exists(path)) return ResultCode::Ok;
  auto document = EditDocument::create(image_id(photo_id));
  // load() falls back to the backup and reports a document recovered empty.
  const LoadResult loaded = EditDocument::load(*document, files_, path, phase_one_version_limits());
  if (loaded.code && *loaded.code != ResultCode::Ok) return *loaded.code;
  out = document->encode();
  return ResultCode::Ok;
}

// One line: "library <photo id>" or "refine <photo id>"; the id is 0 for none.
ResultCode ProjectSession::remember_place(std::int64_t project_id, const ProjectPlace& place) {
  const std::string path = workspace_directory_ + "/project-" + std::to_string(project_id);
  const std::string line = std::string(place.workspace == Workspace::Library ? "library" : "refine") + " " +
                           std::to_string(place.photo_id.value_or(0)) + "\n";
  const std::string temporary = path + ".tmp";
  files_.remove_file(temporary);
  const IoResult written = files_.write_new(temporary, std::vector<std::uint8_t>(line.begin(), line.end()));
  if (!written.ok) return written.code;
  const IoResult renamed = files_.rename_file(temporary, path);
  return renamed.ok ? ResultCode::Ok : renamed.code;
}

ProjectPlace ProjectSession::last_place(std::int64_t project_id) {
  ProjectPlace place;
  std::vector<std::uint8_t> bytes;
  if (!files_.read_file(workspace_directory_ + "/project-" + std::to_string(project_id), bytes).ok) return place;
  const std::string line(bytes.begin(), bytes.end());
  const auto space = line.find(' ');
  const std::string workspace = line.substr(0, space);
  if (workspace == "library") {
    place.workspace = Workspace::Library;
  } else if (workspace != "refine") {
    return place;
  }
  std::int64_t photo = 0;
  if (space != std::string::npos) photo = std::strtoll(line.c_str() + space + 1, nullptr, 10);
  std::vector<PhotoRow> rows;
  if (photo > 0 && list_photos(project_id, rows) == ResultCode::Ok) {
    for (const auto& row : rows) {
      if (row.id == photo) place.photo_id = photo;
    }
  }
  return place;
}

}  // namespace pixcake::project_session
