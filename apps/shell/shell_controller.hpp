#pragma once

#include "page_kit.hpp"

#include "pixcake/preview_channel/preview_session.hpp"
#include "pixcake/preview_channel/thumbnail_service.hpp"
#include "pixcake/project_session/project_session.hpp"

#include <QPointer>
#include <QSocketNotifier>

#include <functional>
#include <map>
#include <set>
#include <vector>
#include <memory>
#include <string>
#include <utility>

namespace pixcake::shell {

// 壳层里唯一改显示模型的地方（代码质量清单 Q-12）。页面只发请求；文档命令也从这里
// 经预览会话的命令入口进入，不从页面调。
class ShellController final : public PageActions {
 public:
  explicit ShellController(ShellDisplay display) : display_(std::move(display)) {}
  ~ShellController() override;

  const ShellDisplay& display() const { return display_; }
  void on_changed(std::function<void()> callback) { changed_ = std::move(callback); }

  void request_page(Page target) override;

  // 打开一张照片：启动 renderd、发送预览源并请求第一帧。失败时 diagnostic 说明原因。
  ResultCode open_photo(const std::string& photo_path, const preview_channel::PreviewSessionOptions& options,
                        std::string& diagnostic);

  // 登录后接上项目库；preview 是打开照片时的 renderd 设置（文档目录由项目库给）。
  void attach_projects(std::unique_ptr<project_session::ProjectSession> projects,
                       preview_channel::PreviewSessionOptions preview);
  bool can_import() const override { return projects_ != nullptr; }
  void request_import(const std::vector<std::string>& paths) override;
  // 需求 §3.2：文件拖到应用图标（程序坞或访达里的 .app）。按当前页面和模态层决定去向，返回实际采取的去向。
  ui::shell::IconDropAction drop_on_app_icon(const std::vector<std::string>& paths, bool modal_open);
  std::vector<ProjectEntry> projects() const override;
  void request_open_project(std::int64_t project_id) override;
  std::vector<PhotoEntry> photos() const override;
  std::optional<std::int64_t> current_photo() const override { return selected_photo_; }
  void select_photo(std::int64_t photo_id) override;
  void request_open_photo(std::int64_t photo_id) override;
  void request_view_photo(std::int64_t photo_id) override;
  void request_library_view(const std::string& view) override;
  std::optional<std::int64_t> current_project() const { return current_project_; }
  QImage thumbnail(std::int64_t photo_id) const override;
  void watch_thumbnails(QObject* owner, std::function<void(std::int64_t, const QImage&)> show) override;
  std::size_t thumbnails_ready() const { return thumbnail_cache_.size(); }
  bool has_photo() const override { return session_ != nullptr; }
  std::optional<std::string> parameter_value(const ui::panels::SliderBinding& binding) const override;
  void request_parameter(const ui::panels::SliderBinding& binding, const std::string& display_value,
                         bool commit) override;
  void watch_preview(QObject* owner, std::function<void(const PreviewView&)> show) override;
  const PreviewView& preview_view() const { return view_; }
  std::uint64_t frames_shown() const { return frames_shown_; }
  // The canvas shows the frame for the newest request (no request still in flight).
  bool preview_current() const { return session_ && session_->shown_sequence() == session_->latest_sequence(); }

 private:
  void read_frames();
  void publish();
  void close_photo();
  void open_project_photo(std::int64_t photo_id);
  void request_thumbnails();
  void request_thumbnail(std::int64_t photo_id);
  void read_thumbnails();
  void close_project();
  // Where the current project is (workspace and selected photo), for the next time it is opened.
  void remember_place();
  // Opens the selected photo (or the project's first) in the preview session unless it already is.
  void show_selected_photo();
  void show_notices(const std::vector<std::string>& notices, NoticeScene scene, ResultCode code);

  ShellDisplay display_;
  std::function<void()> changed_;
  std::unique_ptr<preview_channel::PreviewSession> session_;
  std::unique_ptr<QSocketNotifier> notifier_;
  QPointer<QObject> watcher_owner_;
  std::function<void(const PreviewView&)> watcher_;
  PreviewView view_;
  std::uint64_t frames_shown_ = 0;
  std::unique_ptr<project_session::ProjectSession> projects_;
  preview_channel::PreviewSessionOptions preview_options_;
  std::optional<std::int64_t> current_project_;
  std::optional<std::int64_t> current_photo_;   // opened in the preview session
  std::optional<std::int64_t> selected_photo_;  // selected in the project
  std::unique_ptr<preview_channel::ThumbnailService> thumbnails_;
  std::unique_ptr<QSocketNotifier> thumbnail_notifier_;
  std::map<std::int64_t, QImage> thumbnail_cache_;
  std::set<std::int64_t> thumbnail_requested_;
  std::vector<std::pair<QPointer<QObject>, std::function<void(std::int64_t, const QImage&)>>> thumbnail_watchers_;
};

}  // namespace pixcake::shell
