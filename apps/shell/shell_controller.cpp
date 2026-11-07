#include "shell_controller.hpp"

#include <QDebug>

namespace pixcake::shell {

ShellController::~ShellController() {
  notifier_.reset();
  session_.reset();  // closes the connection; renderd exits
}

void ShellController::request_page(Page target) {
  if (display_.navigate(target) != ResultCode::Ok) return;
  if (target == Page::Home) close_project();
  // Refine from the library tab with nothing open yet: show the project's first photo.
  if (target == Page::Refine && projects_ && current_project_ && !current_photo_) {
    std::vector<PhotoRow> rows;
    if (projects_->list_photos(*current_project_, rows) == ResultCode::Ok && !rows.empty()) {
      open_project_photo(rows.front().id);
    }
  }
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
  if (thumbnails_->request(photo_id, project_session::ProjectSession::document_id(photo_id), path,
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
    if (thumbnail.code != ResultCode::Ok) continue;
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
      open_project_photo(outcome.photo_ids.front());
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
  // A click on a project shows its photos in the library; a double-click on a
  // photo opens it in refine (user ruling 2026-11-06).  No photo is opened yet.
  if (!projects_ || display_.open_project(Page::Library) != ResultCode::Ok) return;
  current_project_ = project_id;
  request_thumbnails();
  if (changed_) changed_();
}

std::vector<PhotoEntry> ShellController::photos() const {
  std::vector<PhotoEntry> out;
  std::vector<PhotoRow> rows;
  if (projects_ && current_project_ && projects_->list_photos(*current_project_, rows) == ResultCode::Ok) {
    for (const auto& row : rows) {
      // Only an abnormal original (missing, delete pending) is worth a line.
      out.push_back({row.id, row.file_name, row.presence == kPresenceEditable ? std::string() : row.presence,
                     row.edit_status});
    }
  }
  return out;
}

void ShellController::request_open_photo(std::int64_t photo_id) {
  if (!projects_ || !current_project_) return;
  // Navigate first: a page change clears notices, and opening may post one.
  if (display_.page() != Page::Refine) display_.navigate(Page::Refine);
  open_project_photo(photo_id);
  if (changed_) changed_();
}

void ShellController::open_project_photo(std::int64_t photo_id) {
  close_photo();
  current_photo_ = photo_id;
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
  options.document_id = project_session::ProjectSession::document_id(photo_id);
  if (open_photo(path, options, diagnostic) == ResultCode::Ok) {
    session_->on_committed([this, photo_id] {
      projects_->mark_edited(photo_id);
      request_thumbnail(photo_id);  // the thumbnail follows the committed edit
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

}  // namespace pixcake::shell
