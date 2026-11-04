#include "pixcake/ui/shell/shell_display.hpp"
#include "pixcake_theme.hpp"
#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QFileInfo>
#include <QPixmap>
#include <QCheckBox>
#include <QFrame>
#include <QMainWindow>
#include <QPushButton>
#include <QStringList>
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
    root->setObjectName(QStringLiteral("shell-root"));
    auto* layout = new QVBoxLayout(root);
    layout->setContentsMargins(24, 18, 24, 18);
    title_ = text(QStringLiteral("登录页面"));
    title_->setObjectName(QStringLiteral("page-title"));
    QFont font = title_->font(); font.setPointSize(20); font.setBold(true); title_->setFont(font);
    title_->setVisible(false);
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
  QFrame* workspace_tool(const QString& label, QWidget* parent) {
    auto* frame = new QFrame(parent); frame->setObjectName(QStringLiteral("workspace-tool-button"));
    auto* layout = new QVBoxLayout(frame); layout->setContentsMargins(5, 6, 5, 6); layout->setSpacing(4);
    auto* glyph = text(QStringLiteral("◇")); glyph->setAlignment(Qt::AlignCenter); layout->addWidget(glyph);
    auto* name = text(label); name->setObjectName(QStringLiteral("workspace-tool-label")); name->setAlignment(Qt::AlignCenter); name->setWordWrap(true); layout->addWidget(name);
    return frame;
  }
  QFrame* thumbnail_strip(QWidget* parent, bool library = false) {
    auto* frame = new QFrame(parent); frame->setObjectName(QStringLiteral("workspace-thumbs")); auto* layout = new QHBoxLayout(frame); layout->setContentsMargins(10, 8, 10, 8); layout->setSpacing(8);
    auto* add = action(QStringLiteral("＋"), frame); add->setObjectName(QStringLiteral("icon-button")); add->setAccessibleName(QStringLiteral("添加图片")); layout->addWidget(add);
    const QStringList labels = library ? QStringList{QStringLiteral("筛选"), QStringLiteral("排序"), QStringLiteral("去挑图"), QStringLiteral("已选 0 张")} : QStringList{QStringLiteral("筛选"), QStringLiteral("排序"), QStringLiteral("去挑图"), QStringLiteral("星标"), QStringLiteral("旗标"), QStringLiteral("色标")};
    for (const auto& label : labels) { auto* b = action(label, frame); b->setEnabled(false); layout->addWidget(b); }
    layout->addStretch(); auto* empty = text(library ? QStringLiteral("暂无照片") : QStringLiteral("暂无缩略图"), true); layout->addWidget(empty); return frame;
  }
  void render_login() {
    auto* page = new QWidget; page->setObjectName(QStringLiteral("login-page"));
    auto* outer = new QHBoxLayout(page); outer->setContentsMargins(0, 0, 0, 0); outer->setSpacing(0);
    auto* hero = new QLabel(page); hero->setObjectName(QStringLiteral("login-hero")); hero->setAlignment(Qt::AlignCenter);
    const auto hero_path = QCoreApplication::applicationDirPath() + QStringLiteral("/login-hero.png");
    QPixmap hero_pix(hero_path); if (!hero_pix.isNull()) hero->setPixmap(hero_pix.scaled(430, 620, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
    hero->setMinimumWidth(430); hero->setMaximumWidth(560); outer->addWidget(hero, 1);
    auto* right = new QWidget(page); right->setMinimumWidth(480); auto* panel = new QVBoxLayout(right); panel->setContentsMargins(70, 80, 70, 70); panel->setSpacing(16);
    auto* heading = text(QStringLiteral("手机验证码登录")); heading->setAlignment(Qt::AlignCenter); heading->setObjectName(QStringLiteral("login-heading")); QFont hf = heading->font(); hf.setPointSize(20); heading->setFont(hf); panel->addWidget(heading);
    auto* account = new QLineEdit(right); account->setObjectName(QStringLiteral("login-account-input")); account->setAccessibleName(QStringLiteral("账号输入框")); account->setPlaceholderText(QStringLiteral("输入手机号")); panel->addWidget(account);
    auto* code = new QLineEdit(right); code->setObjectName(QStringLiteral("login-code-input")); code->setAccessibleName(QStringLiteral("验证码输入框")); code->setMaxLength(6); code->setPlaceholderText(QStringLiteral("输入短信中的验证码"));
    auto* code_row = new QWidget(right); auto* code_layout = new QHBoxLayout(code_row); code_layout->setContentsMargins(0,0,0,0); code_layout->addWidget(code,1); auto* get_code = action(QStringLiteral("获取验证码"), code_row); get_code->setObjectName(QStringLiteral("get-code")); code_layout->addWidget(get_code); panel->addWidget(code_row);
    auto* login = action(QStringLiteral("登录"), right); login->setObjectName(QStringLiteral("primary")); login->setEnabled(false); panel->addWidget(login);
    auto* agreement = new QCheckBox(QStringLiteral("已阅读并同意像素蛋糕《用户使用协议》《隐私政策》"), right); agreement->setObjectName(QStringLiteral("login-agreement")); panel->addWidget(agreement);
    panel->addSpacing(70);
    auto* divider = new QLabel(QStringLiteral("──────────────  or  ──────────────"), right); divider->setAlignment(Qt::AlignCenter); divider->setProperty("muted", true); panel->addWidget(divider);
    auto* methods = new QHBoxLayout; auto* wechat = action(QStringLiteral("◉  微信登录"), right); wechat->setObjectName(QStringLiteral("wechat-login")); auto* merchant = action(QStringLiteral("▰  商家账号"), right); merchant->setObjectName(QStringLiteral("merchant-login")); methods->addStretch(); methods->addWidget(wechat); methods->addWidget(merchant); methods->addStretch(); panel->addLayout(methods); panel->addStretch();
    auto* help = action(QStringLiteral("?"), page); help->setObjectName(QStringLiteral("icon-button")); help->setAccessibleName(QStringLiteral("帮助")); help->setToolTip(QStringLiteral("联系售后 / 查看快捷键")); auto* help_row = new QHBoxLayout; help_row->addStretch(); help_row->addWidget(help); panel->addLayout(help_row); outer->addWidget(right, 1); set_body(page);
  }
  void render_home() {
    auto* page = new QWidget; page->setObjectName(QStringLiteral("home-page"));
    auto* outer = new QHBoxLayout(page); outer->setContentsMargins(0, 0, 0, 0); outer->setSpacing(0);

    auto* nav = new QFrame(page); nav->setObjectName(QStringLiteral("home-sidebar")); nav->setFixedWidth(220);
    auto* nav_layout = new QVBoxLayout(nav); nav_layout->setContentsMargins(18, 20, 18, 18); nav_layout->setSpacing(6);
    auto* brand = text(QStringLiteral("▰  像素蛋糕")); brand->setObjectName(QStringLiteral("home-brand")); nav_layout->addWidget(brand); nav_layout->addSpacing(18);
    const auto nav_items = {QStringLiteral("主页"), QStringLiteral("AI 工具箱"), QStringLiteral("本地项目"), QStringLiteral("云空间"), QStringLiteral("工作流"), QStringLiteral("选片交付"), QStringLiteral("回收站")};
    for (const auto& label : nav_items) { auto* b = action(label, nav); b->setObjectName(QStringLiteral("home-nav")); b->setProperty("selected", label == QStringLiteral("主页")); b->setEnabled(label == QStringLiteral("主页")); nav_layout->addWidget(b); }
    nav_layout->addStretch();
    auto* support = action(QStringLiteral("了解更多版本功能"), nav); support->setEnabled(false); nav_layout->addWidget(support);
    outer->addWidget(nav);

    auto* content = new QFrame(page); content->setObjectName(QStringLiteral("home-content")); auto* main = new QVBoxLayout(content); main->setContentsMargins(24, 18, 24, 18); main->setSpacing(16);
    auto* top = new QHBoxLayout; auto* search = new QLineEdit(content); search->setObjectName(QStringLiteral("home-search")); search->setPlaceholderText(QStringLiteral("⌕  搜索所有项目")); search->setMaximumWidth(420); top->addStretch(); top->addWidget(search); top->addStretch();
    for (const auto& label : {QStringLiteral("会员续费"), QStringLiteral("技术支持"), QStringLiteral("个人中心"), QStringLiteral("消息中心"), QStringLiteral("传输列表"), QStringLiteral("导出列表")}) { auto* b = action(label, content); b->setObjectName(QStringLiteral("home-toolbar")); b->setToolTip(label); b->setEnabled(false); top->addWidget(b); } main->addLayout(top);
    auto* page_title = text(QStringLiteral("主页")); page_title->setObjectName(QStringLiteral("home-section-title")); main->addWidget(page_title);

    auto* import = new QFrame(content); import->setObjectName(QStringLiteral("home-import")); auto* import_layout = new QVBoxLayout(import); import_layout->setContentsMargins(24, 20, 24, 20); import_layout->setAlignment(Qt::AlignCenter);
    auto* plus = text(QStringLiteral("＋")); plus->setAlignment(Qt::AlignCenter); QFont pf = plus->font(); pf.setPointSize(30); plus->setFont(pf); import_layout->addWidget(plus);
    auto* import_title = text(QStringLiteral("照片导入")); import_title->setAlignment(Qt::AlignCenter); import_layout->addWidget(import_title); import_layout->addWidget(text(QStringLiteral("拖动或点击导入图片"), true), 0, Qt::AlignCenter); auto* import_button = action(QStringLiteral("导入照片"), import); import_button->setEnabled(false); import_button->setObjectName(QStringLiteral("primary")); import_button->setMaximumWidth(120); import_layout->addWidget(import_button, 0, Qt::AlignCenter); main->addWidget(import);

    auto* common_title = text(QStringLiteral("常用功能")); common_title->setObjectName(QStringLiteral("home-section-title")); main->addWidget(common_title);
    auto* common = new QHBoxLayout; common->setSpacing(12); for (const auto& label : {QStringLiteral("联机拍摄"), QStringLiteral("AI工具箱"), QStringLiteral("像素助手")}) { auto* card = new QFrame(content); card->setObjectName(QStringLiteral("home-card")); card->setMinimumHeight(116); auto* l = new QVBoxLayout(card); l->setAlignment(Qt::AlignCenter); l->addWidget(text(QStringLiteral("◇")), 0, Qt::AlignCenter); auto* t = text(label); t->setObjectName(QStringLiteral("home-card-title")); t->setAlignment(Qt::AlignCenter); l->addWidget(t); l->addWidget(text(QStringLiteral("暂无内容"), true), 0, Qt::AlignCenter); common->addWidget(card, 1); } main->addLayout(common);

    auto* quick_title = text(QStringLiteral("快捷功能")); quick_title->setObjectName(QStringLiteral("home-section-title")); main->addWidget(quick_title);
    auto* quick = new QFrame(content); quick->setObjectName(QStringLiteral("home-card")); auto* quick_layout = new QHBoxLayout(quick); quick_layout->setContentsMargins(16, 12, 16, 12); quick_layout->addWidget(text(QStringLiteral("暂无快捷功能"), true)); quick_layout->addStretch(); main->addWidget(quick);

    auto* projects_title = text(QStringLiteral("项目列表")); projects_title->setObjectName(QStringLiteral("home-section-title")); main->addWidget(projects_title);
    auto* projects = new QFrame(content); projects->setObjectName(QStringLiteral("home-card")); auto* projects_layout = new QVBoxLayout(projects); projects_layout->setContentsMargins(16, 20, 16, 20); projects_layout->addWidget(text(QStringLiteral("暂无项目"), true), 1, Qt::AlignCenter); main->addWidget(projects, 1);
    outer->addWidget(content, 1); set_body(page);
  }
  void render_workspace() {
    auto* page = new QWidget; page->setObjectName(QStringLiteral("refine-page")); auto* outer = new QVBoxLayout(page); outer->setContentsMargins(0, 0, 0, 0); outer->setSpacing(4);
    auto* top = new QFrame(page); top->setObjectName(QStringLiteral("workspace-top")); auto* top_layout = new QHBoxLayout(top); top_layout->setContentsMargins(8, 6, 8, 6);
    auto* back = action(QStringLiteral("⌂"), top); back->setObjectName(QStringLiteral("icon-button")); back->setAccessibleName(QStringLiteral("返回主页面")); connect(back, &QPushButton::clicked, this, [this] { if (display_.navigate(Page::Home) == ResultCode::Ok) render(); }); top_layout->addWidget(back); top_layout->addWidget(action(QStringLiteral("⌕"), top)); top_layout->addWidget(action(QStringLiteral("✧"), top)); top_layout->addStretch();
    auto* tabs = new QHBoxLayout; add_nav_button(tabs, QStringLiteral("图库"), Page::Library); add_nav_button(tabs, QStringLiteral("精修"), Page::Refine); add_nav_button(tabs, QStringLiteral("创意"), Page::Creative); top_layout->addLayout(tabs); top_layout->addStretch(); for (const auto& label : {QStringLiteral("技术支持"), QStringLiteral("个人中心"), QStringLiteral("消息中心"), QStringLiteral("传输列表"), QStringLiteral("导出列表"), QStringLiteral("导出")}) { auto* b = action(label, top); b->setEnabled(false); top_layout->addWidget(b); } outer->addWidget(top);
    auto* toolbar = new QFrame(page); toolbar->setObjectName(QStringLiteral("workspace-tools")); auto* toolbar_layout = new QHBoxLayout(toolbar); toolbar_layout->setContentsMargins(8, 5, 8, 5); toolbar_layout->setSpacing(4);
    const auto tools = {QStringLiteral("缩放"), QStringLiteral("合适"), QStringLiteral("抓手"), QStringLiteral("祛边"), QStringLiteral("参考线"), QStringLiteral("液化"), QStringLiteral("污点修复"), QStringLiteral("修补"), QStringLiteral("仿制图章"), QStringLiteral("智能消除"), QStringLiteral("AI点修"), QStringLiteral("重置效果"), QStringLiteral("预设")}; for (const auto& label : tools) toolbar_layout->addWidget(workspace_tool(label, toolbar)); toolbar_layout->addStretch(); outer->addWidget(toolbar);
    auto* center = new QHBoxLayout; center->setSpacing(4); auto* canvas = new QFrame(page); canvas->setObjectName(QStringLiteral("workspace-canvas")); auto* canvas_layout = new QVBoxLayout(canvas); auto* preview = text(QStringLiteral("预览区域\n暂无已加载照片"), true); preview->setAlignment(Qt::AlignCenter); canvas_layout->addWidget(preview, 1); auto* compare = action(QStringLiteral("查看对比"), canvas); compare->setEnabled(false); canvas_layout->addWidget(compare, 0, Qt::AlignRight); center->addWidget(canvas, 1);
    auto* modules = new QFrame(page); modules->setObjectName(QStringLiteral("workspace-side")); modules->setMinimumWidth(250); auto* modules_layout = new QVBoxLayout(modules); modules_layout->addWidget(text(QStringLiteral("功能区"))); modules_layout->addWidget(text(QStringLiteral("暂无已加载照片"), true)); modules_layout->addStretch(); center->addWidget(modules); outer->addLayout(center, 1); outer->addWidget(thumbnail_strip(page)); set_body(page);
  }
  void render_library() {
    auto* page = new QWidget; page->setObjectName(QStringLiteral("library-page")); auto* outer = new QVBoxLayout(page); outer->setContentsMargins(0, 0, 0, 0); outer->setSpacing(4);
    auto* top = new QFrame(page); top->setObjectName(QStringLiteral("workspace-top")); auto* top_layout = new QHBoxLayout(top); top_layout->setContentsMargins(8, 6, 8, 6); auto* home = action(QStringLiteral("⌂"), top); home->setObjectName(QStringLiteral("icon-button")); home->setAccessibleName(QStringLiteral("首页")); top_layout->addWidget(home); top_layout->addWidget(action(QStringLiteral("⌕"), top)); top_layout->addStretch(); auto* tabs = new QHBoxLayout; add_nav_button(tabs, QStringLiteral("图库"), Page::Library); add_nav_button(tabs, QStringLiteral("精修"), Page::Refine); add_nav_button(tabs, QStringLiteral("创意"), Page::Creative); top_layout->addLayout(tabs); top_layout->addStretch(); for (const auto& label : {QStringLiteral("会员续费"), QStringLiteral("技术支持"), QStringLiteral("个人中心"), QStringLiteral("消息中心"), QStringLiteral("传输列表"), QStringLiteral("导出列表"), QStringLiteral("导出")}) { auto* b = action(label, top); b->setEnabled(false); top_layout->addWidget(b); } outer->addWidget(top);
    auto* controls = new QFrame(page); controls->setObjectName(QStringLiteral("workspace-tools")); auto* controls_layout = new QHBoxLayout(controls); controls_layout->setContentsMargins(8, 5, 8, 5); for (const auto& label : {QStringLiteral("显示状态"), QStringLiteral("全效果模式"), QStringLiteral("导入新图片"), QStringLiteral("筛选"), QStringLiteral("排序")}) { auto* b = action(label, controls); b->setEnabled(false); controls_layout->addWidget(b); } controls_layout->addStretch(); auto* selected = text(QStringLiteral("已选 0 张（共 0 张）"), true); controls_layout->addWidget(selected); auto* del = action(QStringLiteral("批量删除"), controls); del->setEnabled(false); controls_layout->addWidget(del); outer->addWidget(controls);
    auto* content = new QHBoxLayout; content->setSpacing(4); auto* grid = new QFrame(page); grid->setObjectName(QStringLiteral("library-grid")); auto* grid_layout = new QVBoxLayout(grid); grid_layout->setContentsMargins(16, 16, 16, 16); auto* grid_empty = text(QStringLiteral("暂无照片\n导入图片后将在此显示"), true); grid_empty->setAlignment(Qt::AlignCenter); grid_layout->addWidget(grid_empty, 1); content->addWidget(grid, 1);
    auto* side = new QFrame(page); side->setObjectName(QStringLiteral("workspace-side")); side->setMinimumWidth(260); auto* side_layout = new QVBoxLayout(side); side_layout->addWidget(text(QStringLiteral("侧边栏"))); for (const auto& label : {QStringLiteral("AI挑图"), QStringLiteral("预设"), QStringLiteral("元数据")}) { auto* section = new QGroupBox(label, side); auto* l = new QVBoxLayout(section); l->addWidget(text(QStringLiteral("暂无内容"), true)); side_layout->addWidget(section); } side_layout->addStretch(); content->addWidget(side); outer->addLayout(content, 1);
    outer->addWidget(thumbnail_strip(page, true)); set_body(page);
  }
  void render_creative() {
    auto* page = new QWidget; page->setObjectName(QStringLiteral("creative-page")); auto* outer = new QVBoxLayout(page); outer->setContentsMargins(0, 0, 0, 0); outer->setSpacing(4);
    auto* top = new QFrame(page); top->setObjectName(QStringLiteral("workspace-top")); auto* top_layout = new QHBoxLayout(top); top_layout->setContentsMargins(8, 6, 8, 6); auto* home = action(QStringLiteral("⌂"), top); home->setObjectName(QStringLiteral("icon-button")); top_layout->addWidget(home); top_layout->addStretch(); auto* tabs = new QHBoxLayout; add_nav_button(tabs, QStringLiteral("图库"), Page::Library); add_nav_button(tabs, QStringLiteral("精修"), Page::Refine); add_nav_button(tabs, QStringLiteral("创意"), Page::Creative); top_layout->addLayout(tabs); top_layout->addStretch(); for (const auto& label : {QStringLiteral("AI工具"), QStringLiteral("历史"), QStringLiteral("批量"), QStringLiteral("帮助"), QStringLiteral("导出")}) { auto* b = action(label, top); b->setEnabled(false); top_layout->addWidget(b); } outer->addWidget(top);
    auto* center = new QHBoxLayout; center->setSpacing(4); auto* tool_panel = new QFrame(page); tool_panel->setObjectName(QStringLiteral("workspace-tools")); tool_panel->setMaximumWidth(150); auto* tool_layout = new QVBoxLayout(tool_panel); for (const auto& label : {QStringLiteral("缩放"), QStringLiteral("屏幕模式"), QStringLiteral("抓手"), QStringLiteral("移动图层"), QStringLiteral("缩放工具"), QStringLiteral("液化"), QStringLiteral("污点修复"), QStringLiteral("修补"), QStringLiteral("仿制图章"), QStringLiteral("矩形选区"), QStringLiteral("套索工具"), QStringLiteral("裁切")}) tool_layout->addWidget(workspace_tool(label, tool_panel)); tool_layout->addStretch(); center->addWidget(tool_panel);
    auto* canvas = new QFrame(page); canvas->setObjectName(QStringLiteral("creative-canvas")); auto* canvas_layout = new QVBoxLayout(canvas); auto* preview = text(QStringLiteral("画布\n暂无图片或图层"), true); preview->setAlignment(Qt::AlignCenter); canvas_layout->addWidget(preview, 1); center->addWidget(canvas, 1);
    auto* side = new QFrame(page); side->setObjectName(QStringLiteral("workspace-side")); side->setMinimumWidth(250); auto* side_layout = new QVBoxLayout(side); for (const auto& title : {QStringLiteral("AI工具箱"), QStringLiteral("侧边栏"), QStringLiteral("图层面板")}) { auto* group = new QGroupBox(title, side); auto* l = new QVBoxLayout(group); l->addWidget(text(QStringLiteral("暂无内容"), true)); side_layout->addWidget(group); } side_layout->addStretch(); center->addWidget(side); outer->addLayout(center, 1); outer->addWidget(thumbnail_strip(page)); set_body(page);
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
  QApplication app(argc, argv); pixcake::shell::PixCakeTheme::apply(app); QCoreApplication::setApplicationName(QStringLiteral("PixCake")); QCoreApplication::setApplicationVersion(QStringLiteral("0.1")); QCommandLineParser parser; parser.setApplicationDescription(QStringLiteral("PixCake desktop shell")); parser.addHelpOption(); QString default_page = QStringLiteral("login"); const auto executable = QFileInfo(QString::fromLocal8Bit(argv[0])).baseName(); for (const auto& page : {QStringLiteral("home"), QStringLiteral("library"), QStringLiteral("refine"), QStringLiteral("creative")}) if (executable.endsWith(QStringLiteral("-") + page)) default_page = page; QCommandLineOption page_option(QStringList() << QStringLiteral("page"), QStringLiteral("Display a read-only shell page: login, home, library, refine, creative."), QStringLiteral("page"), default_page); QCommandLineOption smoke_option(QStringList() << QStringLiteral("smoke"), QStringLiteral("Construct the selected page and exit without showing a window.")); parser.addOption(page_option); parser.addOption(smoke_option); parser.process(app); ShellWindow window(parse_page(parser.value(page_option))); if (parser.isSet(smoke_option)) return 0; window.show(); return app.exec();
}
