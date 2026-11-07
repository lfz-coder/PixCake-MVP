#pragma once

// 页面构建的公共部件（代码质量清单 Q-12）：页面文件只画控件，状态变化经 PageActions 交给
// ShellController。文字来自 ui/shell 的文案表，图标按“组/条目编号”查。
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
struct PhotoEntry {
  std::int64_t id = 0;
  std::string file_name;
  std::string presence;     // 原图异常时的状态（如“原文件缺失”）；正常为空
  std::string edit_status;  // “已编辑”或空
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
  // 画布登记显示回调；owner 删除后不再回调。登记时立即回调一次当前画面。
  virtual void watch_preview(QObject*, std::function<void(const PreviewView&)>) {}

  // 项目（需求 5.2/5.3）。导入：在主页新建项目；在项目里追加到当前项目。
  virtual bool can_import() const { return false; }
  virtual void request_import(const std::vector<std::string>&) {}
  virtual std::vector<ProjectEntry> projects() const { return {}; }
  virtual void request_open_project(std::int64_t) {}
  virtual std::vector<PhotoEntry> photos() const { return {}; }
  // 项目里选中的照片（图库单击选中；精修显示的就是它）。
  virtual std::optional<std::int64_t> current_photo() const { return std::nullopt; }
  virtual void select_photo(std::int64_t) {}
  // 在精修打开（精修底部缩略图条单击）。
  virtual void request_open_photo(std::int64_t) {}
  // 图库里看大图：切到图库单视图（图库网格双击，与像素蛋糕一致，不进精修）。
  virtual void request_view_photo(std::int64_t) {}
  // 图库视图：grid、single（filter-view 还没做）。
  virtual void request_library_view(const std::string&) {}
  // 缩略图（renderd 生成，按已提交的编辑绘制）。还没有时返回空图；生成后经 watch_thumbnails 回调。
  virtual QImage thumbnail(std::int64_t) const { return {}; }
  virtual void watch_thumbnails(QObject*, std::function<void(std::int64_t, const QImage&)>) {}
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
