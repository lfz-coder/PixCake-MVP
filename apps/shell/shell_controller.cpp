#include "shell_controller.hpp"

#include <QDebug>

#include <algorithm>

namespace pixcake::shell {

ShellController::~ShellController() {
  notifier_.reset();
  session_.reset();  // closes the connection; renderd exits
}

void ShellController::request_page(Page target) {
  if (display_.navigate(target) != ResultCode::Ok) return;
  if (target == Page::Home) close_project();
  // The library opens on the grid (as 像素蛋糕 does); refine shows the selected photo.
  if (target == Page::Library) display_.set_library_view("grid");
  if (target == Page::Refine) show_selected_photo();
  remember_place();
  if (changed_) changed_();
}

void ShellController::remember_place() {
  if (!projects_ || !current_project_) return;
  const Page page = display_.page();
  if (page != Page::Library && page != Page::Refine) return;  // creative is left through refine
  project_session::ProjectPlace place;
  place.workspace = page == Page::Library ? project_session::Workspace::Library : project_session::Workspace::Refine;
  place.photo_id = selected_photo_;
  projects_->remember_place(*current_project_, place);  // window state: a failure only loses the place
}

void ShellController::show_selected_photo() {
  if (!projects_ || !current_project_) return;
  if (!selected_photo_) {
    std::vector<PhotoRow> rows;
    if (projects_->list_photos(*current_project_, rows) != ResultCode::Ok || rows.empty()) return;
    selected_photo_ = rows.front().id;
  }
  if (current_photo_ != selected_photo_ || !session_) open_project_photo(*selected_photo_);
}

void ShellController::select_photo(std::int64_t photo_id) { select_photos({photo_id}, photo_id); }

std::vector<std::int64_t> ShellController::selected_photos() const {
  if (!selected_photo_) return {};
  if (std::find(selection_.begin(), selection_.end(), *selected_photo_) != selection_.end()) return selection_;
  return {*selected_photo_};
}

void ShellController::select_photos(const std::vector<std::int64_t>& photo_ids, std::int64_t primary) {
  if (!projects_ || !current_project_) return;
  selection_ = photo_ids;
  if (photo_ids.empty()) {
    selected_photo_.reset();
  } else {
    if (std::find(photo_ids.begin(), photo_ids.end(), primary) == photo_ids.end()) primary = photo_ids.back();
    selected_photo_ = primary;
  }
  remember_place();
  selection_changed();
}

void ShellController::watch_selection(QObject* owner, std::function<void()> changed) {
  selection_watchers_.emplace_back(owner, std::move(changed));
}

void ShellController::selection_changed() {
  for (auto it = selection_watchers_.begin(); it != selection_watchers_.end();) {
    if (it->first.isNull()) {
      it = selection_watchers_.erase(it);
      continue;
    }
    it->second();
    ++it;
  }
}

void ShellController::request_view_photo(std::int64_t photo_id) {
  if (!projects_ || !current_project_ || display_.page() != Page::Library) return;
  selected_photo_ = photo_id;
  display_.set_library_view("single");
  show_selected_photo();  // the large image is the rendered preview, edits included
  remember_place();
  if (changed_) changed_();
}

void ShellController::request_library_view(const std::string& view) {
  if (view != "grid" && view != "single") return;  // the filter view is not built yet
  if (display_.set_library_view(view) != ResultCode::Ok) return;
  if (view == "single") show_selected_photo();
  if (changed_) changed_();
}

void ShellController::close_photo() {
  notifier_.reset();
  session_.reset();
  current_photo_.reset();
  view_ = {};
}

void ShellController::close_project() {
  // Leaving the project closes the photo and the thumbnails (both renderd exit).
  close_photo();
  thumbnail_notifier_.reset();
  thumbnails_.reset();
  thumbnail_cache_.clear();
  thumbnail_requested_.clear();
  selected_photo_.reset();
  selection_.clear();
  current_project_.reset();
}

namespace {
constexpr std::uint32_t kThumbnailEdge = 320;  // 160-point cells on a 2x display
}

void ShellController::request_thumbnails() {
  if (!projects_ || !current_project_) return;
  std::vector<PhotoRow> rows;
  if (projects_->list_photos(*current_project_, rows) != ResultCode::Ok) return;
  for (const auto& row : rows) {
    if (thumbnail_requested_.count(row.id) == 0) request_thumbnail(row.id);
  }
}

void ShellController::request_thumbnail(std::int64_t photo_id) {
  if (!projects_) return;
  std::string diagnostic;
  if (!thumbnails_) {
    if (preview_channel::ThumbnailService::start(preview_options_.renderd_executable, preview_options_.log_directory,
                                                 preview_options_.connect_timeout_ms, thumbnails_,
                                                 diagnostic) != ResultCode::Ok) {
      qWarning("thumbnails unavailable: %s", diagnostic.c_str());
      return;
    }
    thumbnail_notifier_ = std::make_unique<QSocketNotifier>(thumbnails_->fd(), QSocketNotifier::Read);
    QObject::connect(thumbnail_notifier_.get(), &QSocketNotifier::activated, thumbnail_notifier_.get(),
                     [this] { read_thumbnails(); });
  }
  thumbnail_requested_.insert(photo_id);
  std::string path;
  if (projects_->resolve_photo(photo_id, path, diagnostic) != ResultCode::Ok) return;  // missing: no thumbnail
  std::vector<std::uint8_t> document;
  if (projects_->document_bytes(photo_id, document) != ResultCode::Ok) document.clear();  // show the original
  if (thumbnails_->request(photo_id, projects_->image_id(photo_id), path,
                           std::move(document), kThumbnailEdge, diagnostic) != ResultCode::Ok) {
    qWarning("thumbnail request failed: %s", diagnostic.c_str());
  }
}

void ShellController::read_thumbnails() {
  if (!thumbnails_) return;
  for (;;) {
    preview_channel::ThumbnailService::Thumbnail thumbnail;
    std::string diagnostic;
    const ResultCode code = thumbnails_->take(thumbnail, diagnostic);
    if (code == ResultCode::Cancelled) break;
    if (code != ResultCode::Ok) {
      qWarning("thumbnail renderd went away: %s", diagnostic.c_str());
      thumbnail_notifier_.reset();
      thumbnails_.reset();
      thumbnail_requested_.clear();  // a later request starts a new one
      break;
    }
    if (thumbnail.code != ResultCode::Ok) {
      qWarning("thumbnail %lld failed: %s", static_cast<long long>(thumbnail.key), thumbnail.diagnostic.c_str());
      continue;
    }
    QImage image(thumbnail.rgba.data(), static_cast<int>(thumbnail.width), static_cast<int>(thumbnail.height),
                 static_cast<qsizetype>(thumbnail.stride), QImage::Format_RGBA8888_Premultiplied);
    const QImage owned = image.copy();
    thumbnail_cache_[thumbnail.key] = owned;
    for (auto it = thumbnail_watchers_.begin(); it != thumbnail_watchers_.end();) {
      if (!it->first) {
        it = thumbnail_watchers_.erase(it);
        continue;
      }
      it->second(thumbnail.key, owned);
      ++it;
    }
  }
}

QImage ShellController::thumbnail(std::int64_t photo_id) const {
  const auto found = thumbnail_cache_.find(photo_id);
  return found == thumbnail_cache_.end() ? QImage() : found->second;
}

void ShellController::watch_thumbnails(QObject* owner, std::function<void(std::int64_t, const QImage&)> show) {
  thumbnail_watchers_.emplace_back(owner, std::move(show));
}

void ShellController::show_notices(const std::vector<std::string>& notices, NoticeScene scene, ResultCode code) {
  for (const auto& text : notices) display_.post_notice(UserNotice{text, true, false});
  if (notices.empty() && code != ResultCode::Ok) display_.post_notice(notice_for(scene, code));
}

void ShellController::attach_projects(std::unique_ptr<project_session::ProjectSession> projects,
                                      preview_channel::PreviewSessionOptions preview) {
  projects_ = std::move(projects);
  preview_options_ = std::move(preview);
  if (changed_) changed_();
}

void ShellController::request_import(const std::vector<std::string>& paths) {
  if (!projects_ || paths.empty()) return;
  const bool in_project = current_project_.has_value() && display_.in_project();
  const auto outcome = in_project ? projects_->import_into(*current_project_, paths)
                                  : projects_->import_new_project(paths);
  if (!in_project && outcome.project_id != 0 && outcome.accepted > 0) {
    // Requirement 5.3: entering a project shows refine with the first photo.
    if (display_.open_project() == ResultCode::Ok) {
      current_project_ = outcome.project_id;
      filter_ = {};
      open_project_photo(outcome.photo_ids.front());
      remember_place();
    }
  }
  request_thumbnails();
  show_notices(outcome.notices, NoticeScene::Import, outcome.code);
  if (changed_) changed_();
}

ui::shell::IconDropAction ShellController::drop_on_app_icon(const std::vector<std::string>& paths, bool modal_open) {
  using ui::shell::IconDropAction;
  IconDropAction action = ui::shell::icon_drop_action(display_, modal_open);
  // A project page with no project behind it (development previews) has nothing to append to.
  if (!projects_ || paths.empty() || (action == IconDropAction::AppendToProject && !current_project_)) {
    action = IconDropAction::Ignore;
  }
  if (action != IconDropAction::Ignore) request_import(paths);  // new project on home, append in a project
  return action;
}

std::vector<ProjectEntry> ShellController::projects() const {
  std::vector<ProjectEntry> out;
  std::vector<ProjectRow> rows;
  if (projects_ && projects_->list_projects(rows) == ResultCode::Ok) {
    for (const auto& row : rows) out.push_back({row.id, row.name});
  }
  return out;
}

void ShellController::request_open_project(std::int64_t project_id) {
  // A project opens where it was left: the library (grid) or refine with the
  // selected photo; refine when nothing was remembered (requirement 5.3).
  // Evidence 2026-11-06-project-entry-observation.md.
  if (!projects_) return;
  const auto place = projects_->last_place(project_id);
  const Page landing = place.workspace == project_session::Workspace::Library ? Page::Library : Page::Refine;
  if (display_.open_project(landing) != ResultCode::Ok) return;
  current_project_ = project_id;
  filter_ = {};
  selected_photo_ = place.photo_id;
  if (landing == Page::Library) {
    display_.set_library_view("grid");
  } else {
    show_selected_photo();
  }
  request_thumbnails();
  if (changed_) changed_();
}

std::vector<PhotoEntry> ShellController::photos() const {
  std::vector<PhotoEntry> out;
  std::vector<PhotoRow> rows;
  if (projects_ && current_project_ &&
      projects_->list_photos(*current_project_, rows, photo_filtered() ? &filter_ : nullptr) == ResultCode::Ok) {
    for (const auto& row : rows) {
      // Only an abnormal original (missing, delete pending) is worth a line.
      out.push_back({row.id, row.file_name, row.presence == kPresenceEditable ? std::string() : row.presence,
                     row.edit_status, projects_->copy_sequence(row.id), row.rating, row.flag, row.color_label});
    }
  }
  return out;
}

void ShellController::request_virtual_copy(const std::vector<std::int64_t>& photo_ids) {
  if (!projects_ || !current_project_ || photo_ids.empty()) return;
  std::vector<std::int64_t> created;
  for (const std::int64_t id : photo_ids) {
    std::int64_t copy = 0;
    std::string diagnostic;
    const ResultCode code = projects_->create_virtual_copy(id, copy, diagnostic);
    if (code != ResultCode::Ok) {
      qWarning("virtual copy: %s", diagnostic.c_str());
      show_notices({}, NoticeScene::OpenDocument, code);
      continue;
    }
    created.push_back(copy);
  }
  if (created.empty()) {
    if (changed_) changed_();
    return;
  }
  request_thumbnails();
  // 新副本成为选中的（图库里一眼看到它们在哪）。
  select_photos(created, created.back());
  if (changed_) changed_();
}

bool ShellController::photo_filtered() const {
  const PhotoQuery none;
  return filter_.rating_min != none.rating_min || filter_.rating_max != none.rating_max || !filter_.flags.empty() ||
         !filter_.colors.empty() || !filter_.formats.empty() || !filter_.edit_statuses.empty() ||
         !filter_.export_statuses.empty() || filter_.sort != none.sort || filter_.descending;
}

std::size_t ShellController::photo_total() const {
  std::vector<PhotoRow> rows;
  if (!projects_ || !current_project_ || projects_->list_photos(*current_project_, rows) != ResultCode::Ok) return 0;
  return static_cast<std::size_t>(std::count_if(rows.begin(), rows.end(), [](const PhotoRow& row) { return row.id > 0; }));
}

void ShellController::request_photo_filter(const PhotoQuery& query) {
  if (!projects_ || !current_project_) return;
  const PhotoQuery before = filter_;
  filter_ = query;
  std::vector<PhotoRow> rows;
  if (projects_->list_photos(*current_project_, rows, photo_filtered() ? &filter_ : nullptr) != ResultCode::Ok) {
    filter_ = before;  // 条件不合法：保持原来的
    show_notices({}, NoticeScene::OpenDocument, ResultCode::StateConflict);
    return;
  }
  // 被筛掉的照片不再选中；正在精修的那张不关（它还开着，只是不在列表里）。
  std::vector<std::int64_t> kept;
  for (const std::int64_t id : selection_) {
    if (std::any_of(rows.begin(), rows.end(), [id](const PhotoRow& row) { return row.id == id; })) kept.push_back(id);
  }
  if (kept.size() != selection_.size()) {
    selection_ = kept;
    selected_photo_ = kept.empty() ? std::nullopt : std::optional<std::int64_t>(kept.back());
    selection_changed();
  }
  request_thumbnails();
  if (changed_) changed_();
}

namespace {

// 历史步骤的名字：第一步用内核的名字，其余是“滑杆名 值”；找不到滑杆名就写能力和参数编号。
QString history_text(const preview_channel::PreviewSession::HistoryEntry& entry) {
  if (!entry.label.empty()) return QString::fromStdString(entry.label);
  if (entry.capability_id == preview_channel::PreviewSession::kPasteStep) return qtext("history.paste");
  QString name = QString::fromStdString(entry.capability_id + "/" + entry.parameter_id);
  auto rows = ui::panels::refine_color_rows();
  for (auto& row : ui::panels::refine_post_color_rows()) rows.push_back(std::move(row));
  for (const auto& row : rows) {
    if (row.binding && row.binding->capability_id == entry.capability_id &&
        row.binding->parameter_id == entry.parameter_id) {
      name = QString::fromStdString(row.label);
      if (entry.capability_id == "color.post") name = qtext("history.post").arg(name);
      break;
    }
  }
  return qtext("history.step").arg(name, QString::fromStdString(entry.display_value));
}

}  // namespace

std::vector<HistoryRow> ShellController::history() const {
  std::vector<HistoryRow> out;
  if (!session_) return out;
  const std::size_t current = session_->history_current();
  const auto steps = session_->history();
  for (std::size_t index = 0; index < steps.size(); ++index) out.push_back({history_text(steps[index]), index == current});
  return out;
}

void ShellController::history_changed() {
  for (auto it = history_watchers_.begin(); it != history_watchers_.end();) {
    if (it->first.isNull()) {
      it = history_watchers_.erase(it);
      continue;
    }
    it->second();
    ++it;
  }
}

void ShellController::watch_history(QObject* owner, std::function<void()> changed) {
  history_watchers_.emplace_back(owner, std::move(changed));
}

void ShellController::request_history_step(std::size_t index) {
  if (!session_) return;
  std::string diagnostic;
  const ResultCode code = session_->select_history(index, diagnostic);
  if (code != ResultCode::Ok) qWarning("history: %s", diagnostic.c_str());
  view_.status = qtext("refine.preview.loading");
  if (changed_) changed_();  // the sliders show the document as it is now
}

void ShellController::request_undo() {
  if (!session_) return;
  std::string diagnostic;
  if (session_->undo(diagnostic) != ResultCode::Ok) return;  // nothing to undo is not an error
  view_.status = qtext("refine.preview.loading");
  if (changed_) changed_();
}

void ShellController::request_redo() {
  if (!session_) return;
  std::string diagnostic;
  if (session_->redo(diagnostic) != ResultCode::Ok) return;
  view_.status = qtext("refine.preview.loading");
  if (changed_) changed_();
}

namespace {
bool contains(const std::vector<std::int64_t>& items, std::int64_t id) {
  return std::find(items.begin(), items.end(), id) != items.end();
}
}  // namespace

void ShellController::refresh_after_effect(const std::vector<std::int64_t>& items) {
  for (const std::int64_t id : items) request_thumbnail(id);
  if (changed_) changed_();
}

void ShellController::request_copy_effect() {
  if (!projects_ || !current_project_) return;
  const auto source = display_.page() == Page::Library ? selected_photo_ : current_photo_;
  if (!source) return;
  EffectClipboard clipboard;
  if (projects_->copy_effect(*source, clipboard) != ResultCode::Ok) {
    show_notices({}, NoticeScene::OpenDocument, ResultCode::StateConflict);
    if (changed_) changed_();
    return;
  }
  clipboard_ = std::move(clipboard);
}

void ShellController::request_paste_effect() {
  if (!projects_ || !current_project_ || !clipboard_) return;
  const std::vector<std::int64_t> targets =
      display_.page() == Page::Library ? selected_photos()
                                       : (current_photo_ ? std::vector<std::int64_t>{*current_photo_}
                                                         : std::vector<std::int64_t>{});
  if (targets.empty()) return;
  std::vector<std::int64_t> others;
  ResultCode failure = ResultCode::Ok;
  for (const std::int64_t id : targets) {
    if (session_ && current_photo_ == id && display_.page() == Page::Refine) {
      std::string diagnostic;
      const ResultCode code = session_->paste(*clipboard_, diagnostic);  // a step of its own in the history
      if (code != ResultCode::Ok) failure = code;
      view_.status = qtext("refine.preview.loading");
    } else {
      others.push_back(id);
    }
  }
  std::vector<project_session::ProjectSession::EffectOutcome> outcomes;
  const bool reopen = session_ && current_photo_ && contains(others, *current_photo_);
  const auto reopened = current_photo_;
  if (reopen) close_photo();
  if (!others.empty() && projects_->paste_effect(*clipboard_, others, outcomes) != ResultCode::Ok) {
    failure = ResultCode::StateConflict;
  }
  if (failure != ResultCode::Ok) show_notices({}, NoticeScene::OpenDocument, failure);
  if (reopen && reopened) open_project_photo(*reopened);
  refresh_after_effect(others);
}

std::size_t ShellController::sync_target_count() const {
  const auto source = display_.page() == Page::Library ? selected_photo_ : current_photo_;
  if (!source) return 0;
  std::size_t count = 0;
  for (const std::int64_t id : selection_) count += id != *source && projects_ != nullptr && projects_->source_photo(id) != 0;
  return count;
}

std::size_t ShellController::request_sync(const std::vector<std::string>& checked_ids) {
  if (!projects_ || !current_project_) return 0;
  const auto source = display_.page() == Page::Library ? selected_photo_ : current_photo_;
  if (!source) return 0;
  std::vector<std::int64_t> targets;
  for (const std::int64_t id : selection_) {
    if (id != *source) targets.push_back(id);
  }
  if (targets.empty()) return 0;
  const bool reopen = session_ && current_photo_ && contains(targets, *current_photo_);
  const auto reopened = current_photo_;
  if (reopen) close_photo();
  std::vector<project_session::ProjectSession::EffectOutcome> outcomes;
  projects_->sync_effect(*source, checked_ids, targets, outcomes);
  std::size_t failed = 0;
  for (const auto& outcome : outcomes) failed += outcome.code != ResultCode::Ok;
  if (failed != 0) show_notices({}, NoticeScene::OpenDocument, ResultCode::StateConflict);
  if (reopen && reopened) open_project_photo(*reopened);
  refresh_after_effect(targets);
  return failed;
}

void ShellController::request_rating(const std::vector<std::int64_t>& photo_ids, int rating) {
  if (!projects_ || photo_ids.empty()) return;
  const ResultCode code = projects_->set_rating(photo_ids, rating);
  if (code != ResultCode::Ok) show_notices({}, NoticeScene::OpenDocument, code);
  if (changed_) changed_();
}

void ShellController::request_flag(const std::vector<std::int64_t>& photo_ids, const std::string& flag) {
  if (!projects_ || photo_ids.empty()) return;
  const ResultCode code = projects_->set_flag(photo_ids, flag);
  if (code != ResultCode::Ok) show_notices({}, NoticeScene::OpenDocument, code);
  if (changed_) changed_();
}

void ShellController::request_color(const std::vector<std::int64_t>& photo_ids, const std::string& color) {
  if (!projects_ || photo_ids.empty()) return;
  const ResultCode code = projects_->set_color(photo_ids, color);
  if (code != ResultCode::Ok) show_notices({}, NoticeScene::OpenDocument, code);
  if (changed_) changed_();
}

void ShellController::request_delete_photos(const std::vector<std::int64_t>& photo_ids) {
  if (!projects_ || !current_project_ || photo_ids.empty()) return;
  std::string diagnostic;
  const ResultCode code = projects_->trash_items(photo_ids, diagnostic);
  if (code != ResultCode::Ok) show_notices({}, NoticeScene::OpenDocument, code);
  const auto gone = [&photo_ids](std::int64_t id) {
    return std::find(photo_ids.begin(), photo_ids.end(), id) != photo_ids.end();
  };
  // 原图删了，它的副本也跟着走：以列表为准，不在列表里的都不再选、不再打开。
  std::vector<std::int64_t> alive;
  for (const auto& entry : photos()) alive.push_back(entry.id);
  const auto present = [&alive](std::int64_t id) { return std::find(alive.begin(), alive.end(), id) != alive.end(); };
  if (current_photo_ && (gone(*current_photo_) || !present(*current_photo_))) close_photo();
  std::vector<std::int64_t> kept;
  for (const std::int64_t id : selection_) {
    if (present(id)) kept.push_back(id);
  }
  selection_ = kept;
  selected_photo_ = kept.empty() ? std::nullopt : std::optional<std::int64_t>(kept.back());
  remember_place();
  selection_changed();
  if (changed_) changed_();
}

void ShellController::request_delete_project(std::int64_t project_id) {
  if (!projects_) return;
  std::string diagnostic;
  const ResultCode code = projects_->trash_project(project_id, diagnostic);
  if (code != ResultCode::Ok) show_notices({}, NoticeScene::OpenDocument, code);
  if (changed_) changed_();
}

std::vector<TrashItem> ShellController::trash() const {
  std::vector<TrashItem> out;
  std::vector<project_session::ProjectSession::TrashEntry> entries;
  if (!projects_ || projects_->list_trash(entries) != ResultCode::Ok) return out;
  for (const auto& entry : entries) {
    TrashItem item;
    item.kind = entry.subject == kTrashProject ? TrashKind::Project
                                               : (entry.subject == kTrashVirtualCopy ? TrashKind::Copy : TrashKind::Photo);
    item.id = entry.id;
    item.name = entry.name;
    item.project = entry.project_name;
    item.copy = entry.copy;
    item.days_left = entry.days_left;
    out.push_back(std::move(item));
  }
  return out;
}

void ShellController::request_restore(TrashKind kind, std::int64_t id) {
  if (!projects_) return;
  project_session::ProjectSession::TrashEntry entry;
  entry.subject = kind == TrashKind::Project ? kTrashProject : (kind == TrashKind::Copy ? kTrashVirtualCopy : kTrashPhoto);
  entry.id = id;
  std::string diagnostic;
  const ResultCode code = projects_->restore(entry, diagnostic);
  if (code != ResultCode::Ok) show_notices({}, NoticeScene::OpenDocument, code);
  request_thumbnails();
  if (changed_) changed_();
}

void ShellController::request_erase_project(std::int64_t project_id) {
  if (!projects_) return;
  std::string diagnostic;
  const ResultCode code = projects_->erase_trashed_project(project_id, diagnostic);
  if (code != ResultCode::Ok) show_notices({}, NoticeScene::OpenDocument, code);
  if (changed_) changed_();
}

void ShellController::request_open_photo(std::int64_t photo_id) {
  if (!projects_ || !current_project_) return;
  // Navigate first: a page change clears notices, and opening may post one.
  if (display_.page() != Page::Refine) display_.navigate(Page::Refine);
  open_project_photo(photo_id);
  remember_place();
  if (changed_) changed_();
}

void ShellController::open_project_photo(std::int64_t photo_id) {
  close_photo();
  current_photo_ = photo_id;
  selected_photo_ = photo_id;
  std::string path;
  std::string diagnostic;
  const ResultCode resolved = projects_->resolve_photo(photo_id, path, diagnostic);
  if (resolved != ResultCode::Ok) {
    view_.status = qtext("refine.preview.failed") + QString::fromStdString(diagnostic);
    display_.post_notice(notice_for(NoticeScene::OpenDocument, resolved));
    return;
  }
  auto options = preview_options_;
  options.document_directory = projects_->document_directory();
  options.document_id = projects_->document_name(photo_id);
  if (open_photo(path, options, diagnostic) == ResultCode::Ok) {
    session_->on_committed([this, photo_id] {
      projects_->mark_edited(photo_id);
      request_thumbnail(photo_id);  // the thumbnail follows the committed edit
      history_changed();
    });
  }
}

ResultCode ShellController::open_photo(const std::string& photo_path,
                                       const preview_channel::PreviewSessionOptions& options,
                                       std::string& diagnostic) {
  notifier_.reset();
  session_.reset();
  view_ = {};
  const ResultCode code = preview_channel::PreviewSession::open(photo_path, options, session_, diagnostic);
  if (code != ResultCode::Ok) {
    view_.status = qtext("refine.preview.failed") + QString::fromStdString(diagnostic);
    publish();
    return code;
  }
  notifier_ = std::make_unique<QSocketNotifier>(session_->fd(), QSocketNotifier::Read);
  QObject::connect(notifier_.get(), &QSocketNotifier::activated, notifier_.get(), [this] { read_frames(); });
  session_->on_committed([this] { history_changed(); });  // a project photo replaces this with its own
  // First frame: the committed document as it is.
  const ui::panels::SliderBinding exposure{"color.global", "exposure"};
  const auto current = parameter_value(exposure);
  request_parameter(exposure, current ? *current : ui::panels::slider_spec("", exposure).default_text, false);
  view_.status = qtext("refine.preview.loading");
  publish();
  return ResultCode::Ok;
}

std::optional<std::string> ShellController::parameter_value(const ui::panels::SliderBinding& binding) const {
  if (!session_) return std::nullopt;
  return session_->document().effective_parameter(binding.capability_id, binding.parameter_id);
}

void ShellController::request_parameter(const ui::panels::SliderBinding& binding, const std::string& display_value,
                                        bool commit) {
  if (!session_) return;
  std::string diagnostic;
  const ResultCode code = commit ? session_->commit(binding.capability_id, binding.parameter_id, display_value, diagnostic)
                                 : session_->preview(binding.capability_id, binding.parameter_id, display_value, diagnostic);
  if (code != ResultCode::Ok) {
    qWarning("preview %s/%s=%s: %s", binding.capability_id.c_str(), binding.parameter_id.c_str(),
             display_value.c_str(), diagnostic.c_str());
  }
}

void ShellController::watch_preview(QObject* owner, std::function<void(const PreviewView&)> show) {
  watcher_owner_ = owner;
  watcher_ = std::move(show);
  publish();
}

void ShellController::publish() {
  if (watcher_ && watcher_owner_) watcher_(view_);
}

void ShellController::read_frames() {
  if (!session_) return;
  bool changed = false;
  for (;;) {
    preview_channel::PreviewFrame frame;
    std::string diagnostic;
    const ResultCode code = session_->take_frame(frame, diagnostic);
    if (code == ResultCode::Cancelled) {
      // A stale frame was dropped: look for the next one; otherwise nothing is waiting.
      if (diagnostic.find("older") != std::string::npos) continue;
      break;
    }
    if (code != ResultCode::Ok) {
      // renderd went away: keep the last frame, say why, stop watching.
      notifier_.reset();
      view_.status = qtext("refine.preview.failed") + QString::fromStdString(diagnostic);
      changed = true;
      break;
    }
    if (frame.result.code != ResultCode::Ok) {
      view_.status = qtext("refine.preview.failed") +
                     QString::fromStdString(frame.result.diagnostic);
      changed = true;
      continue;
    }
    const auto& header = frame.result.header;
    QImage image(frame.rgba.data(), static_cast<int>(header.width), static_cast<int>(header.height),
                 static_cast<qsizetype>(header.stride), QImage::Format_RGBA8888_Premultiplied);
    view_.image = image.copy();  // own the pixels; frame.rgba goes away
    view_.histogram = ui::panels::rgb_histogram(frame.rgba.data(), header.width, header.height, header.stride,
                                                frame.rgba.size());
    view_.status.clear();
    for (const auto& note : frame.result.diagnostics) {
      qInfo("preview note %s: %s", note.capability_id.c_str(), note.reason.c_str());
    }
    ++frames_shown_;
    changed = true;
  }
  if (changed) publish();
}

void ShellController::attach_exports(const SessionState& session, export_session::ExportOptions options) {
  if (!projects_) return;
  std::string diagnostic;
  if (export_session::ExportSession::open(*projects_, session, std::move(options), exports_, diagnostic) !=
      ResultCode::Ok) {
    qWarning("exports unavailable: %s", diagnostic.c_str());
    return;
  }
  // Tasks queued by an earlier session (or left by a crash, now failed and retryable).
  if (exports_->resume(diagnostic) != ResultCode::Ok) qWarning("export resume: %s", diagnostic.c_str());
  watch_exportd();
}

std::optional<ExportTarget> ShellController::export_target() const {
  if (!exports_ || !current_project_ || !selected_photo_) return std::nullopt;
  ExportTarget target;
  // The library exports its selection; refine the photo it shows.
  target.photo_ids = display_.page() == Page::Library ? selected_photos() : std::vector<std::int64_t>{*selected_photo_};
  for (const auto& entry : projects()) {
    if (entry.id == *current_project_) target.project_name = entry.name;
  }
  std::string path;
  std::string diagnostic;
  if (projects_->resolve_photo(*selected_photo_, path, diagnostic) == ResultCode::Ok) {
    const auto slash = path.rfind('/');
    target.original_folder = slash == std::string::npos || slash == 0 ? "/" : path.substr(0, slash);
  }
  target.defaults = exports_->defaults();
  target.estimate = exports_->estimate(target.photo_ids);
  target.remaining_sheets = exports_->remaining_sheets();
  return target;
}

std::vector<std::string> ShellController::request_export(const ExportRequest& request,
                                                         const ExportDefaults& defaults, bool& ok) {
  ok = false;
  std::vector<std::string> notices;
  if (!exports_) return notices;
  std::string diagnostic;
  if (exports_->remember_defaults(defaults) != ResultCode::Ok) qWarning("export defaults not saved");
  const ResultCode code = exports_->submit(request, notices, diagnostic);
  if (code != ResultCode::Ok && !diagnostic.empty()) qWarning("export: %s", diagnostic.c_str());
  ok = code == ResultCode::Ok;
  watch_exportd();
  exports_changed();
  return notices;
}

std::optional<std::int64_t> ShellController::remaining_sheets() const {
  if (!exports_) return std::nullopt;
  return exports_->remaining_sheets();
}

std::vector<ExportRow> ShellController::export_rows() const {
  return exports_ ? exports_->rows() : std::vector<ExportRow>{};
}

bool ShellController::store_watermark(const std::string& path, std::string& material, std::string& reason) {
  if (!exports_) {
    reason = qtext("export.queue.unavailable").toStdString();
    return false;
  }
  return exports_->store_watermark(path, material, reason) == ResultCode::Ok;
}

std::string ShellController::watermark_file(const std::string& material) const {
  return exports_ ? exports_->watermark_path(material) : std::string();
}

void ShellController::remember_export_defaults(const ExportDefaults& defaults) {
  if (exports_ && exports_->remember_defaults(defaults) != ResultCode::Ok) qWarning("export defaults not saved");
}

void ShellController::request_export_retry(const std::string& export_id) {
  if (!exports_) return;
  std::string diagnostic;
  if (exports_->retry(export_id, diagnostic) != ResultCode::Ok) qWarning("export retry: %s", diagnostic.c_str());
  watch_exportd();
  exports_changed();
}

void ShellController::request_export_cancel(const std::string& export_id) {
  if (!exports_) return;
  std::string diagnostic;
  if (exports_->cancel(export_id, diagnostic) != ResultCode::Ok) qWarning("export cancel: %s", diagnostic.c_str());
  exports_changed();
}

void ShellController::watch_exports(QObject* owner, std::function<void()> changed) {
  export_watchers_.emplace_back(owner, std::move(changed));
}

void ShellController::watch_exportd() {
  const int fd = exports_ ? exports_->fd() : -1;
  if (export_notifier_ && static_cast<int>(export_notifier_->socket()) == fd) return;
  if (export_notifier_) {
    // Possibly called from the notifier's own signal: let Qt delete it later.
    export_notifier_->setEnabled(false);
    export_notifier_.release()->deleteLater();
  }
  if (fd < 0) return;
  export_notifier_ = std::make_unique<QSocketNotifier>(fd, QSocketNotifier::Read);
  QObject::connect(export_notifier_.get(), &QSocketNotifier::activated, export_notifier_.get(),
                   [this] { read_exports(); });
}

void ShellController::read_exports() {
  if (!exports_) return;
  const bool changed = exports_->read_progress();
  watch_exportd();  // exportd may have gone
  if (changed) exports_changed();
}

void ShellController::exports_changed() {
  for (auto it = export_watchers_.begin(); it != export_watchers_.end();) {
    if (it->first.isNull()) {
      it = export_watchers_.erase(it);
      continue;
    }
    it->second();
    ++it;
  }
}

}  // namespace pixcake::shell
