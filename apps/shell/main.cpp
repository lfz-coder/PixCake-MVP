#include "pixcake/ui/shell/shell_display.hpp"
#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWidget>

namespace {
using pixcake::ResultCode;
using pixcake::ui::shell::Page;
using pixcake::ui::shell::Region;
using pixcake::ui::shell::RegionEntry;
using pixcake::ui::shell::ShellDisplay;

Page parse_page(const QString& value) {
  if (value == QStringLiteral("home")) return Page::Home;
  if (value == QStringLiteral("library")) return Page::Library;
  if (value == QStringLiteral("refine")) return Page::Refine;
  if (value == QStringLiteral("creative")) return Page::Creative;
  return Page::Login;
}
QString qlabel(const RegionEntry& entry) { return QString::fromStdString(entry.label); }

class ShellWindow final : public QMainWindow {
 public:
  explicit ShellWindow(Page initial)
      : display_(initial == Page::Login ? ShellDisplay::logged_out()
                                        : (initial == Page::Home ? ShellDisplay::home() : ShellDisplay::entered_project())) {
    setWindowTitle(QStringLiteral("像素蛋糕"));
    setMinimumSize(1024, 680);
    resize(1386, 800);
    build();
    if (initial != Page::Login && initial != Page::Refine) {
      display_.navigate(initial);
      render();
    }
  }
 private:
  ShellDisplay display_;
  QWidget* body_ = nullptr;
  QLabel* title_ = nullptr;
  static QLabel* text(const QString& value, bool muted = false) {
    auto* label = new QLabel(value);
    label->setWordWrap(true);
    if (muted) label->setProperty("muted", true);
    return label;
  }
  static void clear_layout(QLayout* layout) {
    while (auto* item = layout->takeAt(0)) { if (item->widget()) delete item->widget(); delete item; }
  }
  void build() {
    auto* root = new QWidget(this);
    auto* layout = new QVBoxLayout(root);
    layout->setContentsMargins(24, 18, 24, 18);
    title_ = text(QStringLiteral("登录页面"));
    title_->setObjectName(QStringLiteral("page-title"));
    QFont font = title_->font(); font.setPointSize(20); font.setBold(true); title_->setFont(font);
    layout->addWidget(title_);
    body_ = new QWidget(root); layout->addWidget(body_, 1); setCentralWidget(root); render();
  }
  QPushButton* action(const QString& label, QWidget* parent, bool enabled = false) {
    auto* button = new QPushButton(label, parent); button->setEnabled(enabled); return button;
  }
  void add_nav_button(QLayout* layout, const QString& label, Page target, bool enabled = true) {
    auto* button = new QPushButton(label); button->setObjectName(QStringLiteral("navigate-%1").arg(label)); button->setEnabled(enabled);
    connect(button, &QPushButton::clicked, this, [this, target] { if (display_.navigate(target) == ResultCode::Ok) render(); });
    layout->addWidget(button);
  }
  QWidget* region_box(const Region& region, bool compact = false) {
    auto* box = new QGroupBox(QString::fromStdString(region.label)); box->setObjectName(QString::fromStdString(region.id));
    box->setAccessibleName(QString::fromStdString(region.label));
    auto* layout = new QVBoxLayout(box); layout->setContentsMargins(12, compact ? 8 : 12, 12, compact ? 8 : 12);
    if (region.entries.empty()) { auto* empty = text(QStringLiteral("暂无内容"), true); empty->setObjectName(QStringLiteral("empty-state")); layout->addWidget(empty); }
    else for (const auto& entry : region.entries) layout->addWidget(action(qlabel(entry), box));
    return box;
  }
  void render_login() {
    auto* page = new QWidget; auto* outer = new QVBoxLayout(page); outer->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
    auto* card = new QGroupBox(QStringLiteral("登录")); card->setMinimumWidth(420); auto* form = new QFormLayout(card);
    auto* account = new QLineEdit(card); account->setObjectName(QStringLiteral("login-account-input")); account->setAccessibleName(QStringLiteral("账号输入框")); account->setPlaceholderText(QStringLiteral("账号"));
    auto* code = new QLineEdit(card); code->setObjectName(QStringLiteral("login-code-input")); code->setAccessibleName(QStringLiteral("验证码输入框")); code->setMaxLength(6); code->setPlaceholderText(QStringLiteral("6 位验证码"));
    auto* code_row = new QWidget(card); auto* code_layout = new QHBoxLayout(code_row); code_layout->setContentsMargins(0,0,0,0); code_layout->addWidget(code,1); code_layout->addWidget(action(QStringLiteral("获取验证码"), code_row));
    form->addRow(QStringLiteral("账号"), account); form->addRow(QStringLiteral("验证码"), code_row);
    auto* methods = new QHBoxLayout; for (const auto& label : {QStringLiteral("手机验证码登录"), QStringLiteral("微信登录"), QStringLiteral("商家账号登录")}) methods->addWidget(action(label, card));
    form->addRow(QStringLiteral("登录方式"), methods); form->addRow(QString(), action(QStringLiteral("登录"), card)); outer->addWidget(card);
    auto* help = new QHBoxLayout; help->addWidget(action(QStringLiteral("联系售后"), page)); help->addWidget(action(QStringLiteral("查看快捷键"), page)); outer->addLayout(help); set_body(page);
  }
  void render_home() {
    auto* page = new QWidget; page->setObjectName(QStringLiteral("home-page")); auto* outer = new QHBoxLayout(page);
    auto* nav = new QGroupBox(QStringLiteral("导航栏")); auto* nav_layout = new QVBoxLayout(nav);
    for (const auto& label : {QStringLiteral("主页"), QStringLiteral("AI 工具箱"), QStringLiteral("本地项目"), QStringLiteral("工作流"), QStringLiteral("云空间"), QStringLiteral("选片交付"), QStringLiteral("回收站")}) nav_layout->addWidget(action(label, nav));
    nav_layout->addStretch(); outer->addWidget(nav);
    auto* content = new QWidget; auto* main = new QVBoxLayout(content); auto* top = new QHBoxLayout; auto* search = new QLineEdit(content); search->setPlaceholderText(QStringLiteral("搜索")); top->addWidget(search,1);
    for (const auto& label : {QStringLiteral("购买套餐"), QStringLiteral("个人中心"), QStringLiteral("更多")}) top->addWidget(action(label, content)); main->addLayout(top);
    auto* import = new QGroupBox(QStringLiteral("导入照片创建新项目")); auto* import_layout = new QVBoxLayout(import); import_layout->addWidget(text(QStringLiteral("拖入照片或选择文件后创建新项目"), true)); import_layout->addWidget(action(QStringLiteral("导入照片"), import)); main->addWidget(import);
    auto* common = new QHBoxLayout; common->addWidget(region_box(display_.regions()[4], true)); common->addWidget(region_box(display_.regions()[5], true)); main->addLayout(common); main->addWidget(region_box(display_.regions()[6])); outer->addWidget(content,1); set_body(page);
  }
  void render_workspace() {
    auto* page = new QWidget; page->setObjectName(QStringLiteral("refine-page")); auto* outer = new QVBoxLayout(page); auto* top = new QHBoxLayout;
    auto* back = action(QStringLiteral("返回主页面"), page); connect(back, &QPushButton::clicked, this, [this] { if (display_.navigate(Page::Home) == ResultCode::Ok) render(); }); top->addWidget(back); top->addWidget(action(QStringLiteral("搜索"), page)); top->addWidget(action(QStringLiteral("像素助手"), page)); top->addStretch(); top->addWidget(action(QStringLiteral("技术支持"), page)); top->addWidget(action(QStringLiteral("个人中心"), page)); top->addWidget(action(QStringLiteral("导出"), page)); outer->addLayout(top);
    auto* tabs = new QHBoxLayout; add_nav_button(tabs, QStringLiteral("图库"), Page::Library); add_nav_button(tabs, QStringLiteral("精修"), Page::Refine); add_nav_button(tabs, QStringLiteral("创意"), Page::Creative); outer->addLayout(tabs);
    auto* center = new QHBoxLayout; auto* toolbar = region_box(display_.regions()[3], true); toolbar->setMaximumWidth(210); center->addWidget(toolbar);
    auto* canvas = new QGroupBox(QStringLiteral("画布")); auto* canvas_layout = new QVBoxLayout(canvas); auto* preview = text(QStringLiteral("预览区域"), true); preview->setAlignment(Qt::AlignCenter); canvas_layout->addWidget(preview,1); canvas_layout->addWidget(action(QStringLiteral("查看对比"), canvas)); center->addWidget(canvas,1);
    auto* modules = new QGroupBox(QStringLiteral("功能区")); modules->setMinimumWidth(220); auto* modules_layout = new QVBoxLayout(modules); modules_layout->addWidget(text(QStringLiteral("暂无已加载照片"), true)); center->addWidget(modules); outer->addLayout(center,1); outer->addWidget(region_box(display_.regions()[5], true)); set_body(page);
  }
  void render_library() {
    auto* page = new QWidget; page->setObjectName(QStringLiteral("library-page")); auto* outer = new QVBoxLayout(page); auto* tabs = new QHBoxLayout; add_nav_button(tabs, QStringLiteral("图库"), Page::Library); add_nav_button(tabs, QStringLiteral("精修"), Page::Refine); add_nav_button(tabs, QStringLiteral("创意"), Page::Creative); outer->addLayout(tabs);
    auto* content = new QHBoxLayout; auto* left = new QVBoxLayout; left->addWidget(region_box(display_.regions()[1],true)); left->addWidget(region_box(display_.regions()[2],true)); left->addWidget(region_box(display_.regions()[3],true)); content->addLayout(left);
    auto* grid = new QGroupBox(QStringLiteral("网格视图")); auto* grid_layout = new QVBoxLayout(grid); grid_layout->addWidget(text(QStringLiteral("暂无照片"),true),1); grid_layout->addWidget(region_box(display_.regions()[6],true)); content->addWidget(grid,1); content->addWidget(region_box(display_.regions()[5],true)); outer->addLayout(content,1); outer->addWidget(region_box(display_.regions()[7],true)); set_body(page);
  }
  void render_creative() {
    auto* page = new QWidget; page->setObjectName(QStringLiteral("creative-page")); auto* outer = new QVBoxLayout(page); auto* top = new QHBoxLayout; add_nav_button(top, QStringLiteral("精修"), Page::Refine); add_nav_button(top, QStringLiteral("创意"), Page::Creative); top->addStretch(); outer->addLayout(top);
    auto* center = new QHBoxLayout; center->addWidget(region_box(display_.regions()[0],true)); auto* canvas = new QGroupBox(QStringLiteral("画布")); auto* canvas_layout = new QVBoxLayout(canvas); canvas_layout->addWidget(text(QStringLiteral("预览区域"),true),1); center->addWidget(canvas,1); center->addWidget(region_box(display_.regions()[4],true)); center->addWidget(region_box(display_.regions()[5],true)); outer->addLayout(center,1); outer->addWidget(region_box(display_.regions()[2],true)); set_body(page);
  }
  void set_body(QWidget* page) {
    auto* layout = body_->layout();
    if (layout == nullptr) {
      layout = new QVBoxLayout(body_);
      layout->setContentsMargins(0, 0, 0, 0);
    } else {
      clear_layout(layout);
    }
    layout->addWidget(page);
  }
  void render() { title_->setText(QString::fromUtf8(pixcake::ui::shell::page_label(display_.page()))); switch (display_.page()) { case Page::Login: render_login(); break; case Page::Home: render_home(); break; case Page::Library: render_library(); break; case Page::Refine: render_workspace(); break; case Page::Creative: render_creative(); break; } }
};
}
int main(int argc, char** argv) {
  QApplication app(argc, argv); QCoreApplication::setApplicationName(QStringLiteral("PixCake")); QCoreApplication::setApplicationVersion(QStringLiteral("0.1")); QCommandLineParser parser; parser.setApplicationDescription(QStringLiteral("PixCake desktop shell")); parser.addHelpOption(); QCommandLineOption page_option(QStringList() << QStringLiteral("page"), QStringLiteral("Display a read-only shell page: login, home, library, refine, creative."), QStringLiteral("page"), QStringLiteral("login")); QCommandLineOption smoke_option(QStringList() << QStringLiteral("smoke"), QStringLiteral("Construct the selected page and exit without showing a window.")); parser.addOption(page_option); parser.addOption(smoke_option); parser.process(app); ShellWindow window(parse_page(parser.value(page_option))); if (parser.isSet(smoke_option)) return 0; window.show(); return app.exec();
}
