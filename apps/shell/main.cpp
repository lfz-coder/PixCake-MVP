#include "pixcake/ui/shell/shell_display.hpp"
#include "pixcake_theme.hpp"
#include "pixcake_widgets.hpp"
#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QGroupBox>
#include <QHash>
#include <QHBoxLayout>
#include <QLabel>
#include <QFileInfo>
#include <QPixmap>
#include <QCheckBox>
#include <QFrame>
#include <QMainWindow>
#include <QPushButton>
#include <QSlider>
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
    layout->setContentsMargins(0, 0, 0, 0);
    title_ = text(QStringLiteral("登录页面"));
    title_->setObjectName(QStringLiteral("page-title"));
    QFont font = title_->font(); font.setPointSize(20); font.setBold(true); title_->setFont(font);
    title_->setVisible(false);
    body_ = new QWidget(root); layout->addWidget(body_, 1); setCentralWidget(root); render();
  }
  QPushButton* action(const QString& label, QWidget* parent, bool enabled = false) {
    auto* button = new pixcake::shell::PixCakeButton(label, parent);
    button->setEnabled(enabled);
    return button;
  }
  QPushButton* icon_action(const QString& label, QWidget* parent, bool enabled = false, int size = 18) {
    auto* button = action(label, parent, enabled);
    const auto path = pixcake::shell::globalAssetPath(label);
    if (!path.isEmpty()) pixcake::shell::applyAssetIcon(button, path, size);
    return button;
  }
  void add_nav_button(QLayout* layout, const QString& label, Page target, bool enabled = true) {
    auto* button = new pixcake::shell::PixCakeButton(label);
    button->setObjectName(QStringLiteral("navigate-%1").arg(label));
    button->setProperty("selected", display_.page() == target);
    button->setProperty("page-tab", true);
    button->setEnabled(enabled);
    const auto tab_asset = pixcake::shell::globalAssetPath(label);
    if (!tab_asset.isEmpty()) pixcake::shell::applyAssetIcon(button, tab_asset, 18);
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
    frame->setAccessibleName(label);
    auto* layout = new QVBoxLayout(frame); layout->setContentsMargins(5, 6, 5, 6); layout->setSpacing(4);
    const auto asset = pixcake::shell::toolAssetPath(label);
    auto* glyph = asset.isEmpty()
                      ? pixcake::shell::pendingIcon(frame, 20, label + QStringLiteral("图标待补充"))
                      : pixcake::shell::assetIcon(asset, 20, label + QStringLiteral("图标"), frame);
    layout->addWidget(glyph, 0, Qt::AlignCenter);
    auto* name = text(label); name->setObjectName(QStringLiteral("workspace-tool-label")); name->setAlignment(Qt::AlignCenter); name->setWordWrap(true); layout->addWidget(name);
    return frame;
  }
  QFrame* thumbnail_strip(QWidget* parent, bool library = false) {
    auto* frame = new QFrame(parent); frame->setObjectName(QStringLiteral("workspace-thumbs")); auto* layout = new QHBoxLayout(frame); layout->setContentsMargins(10, 8, 10, 8); layout->setSpacing(8);
    auto* add = action(QStringLiteral("+"), frame); add->setObjectName(QStringLiteral("icon-button")); add->setAccessibleName(QStringLiteral("添加图片")); layout->addWidget(add);
    const QStringList labels = library ? QStringList{QStringLiteral("筛选"), QStringLiteral("排序"), QStringLiteral("去挑图"), QStringLiteral("已选 0 张")} : QStringList{QStringLiteral("筛选"), QStringLiteral("排序"), QStringLiteral("去挑图"), QStringLiteral("星标"), QStringLiteral("旗标"), QStringLiteral("色标"), QStringLiteral("单视图"), QStringLiteral("对比视图"), QStringLiteral("参考视图")};
    for (const auto& label : labels) { auto* b = icon_action(label, frame); b->setEnabled(false); layout->addWidget(b); }
    layout->addStretch(); auto* empty = text(library ? QStringLiteral("暂无照片") : QStringLiteral("暂无缩略图"), true); layout->addWidget(empty); return frame;
  }
  void render_login() {
    auto* page = new QWidget; page->setObjectName(QStringLiteral("login-page"));
    auto* outer = new QHBoxLayout(page); outer->setContentsMargins(32, 32, 32, 26); outer->setSpacing(28);
    auto* hero = new QLabel(page); hero->setObjectName(QStringLiteral("login-hero")); hero->setAlignment(Qt::AlignCenter);
    const auto app_dir = QCoreApplication::applicationDirPath();
    const QStringList hero_paths = {
        app_dir + QStringLiteral("/../Resources/login-hero.png"),
        app_dir + QStringLiteral("/login-hero.png"),
        app_dir + QStringLiteral("/../../login-hero.png")};
    QPixmap hero_pix;
    for (const auto& path : hero_paths) {
      hero_pix.load(path);
      if (!hero_pix.isNull()) break;
    }
    if (!hero_pix.isNull()) hero->setPixmap(hero_pix.scaled(620, 760, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
    hero->setMinimumWidth(470); hero->setMaximumWidth(690); outer->addWidget(hero, 11);
    auto* right = new QWidget(page); right->setObjectName(QStringLiteral("login-panel")); right->setMinimumWidth(520); auto* panel = new QVBoxLayout(right); panel->setContentsMargins(84, 0, 84, 0); panel->setSpacing(14);
    panel->addStretch(1);
    auto* heading = text(QStringLiteral("手机验证码登录")); heading->setAlignment(Qt::AlignCenter); heading->setObjectName(QStringLiteral("login-heading")); QFont hf = heading->font(); hf.setPointSize(21); heading->setFont(hf); panel->addWidget(heading);
    auto* account = new pixcake::shell::PixCakeField(QStringLiteral("账号输入框"), right); account->setObjectName(QStringLiteral("login-account-input")); account->setPlaceholderText(QStringLiteral("输入手机号")); panel->addWidget(account);
    auto* code = new pixcake::shell::PixCakeField(QStringLiteral("验证码输入框"), right); code->setObjectName(QStringLiteral("login-code-input")); code->setMaxLength(6); code->setPlaceholderText(QStringLiteral("输入短信中的验证码"));
    auto* code_row = new QWidget(right); auto* code_layout = new QHBoxLayout(code_row); code_layout->setContentsMargins(0,0,0,0); code_layout->addWidget(code,1); auto* get_code = action(QStringLiteral("获取验证码"), code_row); get_code->setObjectName(QStringLiteral("get-code")); code_layout->addWidget(get_code); panel->addWidget(code_row);
    auto* login = action(QStringLiteral("登录"), right); login->setObjectName(QStringLiteral("primary")); login->setEnabled(false); panel->addWidget(login);
    auto* agreement = new QCheckBox(QStringLiteral("已阅读并同意像素蛋糕《用户使用协议》《隐私政策》"), right); agreement->setObjectName(QStringLiteral("login-agreement")); panel->addWidget(agreement);
    panel->addSpacing(50);
    auto* divider_row = new QHBoxLayout;
    divider_row->setSpacing(12);
    divider_row->addWidget(pixcake::shell::horizontalRule(right), 1);
    auto* or_label = text(QStringLiteral("or"), true); or_label->setAlignment(Qt::AlignCenter); divider_row->addWidget(or_label);
    divider_row->addWidget(pixcake::shell::horizontalRule(right), 1); panel->addLayout(divider_row);
    auto* methods = new QHBoxLayout; methods->setSpacing(10); auto* wechat = action(QStringLiteral("微信登录"), right); wechat->setObjectName(QStringLiteral("login-wechat")); wechat->setProperty("variant", QStringLiteral("pill")); wechat->setEnabled(false); pixcake::shell::applyAssetIcon(wechat, QStringLiteral("wechat.svg"), 20); auto* merchant = action(QStringLiteral("商家账号"), right); merchant->setObjectName(QStringLiteral("login-merchant")); merchant->setProperty("variant", QStringLiteral("pill")); merchant->setEnabled(false); pixcake::shell::applyAssetIcon(merchant, QStringLiteral("merchant.svg"), 20); methods->addStretch(); methods->addWidget(wechat); methods->addWidget(merchant); methods->addStretch(); panel->addLayout(methods); panel->addStretch(1);
    auto* help = action(QStringLiteral("?"), right); help->setObjectName(QStringLiteral("login-help")); help->setAccessibleName(QStringLiteral("帮助")); help->setToolTip(QStringLiteral("联系售后 / 查看快捷键")); pixcake::shell::applyAssetIcon(help, QStringLiteral("help.svg"), 16); if (!help->icon().isNull()) help->setText(QString()); auto* help_row = new QHBoxLayout; help_row->addStretch(); help_row->addWidget(help); help_row->setContentsMargins(0, 0, 8, 16); panel->addLayout(help_row); outer->addWidget(right, 13); set_body(page);
  }
  void render_home() {
    auto* page = new QWidget; page->setObjectName(QStringLiteral("home-page"));
    auto* outer = new QHBoxLayout(page); outer->setContentsMargins(0, 0, 0, 0); outer->setSpacing(0);

    auto* nav = new QFrame(page); nav->setObjectName(QStringLiteral("home-sidebar")); nav->setFixedWidth(220);
    auto* nav_layout = new QVBoxLayout(nav); nav_layout->setContentsMargins(18, 20, 18, 18); nav_layout->setSpacing(6);
    auto* brand_row = new QWidget(nav); auto* brand_layout = new QHBoxLayout(brand_row); brand_layout->setContentsMargins(0, 0, 0, 0); brand_layout->setSpacing(8);
    brand_layout->addWidget(pixcake::shell::assetIcon(QStringLiteral("pixcake-logo.svg"), 24, QStringLiteral("像素蛋糕"), brand_row));
    auto* brand = text(QStringLiteral("像素蛋糕")); brand->setObjectName(QStringLiteral("home-brand")); brand_layout->addWidget(brand); brand_layout->addStretch(); nav_layout->addWidget(brand_row); nav_layout->addSpacing(18);
    const auto nav_items = {QStringLiteral("主页"), QStringLiteral("AI 工具箱"), QStringLiteral("本地项目"), QStringLiteral("云空间"), QStringLiteral("工作流"), QStringLiteral("选片交付"), QStringLiteral("回收站")};
    for (const auto& label : nav_items) { auto* b = action(label, nav); b->setObjectName(QStringLiteral("home-nav")); b->setProperty("selected", label == QStringLiteral("主页")); b->setEnabled(label == QStringLiteral("主页")); const auto nav_asset = pixcake::shell::homeNavAssetPath(label); if (!nav_asset.isEmpty()) pixcake::shell::applyAssetIcon(b, nav_asset, 18); nav_layout->addWidget(b); }
    nav_layout->addStretch();
    auto* support = action(QStringLiteral("了解更多版本功能"), nav); support->setEnabled(false); nav_layout->addWidget(support);
    outer->addWidget(nav);

    auto* content = new QFrame(page); content->setObjectName(QStringLiteral("home-content")); auto* main = new QVBoxLayout(content); main->setContentsMargins(24, 16, 24, 18); main->setSpacing(12);
    auto* top = new QHBoxLayout;
    auto* search = new pixcake::shell::PixCakeField(QStringLiteral("项目搜索"), content); search->setObjectName(QStringLiteral("home-search")); search->setPlaceholderText(QStringLiteral("搜索所有项目")); search->setMaximumWidth(420);
    const auto search_icon = pixcake::shell::loadAssetPixmap(QStringLiteral("workspace-global/search.svg"), 18);
    if (!search_icon.isNull()) {
      search->addAction(QIcon(search_icon), QLineEdit::LeadingPosition);
    }
    top->addStretch(); top->addWidget(search); top->addStretch();
    for (const auto& label : {QStringLiteral("会员续费"), QStringLiteral("技术支持"), QStringLiteral("个人中心"), QStringLiteral("消息中心"), QStringLiteral("传输列表"), QStringLiteral("导出列表")}) { auto* b = icon_action(label, content); b->setObjectName(QStringLiteral("home-toolbar")); b->setToolTip(label); b->setEnabled(false); top->addWidget(b); } main->addLayout(top);
    auto* page_title = text(QStringLiteral("主页")); page_title->setObjectName(QStringLiteral("home-section-title")); main->addWidget(page_title);

    auto* hero_row = new QHBoxLayout; hero_row->setSpacing(12);
    auto* import = new pixcake::shell::PixCakePanel(QStringLiteral("home-import"), content); import->setAccessibleName(QStringLiteral("照片导入")); auto* import_layout = new QVBoxLayout(import); import_layout->setContentsMargins(24, 14, 24, 14); import_layout->setAlignment(Qt::AlignCenter);
    auto* plus = text(QStringLiteral("＋")); plus->setAlignment(Qt::AlignCenter); QFont pf = plus->font(); pf.setPointSize(30); plus->setFont(pf); import_layout->addWidget(plus);
    auto* import_title = text(QStringLiteral("照片导入")); import_title->setAlignment(Qt::AlignCenter); import_layout->addWidget(import_title); import_layout->addWidget(text(QStringLiteral("拖动或点击导入图片"), true), 0, Qt::AlignCenter); hero_row->addWidget(import, 5);
    for (const auto& label : {QStringLiteral("联机拍摄"), QStringLiteral("AI工具箱"), QStringLiteral("像素助手")}) { auto* card = new QFrame(content); card->setObjectName(QStringLiteral("home-card")); card->setMinimumHeight(116); card->setAccessibleName(label); auto* l = new QVBoxLayout(card); l->setAlignment(Qt::AlignCenter); const auto card_asset = pixcake::shell::homeCardAssetPath(label); auto* card_icon = card_asset.isEmpty() ? pixcake::shell::pendingIcon(card, 28, label + QStringLiteral("图标待补充")) : pixcake::shell::assetIcon(card_asset, 28, label + QStringLiteral("图标"), card); l->addWidget(card_icon, 0, Qt::AlignCenter); auto* t = text(label); t->setObjectName(QStringLiteral("home-card-title")); t->setAlignment(Qt::AlignCenter); l->addWidget(t); l->addWidget(text(QStringLiteral("暂无内容"), true), 0, Qt::AlignCenter); hero_row->addWidget(card, 1); }
    main->addLayout(hero_row);

    auto* common_title = text(QStringLiteral("常用功能")); common_title->setObjectName(QStringLiteral("home-section-title")); main->addWidget(common_title);
    auto* common = new QFrame(content); common->setObjectName(QStringLiteral("home-card")); auto* common_layout = new QVBoxLayout(common); common_layout->setContentsMargins(16, 20, 16, 20); common_layout->addWidget(text(QStringLiteral("暂无内容"), true), 1, Qt::AlignCenter); main->addWidget(common);

    auto* quick_title = text(QStringLiteral("快捷功能")); quick_title->setObjectName(QStringLiteral("home-section-title")); main->addWidget(quick_title);
    auto* quick = new QFrame(content); quick->setObjectName(QStringLiteral("home-card")); auto* quick_layout = new QHBoxLayout(quick); quick_layout->setContentsMargins(16, 12, 16, 12); quick_layout->addWidget(text(QStringLiteral("暂无快捷功能"), true)); quick_layout->addStretch(); main->addWidget(quick);

    auto* projects_title = text(QStringLiteral("项目列表")); projects_title->setObjectName(QStringLiteral("home-section-title")); main->addWidget(projects_title);
    auto* projects = new QFrame(content); projects->setObjectName(QStringLiteral("home-card")); auto* projects_layout = new QVBoxLayout(projects); projects_layout->setContentsMargins(16, 20, 16, 20); projects_layout->addWidget(text(QStringLiteral("暂无项目"), true), 1, Qt::AlignCenter); main->addWidget(projects, 1);
    outer->addWidget(content, 1); set_body(page);
  }
  void render_workspace() {
    auto* page = new QWidget; page->setObjectName(QStringLiteral("refine-page")); auto* outer = new QVBoxLayout(page); outer->setContentsMargins(0, 0, 0, 0); outer->setSpacing(4);
    auto* top = new QFrame(page); top->setObjectName(QStringLiteral("workspace-top")); auto* top_layout = new QHBoxLayout(top); top_layout->setContentsMargins(8, 6, 8, 6);
    auto* back = icon_action(QStringLiteral("返回"), top, true); back->setObjectName(QStringLiteral("top-action")); back->setAccessibleName(QStringLiteral("返回主页面")); connect(back, &QPushButton::clicked, this, [this] { if (display_.navigate(Page::Home) == ResultCode::Ok) render(); }); top_layout->addWidget(back); top_layout->addWidget(icon_action(QStringLiteral("搜索"), top)); top_layout->addWidget(icon_action(QStringLiteral("像素助手"), top)); top_layout->addStretch();
    auto* tabs = new QHBoxLayout; add_nav_button(tabs, QStringLiteral("图库"), Page::Library); add_nav_button(tabs, QStringLiteral("精修"), Page::Refine); add_nav_button(tabs, QStringLiteral("创意"), Page::Creative); top_layout->addLayout(tabs); top_layout->addStretch(); for (const auto& label : {QStringLiteral("技术支持"), QStringLiteral("个人中心"), QStringLiteral("消息中心"), QStringLiteral("传输列表"), QStringLiteral("导出列表"), QStringLiteral("导出")}) { auto* b = icon_action(label, top); b->setEnabled(false); top_layout->addWidget(b); } outer->addWidget(top);
    auto* toolbar = new pixcake::shell::PixCakeToolbar(QStringLiteral("workspace-tools"), page); auto* toolbar_layout = static_cast<QHBoxLayout*>(toolbar->layout()); toolbar_layout->setSpacing(4);
    const auto tools = {QStringLiteral("显示比例调整"), QStringLiteral("抓手"), QStringLiteral("祛边"), QStringLiteral("参考线"), QStringLiteral("液化"), QStringLiteral("污点修复"), QStringLiteral("修补"), QStringLiteral("仿制图章"), QStringLiteral("智能消除"), QStringLiteral("AI点修"), QStringLiteral("重置效果"), QStringLiteral("预设")}; for (const auto& label : tools) toolbar_layout->addWidget(workspace_tool(label, toolbar)); toolbar_layout->addStretch(); outer->addWidget(toolbar);
    auto* center = new QHBoxLayout; center->setSpacing(4); auto* canvas = new QFrame(page); canvas->setObjectName(QStringLiteral("workspace-canvas")); auto* canvas_layout = new QVBoxLayout(canvas); auto* preview = text(QStringLiteral("预览区域\n暂无已加载照片"), true); preview->setAlignment(Qt::AlignCenter); canvas_layout->addWidget(preview, 1); auto* compare = action(QStringLiteral("查看对比"), canvas); compare->setEnabled(false); canvas_layout->addWidget(compare, 0, Qt::AlignRight); center->addWidget(canvas, 1);
    auto* modules = new QFrame(page); modules->setObjectName(QStringLiteral("workspace-side")); modules->setMinimumWidth(pixcake::shell::PixCakeTheme::kWorkspaceSideWidth); modules->setAccessibleName(QStringLiteral("功能区")); auto* modules_layout = new QVBoxLayout(modules); modules_layout->setContentsMargins(14, 12, 14, 12); modules_layout->addWidget(text(QStringLiteral("功能区"))); const QStringList module_labels = {QStringLiteral("AI色彩"), QStringLiteral("调色"), QStringLiteral("局部"), QStringLiteral("人像"), QStringLiteral("图像"), QStringLiteral("衣物"), QStringLiteral("宠物"), QStringLiteral("后调色"), QStringLiteral("裁剪"), QStringLiteral("历史"), QStringLiteral("联机")};
    const QHash<QString, QString> module_descriptions = {
        {QStringLiteral("调色"), QStringLiteral("色彩调节")},
        {QStringLiteral("局部"), QStringLiteral("局部调色")},
        {QStringLiteral("人像"), QStringLiteral("人像美化")},
        {QStringLiteral("图像"), QStringLiteral("图像美化")},
        {QStringLiteral("衣物"), QStringLiteral("衣物美化")},
        {QStringLiteral("后调色"), QStringLiteral("后期调色")},
        {QStringLiteral("裁剪"), QStringLiteral("裁剪、旋转")},
        {QStringLiteral("历史"), QStringLiteral("历史记录")},
        {QStringLiteral("联机"), QStringLiteral("联机拍摄")}};
    for (const auto& label : module_labels) { auto* button = action(label, modules); button->setEnabled(false); if (module_descriptions.contains(label)) button->setAccessibleDescription(module_descriptions.value(label)); modules_layout->addWidget(button); }
    auto* color_labels = new QFrame(modules);
    color_labels->setObjectName(QStringLiteral("refine-color-labels"));
    color_labels->setAccessibleName(QStringLiteral("色彩调节"));
    auto* color_layout = new QVBoxLayout(color_labels);
    color_layout->setContentsMargins(0, 0, 0, 0);
    const QStringList color_names = {
        QStringLiteral("开启 AI 色彩风格"), QStringLiteral("配置文件"),
        QStringLiteral("色彩调节"), QStringLiteral("预设"), QStringLiteral("滤镜"),
        QStringLiteral("推荐"), QStringLiteral("我的"), QStringLiteral("AI 校准"),
        QStringLiteral("程度"), QStringLiteral("白平衡"), QStringLiteral("AI校准白平衡"),
        QStringLiteral("白平衡选择器"), QStringLiteral("色温"), QStringLiteral("色调"),
        QStringLiteral("影调"), QStringLiteral("AI校准曝光"), QStringLiteral("曝光"),
        QStringLiteral("对比度"), QStringLiteral("亮度"), QStringLiteral("高光"),
        QStringLiteral("阴影"), QStringLiteral("白色"), QStringLiteral("黑色"),
        QStringLiteral("纹理"), QStringLiteral("清晰度"), QStringLiteral("祛雾"),
        QStringLiteral("鲜艳度"), QStringLiteral("饱和度"), QStringLiteral("色阶"),
        QStringLiteral("曲线"), QStringLiteral("HSL"), QStringLiteral("色彩编辑器"),
        QStringLiteral("颜色分级"), QStringLiteral("可选颜色"), QStringLiteral("细节"),
        QStringLiteral("颗粒"), QStringLiteral("镜头调整"), QStringLiteral("透视矫正"),
        QStringLiteral("校准"), QStringLiteral("创意色彩")};
    for (const auto& label : color_names) {
      auto* name = text(label);
      name->setEnabled(false);
      color_layout->addWidget(name);
    }
    modules_layout->addWidget(color_labels);
    auto* clothing_labels = new QFrame(modules);
    clothing_labels->setObjectName(QStringLiteral("refine-clothing-labels"));
    clothing_labels->setAccessibleName(QStringLiteral("衣服调整"));
    auto* clothing_layout = new QVBoxLayout(clothing_labels);
    clothing_layout->setContentsMargins(0, 0, 0, 0);
    const QStringList clothing_visible = {
        QStringLiteral("上衣祛褶皱"), QStringLiteral("上衣祛褶皱-祛粗褶皱"),
        QStringLiteral("上衣祛褶皱-祛细褶皱"), QStringLiteral("下装祛褶皱"),
        QStringLiteral("下装祛褶皱-祛粗褶皱"), QStringLiteral("下装祛褶皱-祛细褶皱"),
        QStringLiteral("衣服祛汗渍"), QStringLiteral("衣服祛瑕疵"),
        QStringLiteral("衣服祛褶皱"), QStringLiteral("衣服祛褶皱-祛粗褶皱"),
        QStringLiteral("衣服祛褶皱-祛细褶皱"), QStringLiteral("衣服边缘平整"),
        QStringLiteral("道具保护")};
    const QStringList clothing_full = {
        QStringLiteral("衣物美化-衣服调整-上衣祛褶皱"),
        QStringLiteral("衣物美化-衣服调整-上衣祛褶皱-祛粗褶皱"),
        QStringLiteral("衣物美化-衣服调整-上衣祛褶皱-祛细褶皱"),
        QStringLiteral("衣物美化-衣服调整-下装祛褶皱"),
        QStringLiteral("衣物美化-衣服调整-下装祛褶皱-祛粗褶皱"),
        QStringLiteral("衣物美化-衣服调整-下装祛褶皱-祛细褶皱"),
        QStringLiteral("衣物美化-衣服调整-衣服祛汗渍"),
        QStringLiteral("衣物美化-衣服调整-衣服祛瑕疵"),
        QStringLiteral("衣物美化-衣服调整-衣服祛褶皱"),
        QStringLiteral("衣物美化-衣服调整-衣服祛褶皱-祛粗褶皱"),
        QStringLiteral("衣物美化-衣服调整-衣服祛褶皱-祛细褶皱"),
        QStringLiteral("衣物美化-衣服调整-衣服边缘平整"),
        QStringLiteral("衣物美化-衣服调整-道具保护")};
    for (int i = 0; i < clothing_visible.size(); ++i) {
      auto* name = text(clothing_visible.at(i));
      name->setAccessibleName(clothing_full.at(i));
      name->setEnabled(false);
      clothing_layout->addWidget(name);
    }
    modules_layout->addWidget(clothing_labels);
    modules_layout->addStretch(); center->addWidget(modules); outer->addLayout(center, 1); outer->addWidget(thumbnail_strip(page)); set_body(page);
  }
  void render_library() {
    auto* page = new QWidget; page->setObjectName(QStringLiteral("library-page")); auto* outer = new QVBoxLayout(page); outer->setContentsMargins(0, 0, 0, 0); outer->setSpacing(4);
    auto* top = new QFrame(page); top->setObjectName(QStringLiteral("workspace-top")); auto* top_layout = new QHBoxLayout(top); top_layout->setContentsMargins(8, 6, 8, 6); auto* home = icon_action(QStringLiteral("首页"), top, true); home->setObjectName(QStringLiteral("top-action")); home->setAccessibleName(QStringLiteral("首页")); connect(home, &QPushButton::clicked, this, [this] { if (display_.navigate(Page::Home) == ResultCode::Ok) render(); }); top_layout->addWidget(home); top_layout->addWidget(icon_action(QStringLiteral("搜索"), top)); top_layout->addStretch(); auto* tabs = new QHBoxLayout; add_nav_button(tabs, QStringLiteral("图库"), Page::Library); add_nav_button(tabs, QStringLiteral("精修"), Page::Refine); add_nav_button(tabs, QStringLiteral("创意"), Page::Creative); top_layout->addLayout(tabs); top_layout->addStretch(); for (const auto& label : {QStringLiteral("会员续费"), QStringLiteral("技术支持"), QStringLiteral("个人中心"), QStringLiteral("消息中心"), QStringLiteral("传输列表"), QStringLiteral("导出列表"), QStringLiteral("导出")}) { auto* b = icon_action(label, top); b->setEnabled(false); top_layout->addWidget(b); } outer->addWidget(top);
    auto* controls = new pixcake::shell::PixCakeStatusStrip(QStringLiteral("workspace-tools"), page); auto* controls_layout = controls->row(); auto* status = action(QStringLiteral("显示状态"), controls); status->setEnabled(false); status->setAccessibleName(QStringLiteral("显示状态：显示RAW格式、显示修图状态、显示导出状态")); controls_layout->addWidget(status); controls_layout->addWidget(icon_action(QStringLiteral("全效果模式"), controls)); controls_layout->addWidget(icon_action(QStringLiteral("导入新图片"), controls)); controls_layout->addWidget(icon_action(QStringLiteral("筛选"), controls)); controls_layout->addWidget(icon_action(QStringLiteral("排序"), controls)); controls_layout->addStretch(); auto* selected = text(QStringLiteral("已选 0 张（共 0 张）"), true); controls_layout->addWidget(selected); auto* del = action(QStringLiteral("批量删除"), controls); del->setEnabled(false); controls_layout->addWidget(del); outer->addWidget(controls);
    auto* content = new QHBoxLayout; content->setSpacing(4); auto* grid = new pixcake::shell::PixCakePanel(QStringLiteral("library-grid"), page); auto* grid_layout = new QVBoxLayout(grid); grid_layout->setContentsMargins(16, 16, 16, 16); auto* grid_empty = new pixcake::shell::PixCakeEmptyState(QStringLiteral("暂无照片"), QStringLiteral("导入图片后将在此显示"), grid); grid_layout->addWidget(grid_empty, 1); content->addWidget(grid, 1);
    auto* side = new QFrame(page); side->setObjectName(QStringLiteral("workspace-side")); side->setMinimumWidth(pixcake::shell::PixCakeTheme::kWorkspaceSideWidth); auto* side_layout = new QVBoxLayout(side); side_layout->setContentsMargins(14, 12, 14, 12); side_layout->addWidget(text(QStringLiteral("侧边栏"))); for (const auto& label : {QStringLiteral("AI挑图"), QStringLiteral("预设"), QStringLiteral("元数据")}) { auto* section = new QGroupBox(label, side); auto* l = new QVBoxLayout(section); l->addWidget(pixcake::shell::emptyState(QStringLiteral("暂无内容"), QString(), section)); side_layout->addWidget(section); } side_layout->addStretch(); content->addWidget(side); outer->addLayout(content, 1);
    auto* library_views = new pixcake::shell::PixCakeToolbar(QStringLiteral("library-views"), page); auto* library_views_layout = library_views->row(); auto* span = new QSlider(Qt::Horizontal, library_views); span->setObjectName(QStringLiteral("library-grid-span")); span->setAccessibleName(QStringLiteral("网格每行张数，范围 1 到 7，默认位置未确认")); span->setRange(1, 7); span->setEnabled(false); span->setFixedWidth(120); library_views_layout->insertWidget(0, span); for (const auto& label : {QStringLiteral("网格视图"), QStringLiteral("单视图"), QStringLiteral("筛选视图")}) { auto* b = icon_action(label, library_views); b->setEnabled(false); library_views_layout->addWidget(b); } library_views_layout->addStretch(); outer->addWidget(library_views); outer->addWidget(thumbnail_strip(page, true)); set_body(page);
  }
  void render_creative() {
    auto* page = new QWidget; page->setObjectName(QStringLiteral("creative-page")); auto* outer = new QVBoxLayout(page); outer->setContentsMargins(0, 0, 0, 0); outer->setSpacing(4);
    auto* top = new QFrame(page); top->setObjectName(QStringLiteral("workspace-top")); auto* top_layout = new QHBoxLayout(top); top_layout->setContentsMargins(8, 6, 8, 6); top_layout->addStretch(); auto* tabs = new QHBoxLayout; add_nav_button(tabs, QStringLiteral("图库"), Page::Library); add_nav_button(tabs, QStringLiteral("精修"), Page::Refine); add_nav_button(tabs, QStringLiteral("创意"), Page::Creative); top_layout->addLayout(tabs); top_layout->addStretch(); for (const auto& label : {QStringLiteral("AI工具"), QStringLiteral("历史"), QStringLiteral("批量"), QStringLiteral("帮助"), QStringLiteral("导出")}) { auto* b = icon_action(label, top, false, label == QStringLiteral("帮助") ? 16 : 18); b->setEnabled(false); top_layout->addWidget(b); } outer->addWidget(top);
    auto* center = new QHBoxLayout; center->setSpacing(4); auto* tool_panel = new pixcake::shell::PixCakeToolbar(QStringLiteral("workspace-tools"), page); auto* tool_layout = static_cast<QHBoxLayout*>(tool_panel->layout()); tool_layout->setSpacing(4); for (const auto& label : {QStringLiteral("显示比例调整"), QStringLiteral("更改屏幕模式"), QStringLiteral("抓手"), QStringLiteral("移动图层"), QStringLiteral("缩放工具"), QStringLiteral("液化"), QStringLiteral("污点修复画笔"), QStringLiteral("修补工具"), QStringLiteral("仿制图章"), QStringLiteral("矩形/椭圆选区"), QStringLiteral("套索/选区画笔"), QStringLiteral("裁切")}) tool_layout->addWidget(workspace_tool(label, tool_panel)); tool_layout->addStretch();
    auto* canvas = new pixcake::shell::PixCakePanel(QStringLiteral("creative-canvas"), page); auto* canvas_layout = new QVBoxLayout(canvas); auto* preview = new pixcake::shell::PixCakeEmptyState(QStringLiteral("暂无图片或图层"), QStringLiteral("将照片拖入画布后开始创作"), canvas); canvas_layout->addWidget(preview, 1); center->addWidget(canvas, 1);
    auto* side = new QFrame(page); side->setObjectName(QStringLiteral("workspace-side")); side->setMinimumWidth(pixcake::shell::PixCakeTheme::kWorkspaceSideWidth); auto* side_layout = new QVBoxLayout(side); side_layout->setContentsMargins(14, 12, 14, 12); for (const auto& title : {QStringLiteral("AI工具箱"), QStringLiteral("侧边栏"), QStringLiteral("图层面板")}) { auto* group = new QGroupBox(title, side); auto* l = new QVBoxLayout(group); if (title == QStringLiteral("侧边栏")) { for (const auto& item : {QStringLiteral("AI工具"), QStringLiteral("历史记录"), QStringLiteral("批量设计"), QStringLiteral("帮助"), QStringLiteral("新手引导"), QStringLiteral("反馈")}) { auto* b = pixcake::shell::globalAssetPath(item).isEmpty() ? action(item, group) : icon_action(item, group, false, item == QStringLiteral("帮助") ? 16 : 18); b->setEnabled(false); l->addWidget(b); } } else { l->addWidget(pixcake::shell::emptyState(QStringLiteral("暂无内容"), QString(), group)); } side_layout->addWidget(group); } side_layout->addStretch(); center->addWidget(side); outer->insertWidget(1, tool_panel); outer->addLayout(center, 1); outer->addWidget(thumbnail_strip(page)); set_body(page);
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
  QApplication app(argc, argv); pixcake::shell::PixCakeTheme::apply(app); QCoreApplication::setApplicationName(QStringLiteral("PixCake")); QCoreApplication::setApplicationVersion(QStringLiteral("0.1")); QCommandLineParser parser; parser.setApplicationDescription(QStringLiteral("PixCake desktop shell")); parser.addHelpOption(); QString default_page = QStringLiteral("login"); const auto executable = QFileInfo(QString::fromLocal8Bit(argv[0])).baseName(); for (const auto& page : {QStringLiteral("home"), QStringLiteral("library"), QStringLiteral("refine"), QStringLiteral("creative")}) if (executable.endsWith(QStringLiteral("-") + page)) default_page = page; QCommandLineOption page_option(QStringList() << QStringLiteral("page"), QStringLiteral("Display a read-only shell page: login, home, library, refine, creative."), QStringLiteral("page"), default_page); QCommandLineOption smoke_option(QStringList() << QStringLiteral("smoke"), QStringLiteral("Construct the selected page and exit without showing a window.")); parser.addOption(page_option); parser.addOption(smoke_option); parser.process(app); ShellWindow window(parse_page(parser.value(page_option))); if (parser.isSet(smoke_option)) { const auto bundled = QCoreApplication::applicationDirPath() + QStringLiteral("/../Resources/assets/"); for (const auto& relative : pixcake::shell::uploadedShellAssets()) { if (!QFileInfo::exists(bundled + relative) || pixcake::shell::loadAssetPixmap(relative, 16).isNull()) { qCritical("missing shell asset: %s", qPrintable(relative)); return 2; } } return 0; } window.show(); return app.exec();
}
