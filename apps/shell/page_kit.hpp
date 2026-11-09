#pragma once

// 页面构建的公共部件（代码质量清单 Q-12）：页面文件只画控件，状态变化经 PageActions 交给
// ShellController。文字来自 ui/shell 的文案表，图标按“组/条目编号”查。
#include "pixcake/export_session/export_session.hpp"
#include "guide_line.hpp"
#include "pixcake/project/records.hpp"
#include "pixcake/project_session/project_session.hpp"
#include "pixcake/sync/sync_batch.hpp"
#include "pixcake/ui/panels/histogram.hpp"
#include "pixcake/ui/panels/slider_spec.hpp"
#include "pixcake/ui/shell/page_copy.hpp"
#include "pixcake/ui/shell/shell_display.hpp"

#include <QFrame>
#include <QImage>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QShortcut>
#include <QString>
#include <QWidget>

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace pixcake::shell {

using ui::shell::CopyItem;
using ui::shell::Page;
using ui::shell::ShellDisplay;
using export_session::ExportDefaults;
using export_session::ExportRequest;
using export_session::ExportCharge;
using export_session::ExportRow;

// 画布要显示的东西：最新一帧和它的直方图，或者一行状态（生成中、失败原因）。
struct PreviewView {
  QImage image;
  ui::panels::RgbHistogram histogram;
  QString status;
};

// 主页项目列表的一项、图库里的一张照片（只读的显示数据）。
struct ProjectEntry {
  std::int64_t id = 0;
  std::string name;
};
// 回收站里的一行：项目、图片或虚拟副本。id 是项目编号、照片编号或副本编号（不是条目编号）。
enum class TrashKind { Project, Photo, Copy };
struct TrashItem {
  TrashKind kind = TrashKind::Project;
  std::int64_t id = 0;
  std::string name;
  std::string project;  // 图片和副本所在的项目
  std::int64_t copy = 0;
  std::int64_t days_left = 0;
};
// 历史面板的一行（需求 §8.3）：当前这张图本次打开以来的步骤。
struct HistoryRow {
  QString text;
  bool current = false;
};
struct PhotoEntry {
  std::int64_t id = 0;
  std::string file_name;
  std::string presence;     // 原图异常时的状态（如“原文件缺失”）；正常为空
  std::string edit_status;  // “已编辑”或空
  std::int64_t copy = 0;    // 虚拟副本的序号（从 1 递增）；原图为 0
  int rating = 0;           // 星级 0–5（副本和原图共用）
  std::string flag;         // 旗标：选取/排除/空
  std::string color;        // 色标：红黄绿蓝紫/空
};

// 导出弹窗要的东西（需求第 19 节）：选中的照片、项目名、第一张原图所在的文件夹、记住的默认值。
struct ExportTarget {
  std::vector<std::int64_t> photo_ids;
  std::string project_name;
  std::string original_folder;
  ExportDefaults defaults;
  // 第一张照片成品的像素大小（预览排版用；读原图文件头估的，不含裁剪，0 表示读不到）。
  int product_width = 0;
  int product_height = 0;
  // 精修效果图、免费效果图、原图的预估张数和这次要扣的张数；剩余张数来自本机账本。
  export_session::ExportEstimate estimate;
  std::int64_t remaining_sheets = 0;
};

// 页面发出的请求。实现方（ShellController）改显示模型；页面不直接改。
class PageActions {
 public:
  virtual ~PageActions() = default;
  virtual void request_page(Page target) = 0;

  // 预览：有打开的照片时为真；否则精修页的滑杆保持禁用。
  virtual bool has_photo() const { return false; }
  // 文档里这个参数当前的显示值（打开照片时滑杆停在这里）。
  virtual std::optional<std::string> parameter_value(const ui::panels::SliderBinding&) const { return std::nullopt; }
  // 拖动中 commit 为假（只预览）；松手或键盘改值 commit 为真（经命令入口提交）。
  virtual void request_parameter(const ui::panels::SliderBinding&, const std::string&, bool) {}
  // 同一个能力的多个参数一次提交（一步历史）：画布上拖一次渐变、添加一个子项。
  virtual void request_parameters(const std::vector<std::pair<ui::panels::SliderBinding, std::string>>&) {}
  // 画布登记显示回调；owner 删除后不再回调。登记时立即回调一次当前画面。
  virtual void watch_preview(QObject*, std::function<void(const PreviewView&)>) {}

  // Reference lines are workspace overlays, not edit-document parameters.
  virtual std::vector<GuideLine> guides() const { return {}; }
  virtual bool guides_visible() const { return true; }
  virtual void request_create_guide(bool) {}
  virtual void request_toggle_guides() {}
  virtual void request_move_guide(std::size_t, double) {}
  virtual void request_remove_guide(std::size_t) {}
  virtual void request_remove_all_guides() {}
  virtual void watch_guides(QObject*, std::function<void(const std::vector<GuideLine>&, bool)>) {}

  // 项目（需求 5.2/5.3）。导入：在主页新建项目；在项目里追加到当前项目。
  virtual bool can_import() const { return false; }
  virtual void request_import(const std::vector<std::string>&) {}
  virtual std::vector<ProjectEntry> projects() const { return {}; }
  virtual void request_open_project(std::int64_t) {}
  virtual std::vector<PhotoEntry> photos() const { return {}; }
  // 筛选与排序（需求 §6.2、§6.3）：当前项目图库的条件；photos() 已按它列出。
  virtual PhotoQuery photo_filter() const { return {}; }
  virtual void request_photo_filter(const PhotoQuery&) {}
  // 条件和默认（不限、导入顺序）不同。
  virtual bool photo_filtered() const { return false; }
  // 当前项目一共多少张（不受筛选影响，副本不算）。
  virtual std::size_t photo_total() const { return 0; }
  // 项目里选中的照片（图库单击选中；精修显示的就是它）。
  virtual std::optional<std::int64_t> current_photo() const { return std::nullopt; }
  virtual void select_photo(std::int64_t) {}
  // 图库多选：选中的照片（含 current_photo），按图库顺序。单选时只有一张，没选时为空。
  virtual std::vector<std::int64_t> selected_photos() const { return {}; }
  // 图库里改了选择：ids 是全部选中的，primary 是最后点的那张（成为 current_photo）。
  virtual void select_photos(const std::vector<std::int64_t>&, std::int64_t) {}
  // 选择变化时回调（导出按钮、“已选 N 张”跟着变）；owner 删除后不再回调。
  virtual void watch_selection(QObject*, std::function<void()>) {}
  // 复制、粘贴效果（需求 §18，⌘C、⌘V）和批量同步（⇧⌘S）。精修里作用于当前照片，图库里复制选中的那张、
  // 粘贴到选中的全部。同步以当前照片为来源，目标是选中的其他照片。
  virtual void request_copy_effect() {}
  virtual void request_paste_effect() {}
  // 局部调色现有的蒙版（实例编号，升序）和删除一个蒙版（新建靠写它的第一个参数）。
  virtual std::vector<int> local_masks() const { return {}; }
  virtual void request_remove_local_mask(int) {}
  virtual bool can_paste_effect() const { return false; }
  virtual std::vector<SyncItem> sync_items() const { return {}; }
  virtual std::size_t sync_target_count() const { return 0; }
  // 每个目标的结果；全部成功时 failed 为空。
  virtual std::size_t request_sync(const std::vector<std::string>&) { return 0; }
  // 预设（需求 §9）：本机“我的”预设。保存从当前照片已调整的能力里勾选；套用在精修里走命令入口（历史多一步），
  // 在图库里套用到选中的全部。套用会替换已有色彩时，界面先问。
  struct PresetCapability {
    std::string id;
    std::string module;
    std::string label;
  };
  virtual std::vector<project_session::ProjectSession::PresetRow> presets() const { return {}; }
  virtual std::vector<PresetCapability> adjusted_preset_capabilities() const { return {}; }
  virtual bool request_save_preset(const project_session::ProjectSession::PresetDraft&, std::string& problem) { problem.clear(); return false; }
  virtual void request_rename_preset(const std::string&, const std::string&) {}
  virtual void request_delete_preset(const std::string&) {}
  virtual bool preset_overwrites_color(const std::string&) const { return false; }
  virtual void request_apply_preset(const std::string&) {}
  // 历史（需求 §8.3）：撤销、重做、点某一步回到那一步。步骤变了（提交、撤销）时回调 watch_history 登记的函数。
  virtual std::vector<HistoryRow> history() const { return {}; }
  virtual void request_history_step(std::size_t) {}
  virtual void request_undo() {}
  virtual void request_redo() {}
  virtual void watch_history(QObject*, std::function<void()>) {}
  // 标记（需求 §6.1）：条目编号；副本记在原图上。旗标、色标传空串表示移除。
  virtual void request_rating(const std::vector<std::int64_t>&, int) {}
  virtual void request_flag(const std::vector<std::int64_t>&, const std::string&) {}
  virtual void request_color(const std::vector<std::int64_t>&, const std::string&) {}
  // 删除与回收站（需求 §7）：删照片、副本（条目编号）或项目都只进回收站，保留 30 天。
  virtual void request_delete_photos(const std::vector<std::int64_t>&) {}
  virtual void request_delete_project(std::int64_t) {}
  virtual std::vector<TrashItem> trash() const { return {}; }
  virtual void request_restore(TrashKind, std::int64_t) {}
  virtual void request_erase_project(std::int64_t) {}
  // 创建虚拟副本（需求 §7）：每个编号（原图或副本）各建一个，副本排在原图右边，继承当前全部调整。
  virtual void request_virtual_copy(const std::vector<std::int64_t>&) {}
  // 在精修打开（精修底部缩略图条单击）。
  virtual void request_open_photo(std::int64_t) {}
  // 图库里看大图：切到图库单视图（图库网格双击，与像素蛋糕一致，不进精修）。
  virtual void request_view_photo(std::int64_t) {}
  // 图库视图：grid、single（filter-view 还没做）。
  virtual void request_library_view(const std::string&) {}
  // 缩略图（renderd 生成，按已提交的编辑绘制）。还没有时返回空图；生成后经 watch_thumbnails 回调。
  virtual QImage thumbnail(std::int64_t) const { return {}; }
  virtual void watch_thumbnails(QObject*, std::function<void(std::int64_t, const QImage&)>) {}

  // 导出（需求第 19 节）。没有选中照片或导出不可用时 export_target 为空，导出按钮禁用。
  virtual std::optional<ExportTarget> export_target() const { return std::nullopt; }
  virtual bool has_exports() const { return false; }
  // 排进导出队列并交给导出进程；返回逐张的拒绝说明（“文件名: 原因”），队列不可用时 ok 为假。
  virtual std::vector<std::string> request_export(const ExportRequest&, const ExportDefaults&, bool& ok) {
    ok = false;
    return {};
  }
  virtual std::vector<ExportRow> export_rows() const { return {}; }
  // 本机账本的剩余张数；导出不可用时为空（个人中心按钮禁用）。
  virtual std::optional<std::int64_t> remaining_sheets() const { return std::nullopt; }
  // 水印素材复制进数据目录；成功时 material 是写进设置快照的名字，失败时 reason 是说明。
  virtual bool store_watermark(const std::string&, std::string&, std::string& reason) {
    reason.clear();
    return false;
  }
  // 已保存素材的文件路径（弹窗预览用），没有时为空。
  virtual std::string watermark_file(const std::string&) const { return {}; }
  // 只记住设置不导出（test 版取消弹窗后，水印开关和参数仍是上次的）。
  virtual void remember_export_defaults(const ExportDefaults&) {}
  virtual void request_export_retry(const std::string&) {}
  virtual void request_export_cancel(const std::string&) {}
  // 导出进度变化时回调（导出列表刷新）；owner 删除后不再回调。
  virtual void watch_exports(QObject*, std::function<void()>) {}
};

struct PageContext {
  const ShellDisplay& display;
  PageActions& actions;
};

QString qtext(std::string_view key);
QString qs(const std::string& value);
const CopyItem& copyItem(std::string_view list, std::string_view id);

QLabel* text(const QString& value, bool muted = false);
QPushButton* action(const QString& label, QWidget* parent, bool enabled = false);
QPushButton* iconAction(const QString& label, const QString& icon, QWidget* parent, bool enabled = false, int size = 18);
// 文案表里一组中的一项，图标按同一组查。
// 按快捷键表（ui/shell 的 shortcut_table）里的编号绑一个快捷键；objectName 是 shortcut-<编号>。
QShortcut* bindShortcut(QWidget* owner, std::string_view id, std::function<void()> run,
                        Qt::ShortcutContext context = Qt::WindowShortcut);
// 图库和精修都有的键：转到首页、图库、精修（⇧⌘0/1/2），导出设置 ⇧E，导出列表 ⌘S。
void bindProjectShortcuts(QWidget* page, PageActions* actions);
QPushButton* itemAction(std::string_view list, const CopyItem& item, QWidget* parent, bool enabled = false,
                        int size = 18);
// 图库、精修、创意三个页签。点击只发请求。
void addPageTabs(QHBoxLayout* layout, const PageContext& context);
QFrame* workspaceTool(std::string_view list, const CopyItem& item, QWidget* parent);
// renderd 的参考绘制画得出这个参数（精修里这些滑杆在照片打开时可用）：全图色彩的白平衡、影调、色阶、HSL、
// 黑白混合、可选颜色、颜色分级和配置文件（标准、黑白），以及细节里的颗粒。色彩编辑器还没有目标颜色参数，不算。
bool reference_drawn(const ui::panels::SliderBinding& binding);
// 参数模式的禁用规则按当前文档的值判断（例如黑白档下 HSL 不可用、颗粒效果为 0 时其余颗粒项不可用）。
std::optional<std::string> parameter_blocked(const PageActions* actions, const ui::panels::SliderBinding& binding);

// 底部缩略图条。有当前项目的照片时列出照片，单击调用 open（精修里打开、图库单视图里换大图、
// 图库网格里选中），“+”在能导入时可用。
QFrame* thumbnailStrip(QWidget* parent, bool library, PageActions* actions = nullptr,
                       std::function<void(std::int64_t)> open = {});
// 当前项目的照片列表：grid 为真是图库网格（单击选中，双击调用 open），否则是一行缩略图条（单击调用 open）。
// 缩略图生成后自动补上。
QListWidget* photoList(QWidget* parent, PageActions* actions, bool grid, std::function<void(std::int64_t)> open);
// 选择图片并导入（主页新建项目，项目内追加）。
void chooseAndImport(QWidget* parent, PageActions* actions);

}  // namespace pixcake::shell
