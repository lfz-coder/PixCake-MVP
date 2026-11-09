#pragma once

#include "page_kit.hpp"
#include "histogram_refresh_gate.hpp"

#include "pixcake/export_session/export_session.hpp"
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
  // 接上导出（需求第 19 节）：<数据目录>/exports 的导出日志，导出进程按需启动。上次没交出去的任务接着交。
  void attach_exports(const SessionState& session, export_session::ExportOptions options);
  std::optional<ExportTarget> export_target() const override;
  bool has_exports() const override { return exports_ != nullptr; }
  std::vector<std::string> request_export(const ExportRequest& request, const ExportDefaults& defaults,
                                          bool& ok) override;
  std::vector<ExportRow> export_rows() const override;
  std::optional<std::int64_t> remaining_sheets() const override;
  bool store_watermark(const std::string& path, std::string& material, std::string& reason) override;
  std::string watermark_file(const std::string& material) const override;
  void remember_export_defaults(const ExportDefaults& defaults) override;
  void request_export_retry(const std::string& export_id) override;
  void request_export_cancel(const std::string& export_id) override;
  void watch_exports(QObject* owner, std::function<void()> changed) override;
  bool can_import() const override { return projects_ != nullptr; }
  void request_import(const std::vector<std::string>& paths) override;
  // 需求 §3.2：文件拖到应用图标（程序坞或访达里的 .app）。按当前页面和模态层决定去向，返回实际采取的去向。
  ui::shell::IconDropAction drop_on_app_icon(const std::vector<std::string>& paths, bool modal_open);
  std::vector<ProjectEntry> projects() const override;
  void request_open_project(std::int64_t project_id) override;
  std::vector<PhotoEntry> photos() const override;
  std::optional<std::int64_t> current_photo() const override { return selected_photo_; }
  void select_photo(std::int64_t photo_id) override;
  std::vector<std::int64_t> selected_photos() const override;
  void select_photos(const std::vector<std::int64_t>& photo_ids, std::int64_t primary) override;
  void watch_selection(QObject* owner, std::function<void()> changed) override;
  void request_open_photo(std::int64_t photo_id) override;
  void request_virtual_copy(const std::vector<std::int64_t>& photo_ids) override;
  PhotoQuery photo_filter() const override { return filter_; }
  void request_photo_filter(const PhotoQuery& query) override;
  bool photo_filtered() const override;
  std::size_t photo_total() const override;
  void request_copy_effect() override;
  void request_paste_effect() override;
  bool can_paste_effect() const override { return clipboard_.has_value(); }
  std::vector<SyncItem> sync_items() const override { return sync_tree(); }
  std::size_t sync_target_count() const override;
  std::size_t request_sync(const std::vector<std::string>& checked_ids) override;
  std::vector<project_session::ProjectSession::PresetRow> presets() const override;
  std::vector<PresetCapability> adjusted_preset_capabilities() const override;
  bool request_save_preset(const project_session::ProjectSession::PresetDraft& draft, std::string& problem) override;
  void request_rename_preset(const std::string& id, const std::string& name) override;
  void request_delete_preset(const std::string& id) override;
  bool preset_overwrites_color(const std::string& id) const override;
  void request_apply_preset(const std::string& id) override;
  void request_reset(ResetScope scope) override;
  std::vector<HistoryRow> history() const override;
  void request_history_step(std::size_t index) override;
  void request_undo() override;
  void request_redo() override;
  void watch_history(QObject* owner, std::function<void()> changed) override;
  void request_rating(const std::vector<std::int64_t>& photo_ids, int rating) override;
  void request_flag(const std::vector<std::int64_t>& photo_ids, const std::string& flag) override;
  void request_color(const std::vector<std::int64_t>& photo_ids, const std::string& color) override;
  void request_delete_photos(const std::vector<std::int64_t>& photo_ids) override;
  void request_delete_project(std::int64_t project_id) override;
  std::vector<TrashItem> trash() const override;
  void request_restore(TrashKind kind, std::int64_t id) override;
  void request_erase_project(std::int64_t project_id) override;
  void request_view_photo(std::int64_t photo_id) override;
  void request_library_view(const std::string& view) override;
  std::optional<std::int64_t> current_project() const { return current_project_; }
  QImage thumbnail(std::int64_t photo_id) const override;
  void watch_thumbnails(QObject* owner, std::function<void(std::int64_t, const QImage&)> show) override;
  std::size_t thumbnails_ready() const { return thumbnail_cache_.size(); }
  bool has_photo() const override { return session_ != nullptr; }
  std::uint32_t preview_max_edge() const override { return preview_max_edge_; }
  void request_preview_max_edge(std::uint32_t max_edge) override;
  CanvasViewMode canvas_view_mode() const override { return canvas_view_mode_; }
  void request_canvas_view_mode(CanvasViewMode mode) override;
  void request_original_preview(bool visible) override;
  void request_parameters(const std::vector<std::pair<ui::panels::SliderBinding, std::string>>& values) override;
  std::vector<GuideLine> guides() const override { return guides_; }
  bool guides_visible() const override { return guides_visible_; }
  void request_create_guide(bool vertical) override;
  void request_toggle_guides() override;
  void request_move_guide(std::size_t index, double position) override;
  void request_remove_guide(std::size_t index) override;
  void request_remove_all_guides() override;
  void watch_guides(QObject* owner,
                    std::function<void(const std::vector<GuideLine>&, bool)> changed) override;
  std::vector<int> local_masks() const override;
  void request_remove_local_mask(int instance) override;
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
  void mark_high_quality_preview_request();
  void request_original_frame(bool interactive);
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
  // Follows exportd's connection: a notifier on its fd while it runs.
  void watch_exportd();
  void read_exports();
  void exports_changed();

  ShellDisplay display_;
  std::function<void()> changed_;
  std::unique_ptr<preview_channel::PreviewSession> session_;
  std::unique_ptr<QSocketNotifier> notifier_;
  QPointer<QObject> watcher_owner_;
  std::function<void(const PreviewView&)> watcher_;
  PreviewView view_;
  HistogramRefreshGate histogram_refresh_gate_;
  CanvasViewMode canvas_view_mode_ = CanvasViewMode::Single;
  bool original_preview_visible_ = false;
  std::optional<std::uint64_t> original_sequence_;
  std::map<std::uint64_t, std::uint32_t> original_requests_;
  std::uint32_t original_edge_ = 0;
  QImage original_image_;
  std::uint64_t last_edit_sequence_ = 0;
  std::uint64_t last_edit_frame_sequence_ = 0;
  bool last_edit_interactive_ = false;
  std::uint64_t frames_shown_ = 0;
  std::unique_ptr<project_session::ProjectSession> projects_;
  preview_channel::PreviewSessionOptions preview_options_;
  std::uint32_t preview_max_edge_ = 4000;
  std::optional<std::int64_t> current_project_;
  std::optional<std::int64_t> current_photo_;   // opened in the preview session
  std::optional<std::int64_t> selected_photo_;  // selected in the project (the last clicked)
  // Library multi-selection; meaningful only while it contains selected_photo_
  // (other paths that change selected_photo_ fall back to that one photo).
  std::vector<std::int64_t> selection_;
  std::vector<std::pair<QPointer<QObject>, std::function<void()>>> history_watchers_;
  std::vector<GuideLine> guides_;
  bool guides_visible_ = true;
  std::vector<std::pair<QPointer<QObject>, std::function<void(const std::vector<GuideLine>&, bool)>>> guide_watchers_;
  void guides_changed();
  void history_changed();
  std::optional<EffectClipboard> clipboard_;
  // 效果搬到别的照片后：缩略图跟着变；打开着的那张先关再开，免得内存里的文档和磁盘不一致。
  void refresh_after_effect(const std::vector<std::int64_t>& items);
  PhotoQuery filter_;  // 当前项目的筛选与排序；换项目时清空
  std::vector<std::pair<QPointer<QObject>, std::function<void()>>> selection_watchers_;
  void selection_changed();
  std::unique_ptr<preview_channel::ThumbnailService> thumbnails_;
  std::unique_ptr<QSocketNotifier> thumbnail_notifier_;
  std::map<std::int64_t, QImage> thumbnail_cache_;
  std::set<std::int64_t> thumbnail_requested_;
  std::vector<std::pair<QPointer<QObject>, std::function<void(std::int64_t, const QImage&)>>> thumbnail_watchers_;
  // After projects_: holds a reference to it and is destroyed first.
  std::unique_ptr<export_session::ExportSession> exports_;
  std::unique_ptr<QSocketNotifier> export_notifier_;
  std::vector<std::pair<QPointer<QObject>, std::function<void()>>> export_watchers_;
};

}  // namespace pixcake::shell
