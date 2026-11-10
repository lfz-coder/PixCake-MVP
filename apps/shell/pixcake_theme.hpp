#pragma once

#include <QApplication>
#include <QColor>
#include <QString>

namespace pixcake::shell {

// Visual tokens live in the shell. The official screenshots establish the
// dark-workspace/yellow-accent relationship. User-supplied SVG files in
// assets/ are drawn as provided; missing files stay pending-icon.
struct PixCakeTheme final {
  static constexpr int kWindowWidth = 1386;
  static constexpr int kWindowHeight = 832;
  static constexpr int kRadiusSmall = 4;
  static constexpr int kRadiusPanel = 8;
  static constexpr int kControlHeight = 32;
  static constexpr int kFieldHeight = 40;
  static constexpr int kToolbarHeight = 60;
  static constexpr int kSidebarWidth = 220;
  static constexpr int kWorkspaceSideWidth = 282;

  static QColor workspace() { return QColor("#0b0b0b"); }
  static QColor panel() { return QColor("#141414"); }
  static QColor panelRaised() { return QColor("#1d1d1d"); }
  static QColor panelStrong() { return QColor("#242424"); }
  static QColor canvas() { return QColor("#070707"); }
  static QColor field() { return QColor("#101010"); }
  static QColor border() { return QColor("#383838"); }
  static QColor borderSoft() { return QColor("#292929"); }
  static QColor text() { return QColor("#f4f4f4"); }
  static QColor muted() { return QColor("#a4a4a4"); }
  static QColor accent() { return QColor("#ffe000"); }
  static QColor accentMuted() { return QColor("#685a0c"); }

  static QString globalStyle() {
    return QStringLiteral(R"QSS(
      * { outline: none; }
      QWidget { color: #f4f4f4; font-family: "PingFang SC", "Helvetica Neue", sans-serif; font-size: 13px; }
      QToolTip { background: #222226; color: #FFFFFF; border: none; border-radius: 2px; padding: 4px 8px;
                 font-family: "SF Pro Text", "SF Pro Display", "PingFang SC", "-apple-system", sans-serif;
                 font-size: 12px; }
      QMainWindow, QWidget#shell-root, QWidget#login-page, QWidget#home-page,
      QWidget#local-projects-page { background: #18181A; }
      QWidget#local-project-import-page { background: #0F0F11; }
      QWidget#refine-page, QWidget#library-page, QWidget#creative-page { background: #0b0b0b; }
      QWidget#login-panel { background: #0b0b0b; }
      QFrame#workspace-top, QFrame#workspace-tools, QFrame#workspace-thumbs,
      QFrame#workspace-side, QFrame#home-sidebar, QFrame#home-content { background: #141414; }
      QFrame#local-projects-sidebar { background: #141416; }
      QFrame#local-projects-content { background: #18181A; }
      QLabel#local-projects-title { color: #FFFFFF; font-size: 24px; font-weight: 600; }
      QLabel#local-projects-brand { color: #F4F4F4; font-size: 19px; font-weight: 600; }
      QPushButton#local-projects-nav-item { text-align: left; padding: 9px 12px; border: none; background: transparent; }
      QPushButton#local-projects-nav-item:hover, QPushButton#local-projects-nav-item[selected="true"] {
        background: #3D3D43; border-radius: 6px; }
      QPushButton#local-projects-membership { min-height: 30px; padding: 3px 12px; border: none; background: #2A2A2F; border-radius: 15px; }
      QPushButton#local-projects-membership:hover { background: #36363C; }
      QPushButton#local-projects-avatar { min-height: 32px; max-height: 32px; padding: 0; border: none; border-radius: 16px;
        background: #5B6E85; color: #FFFFFF; font-weight: 600; }
      QWidget#local-projects-grid { background: transparent; }
      QScrollArea#local-projects-scroll { background: transparent; }
      QFrame#local-project-import-drop { background: #161619; border: 1px dashed #66666E; border-radius: 12px; }
      QFrame#local-project-import-drop:hover { background: #202024; border-color: #8B8B96; }
      QLabel#local-project-import-title { color: #FFFFFF; font-size: 20px; font-weight: 600; margin-bottom: 16px; }
      QLabel#local-project-import-plus { color: #8A8A8F; font-size: 52px; font-weight: 300; }
      QPushButton#local-project-import-link { background: transparent; border: none; color: #A6A6AF; text-decoration: underline; padding: 5px; }
      QPushButton#local-project-import-link:hover { color: #FFFFFF; }
      QDialog#create-project-dialog { background: #38383C; border-radius: 12px; }
      QLabel#create-project-title { color: #FFFFFF; font-size: 24px; font-weight: 600; }
      QPushButton#create-project-close { background: transparent; border: none; color: #B8B8BF; font-size: 24px; padding: 0; }
      QPushButton#create-project-close:hover { color: #FFFFFF; }
      QLineEdit#create-project-name { background: #28282B; border: 1px solid #49494F; border-radius: 7px; color: #FFFFFF; min-height: 40px; }
      QLineEdit#create-project-name:focus { border: 1px solid #489CFF; }
      QLabel#create-project-counter { min-width: 40px; }
      QPushButton#create-project-save { background: #FFD02E; color: #161616; border: none; border-radius: 8px; font-size: 18px; font-weight: 600; }
      QPushButton#create-project-save:hover { background: #FFDA57; }
      QDialog#export-dialog, QDialog#export-list-dialog { background: #141414; }
      QDialog#export-dialog QScrollArea, QDialog#export-dialog QScrollArea > QWidget, QWidget#export-form { background: #141414; }
      QDialog#export-dialog QTabBar::tab { background: #1d1d1d; color: #a4a4a4; padding: 8px 28px; border: none; }
      QDialog#export-dialog QTabBar::tab:selected { background: #2a2a2a; color: #f4f4f4; }
      QDialog#export-dialog QTabBar::tab:disabled { color: #555555; }
      QDialog#export-dialog QSpinBox { min-width: 64px; }
      QDialog#export-dialog QSpinBox, QDialog#export-dialog QDoubleSpinBox {
        background: #101010; color: #f4f4f4; border: 1px solid #383838; border-radius: 3px; padding: 2px 4px; }
      QDialog#export-dialog QDoubleSpinBox:disabled { color: #555555; }
      QToolButton#export-advanced { background: transparent; border: none; color: #f4f4f4; padding: 4px 0; }
      QGroupBox#export-watermark QToolButton { background: #1d1d1d; color: #d5d5d5; border: 1px solid #383838;
        border-radius: 3px; padding: 4px 14px; }
      QGroupBox#export-watermark QToolButton:checked { background: #3a3a3a; color: #f4f4f4; }
      QGroupBox#export-watermark QToolButton:hover { border-color: #5a5a5a; }
      QGroupBox#export-watermark QToolButton[anchor="true"] { padding: 0; border-radius: 2px; }
      QGroupBox#export-watermark QToolButton[anchor="true"]:checked { background: #ffe600; border-color: #ffe600; }
      QToolButton#export-wm-material { background: #101010; color: #a4a4a4; border: 1px dashed #4a4a4a;
        border-radius: 4px; padding: 8px; }
      QDialog#export-list-dialog QTableWidget { background: #101010; gridline-color: #292929; }
      QDialog#export-list-dialog QHeaderView::section { background: #1d1d1d; color: #a4a4a4; border: none; padding: 4px; }
      QFrame#workspace-top { border-bottom: 1px solid #292929; }
      QFrame#workspace-tools { border-bottom: 1px solid #292929; }
      QFrame#workspace-thumbs { border-top: 1px solid #292929; }
      QFrame#workspace-side { border-left: 1px solid #292929; }
      QFrame#workspace-canvas, QFrame#library-grid, QFrame#creative-canvas { background: #070707; border: 1px solid #353535; border-radius: 4px; }
      QFrame#library-grid { background: #252525; }
      QLabel#login-hero { background: #7e9caf; }
      QLabel#login-heading { color: #f4f4f4; font-size: 22px; font-weight: 500; }
      QLabel#home-brand { color: #f4f4f4; font-size: 19px; font-weight: 600; }
      QLabel#home-section-title { color: #f4f4f4; font-size: 15px; font-weight: 600; }
      QLabel#home-card-title { color: #f4f4f4; font-size: 15px; font-weight: 600; }
      QPushButton[icon-only="true"] { background: transparent; border: none; padding: 4px; min-height: 28px; }
      QPushButton[icon-only="true"]:hover { background: #2a2d33; }
      QPushButton[icon-only="true"]:disabled { background: transparent; color: #747474; }
      QLabel#muted, QLabel[muted="true"], QLabel#empty-state { color: #999999; }
      QLabel[warning="true"] { color: #ffe000; }
      QPushButton { background: #1a1a1a; border: 1px solid #383838; border-radius: 4px; color: #f4f4f4; padding: 6px 11px; min-height: 32px; }
      QPushButton:hover { border-color: #ffe000; }
      QPushButton:pressed { background: #303030; }
      QPushButton:disabled { color: #747474; border-color: #2b2b2b; }
      QPushButton#primary { background: #ffe000; color: #121212; border-color: #ffe000; min-height: 40px; font-weight: 600; }
      QPushButton#primary:disabled { background: #685a0c; color: #34300d; border-color: #685a0c; }
      QPushButton#icon-button { min-width: 34px; max-width: 34px; padding: 3px; }
      QPushButton#pill { border-radius: 14px; min-height: 26px; padding: 3px 12px; }
      QPushButton#login-wechat, QPushButton#login-merchant {
        border-radius: 18px; min-height: 34px; padding: 3px 14px; background: #111111; border-color: #303030; }
      QPushButton#login-wechat:disabled, QPushButton#login-merchant:disabled { color: #8a8a8a; border-color: #303030; background: #111111; }
      QPushButton#top-action { min-height: 26px; padding: 3px 8px; border: none; background: transparent; }
      QPushButton#top-action:hover { background: #2a2a2a; border-radius: 4px; }
      QPushButton#page-tab { min-height: 27px; padding: 3px 18px; border: none; background: #1b1b1b; }
      QPushButton[page-tab="true"] { min-height: 27px; padding: 3px 18px; border: none; background: #1b1b1b; }
      QPushButton[page-tab="true"][selected="true"] { background: #3d3d3d; color: #ffffff; }
      QPushButton#home-nav { text-align: left; padding: 9px 12px; border: none; background: transparent; }
      QPushButton#home-nav:hover, QPushButton#home-nav[selected="true"] { background: #3d3d3d; border-radius: 6px; }
      QPushButton#home-toolbar { min-height: 26px; padding: 3px 8px; }
      QLineEdit { background: #101010; border: 1px solid #494949; border-radius: 2px; color: #f4f4f4; padding: 0 10px; min-height: 38px; }
      QLineEdit:focus { border: 1px solid #ffe000; }
      QLineEdit#home-search { min-height: 28px; border-radius: 14px; background: #292929; border-color: #414141; }
      QCheckBox { spacing: 8px; color: #d7d7d7; }
      QCheckBox::indicator { width: 14px; height: 14px; border: 1px solid #777777; background: #101010; }
      QCheckBox::indicator:checked { background: #ffe000; border-color: #ffe000; }
      QFrame#home-feature, QFrame#home-card, QFrame#home-projects { background: #171717; border: 1px solid #2d2d2d; border-radius: 8px; }
      QFrame#home-import { background: #141414; border: 1px dashed #5b5b5b; border-radius: 8px; }
      QFrame#home-sidebar { background: #0c0c0c; border-right: 1px solid #1c1c1c; }
      QWidget#home-page QFrame#home-content { background: #070707; }
      QFrame#home-sidebar QPushButton#home-nav { min-height: 36px; border-radius: 8px; color: #d2d2d2; }
      QFrame#home-sidebar QPushButton#home-nav:hover { background: #242424; }
      QFrame#home-sidebar QPushButton#home-nav[selected="true"] { background: #2e2e2e; color: #ffffff; }
      QWidget#home-page QFrame#home-import { background: #121212; border: 1px solid #2a2a2a; border-radius: 12px; }
      QWidget#home-page QFrame#home-import:hover { border-color: #5a5a5a; }
      QWidget#home-page QPushButton#home-card { background: #141414; border: 1px solid #2a2a2a; border-radius: 12px; }
      QWidget#home-page QPushButton#home-card:hover, QWidget#home-page QPushButton#home-quick:hover { border-color: #5a5a5a; background: #242424; }
      QWidget#home-page QPushButton#home-quick { background: #1a1a1a; border: 1px solid #1a1a1a; border-radius: 10px; text-align: left; padding-left: 13px; }
      QWidget#home-page QPushButton#home-project-tab { background: transparent; border: none; color: #8a8a8a; min-height: 26px; padding: 0; }
      QWidget#home-page QPushButton#home-project-tab:hover { color: #ffffff; }
      QWidget#home-page QPushButton#home-project-refresh { min-width: 26px; max-width: 26px; padding: 0; }
      QFrame#home-sidebar QPushButton#home-promo {
        background: qlineargradient(x1:0,y1:0,x2:1,y2:0,stop:0 #16345c,stop:1 #e6b84a);
        border: none; border-radius: 12px; text-align: left; padding: 12px;
      }
      QFrame#feature-icon { background: #303030; border: 1px solid #555555; border-radius: 8px; }
      QFrame#workspace-tool-button { background: transparent; border: none; border-radius: 4px; }
      QFrame#workspace-tool-button:hover { background: #2a2d33; }
      QFrame#workspace-tool-button[emphasis="true"] { background: #33363d; }
      QFrame#pixcake-status-strip { background: #111111; border: 1px solid #303030; border-radius: 4px; }
      QFrame#pixcake-section { background: #171717; border: 1px solid #2d2d2d; border-radius: 8px; }
      QLabel#pending-icon { color: #777777; background: #303030; border: 1px solid #555555; border-radius: 8px; }
      QLabel#shell-icon { background: transparent; border: none; }
      QFrame#empty-state { background: transparent; border: none; }
      QLabel#empty-state-title { color: #d8d8d8; font-size: 15px; font-weight: 500; }
      QLabel#empty-state-detail { color: #929292; }
      QPushButton#login-help { min-width: 30px; max-width: 30px; min-height: 30px; max-height: 30px; border-radius: 15px; padding: 0; }
      QGroupBox { background: #1c1c1c; border: 1px solid #353535; border-radius: 7px; margin-top: 13px; padding: 17px 10px 10px 10px; }
      QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px; color: #eeeeee; }
      QFrame#login-divider-line { background: #454545; min-height: 1px; max-height: 1px; }
      QScrollArea { border: none; background: #141414; }
      QScrollBar:vertical { background: #141414; width: 10px; margin: 0; }
      QScrollBar:horizontal { background: #141414; height: 10px; margin: 0; }
      QScrollBar::handle:vertical { background: #3a3a3a; min-height: 24px; border-radius: 5px; }
      QScrollBar::handle:horizontal { background: #3a3a3a; min-width: 24px; border-radius: 5px; }
      QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
      QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }
      QListWidget#home-projects { background: transparent; border: none; }
      QListWidget#home-projects::item { background: #262626; border-radius: 6px; padding: 8px; margin: 4px; min-width: 172px; }
      QListWidget#home-projects::item:selected { background: #2f2f2f; border: 1px solid #f5c518; }
      QListWidget#thumbs-photos { background: transparent; border: none; }
      QListWidget#thumbs-photos::item { border-radius: 4px; padding: 2px; }
      QListWidget#thumbs-photos::item:selected { background: #3a3a3a; }
      QListWidget#library-photos { background: #141414; border: 1px solid #292929; border-radius: 6px; color: #d8d8d8; }
      QListWidget#library-photos::item { border-radius: 6px; padding: 6px; }
      QListWidget#library-photos::item:selected { background: #2f2f2f; color: #ffffff; }
      QComboBox { background: #262626; color: #e6e6e6; border: 1px solid #3a3a3a; border-radius: 4px;
                  padding: 2px 8px; min-height: 22px; }
      QComboBox:disabled { color: #6a6a6a; border-color: #2c2c2c; }
      QComboBox QAbstractItemView { background: #262626; color: #e6e6e6; selection-background-color: #3a3a3a;
                                    border: 1px solid #3a3a3a; }
      QComboBox#refine-zoom-ratio { background: #3C3C3E; color: #FFFFFF; border: 1px solid #505054;
                                    border-radius: 8px; padding: 0 10px; min-height: 32px;
                                    font-family: "SF Pro Text", "SF Pro Display", "-apple-system", sans-serif; }
      QComboBox#refine-zoom-ratio::drop-down { width: 20px; border: none; }
      QComboBox#refine-zoom-ratio:hover { background: #515154; }
      QSlider::groove:horizontal { height: 4px; background: #4a4a4a; border-radius: 2px; }
      QSlider::sub-page:horizontal { background: #d0d0d0; border-radius: 2px; }
      QSlider::handle:horizontal { width: 14px; height: 14px; margin: -5px 0; background: #d9d9d9; border-radius: 7px; }

      QFrame#refine-rail { background: #1b1d22; border-left: 1px solid #2a2c31; }
      QFrame#refine-rail-separator { background: #34363b; min-height: 1px; max-height: 1px; margin: 6px 10px; }
      QToolButton[rail="true"] { background: transparent; border: none; border-radius: 6px; color: #b8b8b8;
        padding: 4px; min-width: 40px; max-width: 40px; min-height: 40px; max-height: 40px; }
      QToolButton[rail="true"]:hover { background: #2a2d33; color: #f4f4f4; }
      QToolButton[rail="true"]:checked { background: #33363d; color: #ffffff; }
      QFrame#refine-module-panel, QWidget#refine-module-page, QWidget#refine-module-content { background: #1f2126; }
      QFrame#refine-module-panel { border-left: 1px solid #2a2c31; }
      QFrame#refine-module-header { background: #1f2126; border-bottom: 1px solid #2a2c31; }
      QLabel#refine-module-title { color: #f4f4f4; font-size: 15px; font-weight: 600; }
      QWidget#refine-color-top { background: #1f2126; }
      QWidget#refine-module-page QScrollArea, QWidget#refine-module-page QScrollArea > QWidget > QWidget { background: #1f2126; }
      QFrame#refine-preset-column { background: #1f2126; border-left: 1px solid #2a2c31; }
      QFrame#refine-side-bottom { background: #1f2126; border-top: 1px solid #2a2c31; border-left: 1px solid #2a2c31; }
      QFrame#refine-histogram-card { background: #16181c; border-radius: 6px; }
      QFrame#refine-histogram { border-top-left-radius: 6px; border-top-right-radius: 6px; }
      QLabel#refine-readout { color: #d0d0d0; font-size: 12px; }
      QLabel#refine-readout-key { color: #8a8d93; font-size: 12px; }
      QLabel#refine-format-badge { color: #c8c8c8; background: #34373d; border-radius: 3px; padding: 1px 5px; font-size: 10px; }
      QPushButton#refine-pill { background: #33363d; border: none; border-radius: 14px; min-height: 28px; padding: 0 14px;
        color: #e0e0e0; }
      QPushButton#refine-pill:disabled { color: #8a8d93; }
      QPushButton#refine-master-color { background: #33363d; border: none; border-radius: 4px; min-height: 28px; padding: 0 12px; }
      QFrame#refine-section-rule { background: #2a2c31; }
      QPushButton#refine-segment { background: #2a2d33; border: none; border-radius: 0; min-height: 30px; color: #9a9da3; }
      QPushButton#refine-segment:checked { background: #43464d; color: #f4f4f4; }
      QLabel#refine-notice { background: #2a2d33; border-radius: 3px; color: #8a8d93; }
      QToolButton#refine-local-mask-toggle { background: #303030; border: 1px solid #555555; border-radius: 6px; }
      QToolButton#refine-local-mask-toggle:checked { border-color: #ffd534; }
      QFrame[floating-panel="true"] { background: #1f2227; border: 1px solid #36383e; border-radius: 4px; }
      QFrame#floating-panel-handle { background: #35373c; border: none; border-radius: 2px; }
      QLabel#floating-panel-title { color: #f2f2f2; font-size: 14px; font-weight: 600; }
      QToolButton#floating-panel-close { background: transparent; border: none; padding: 2px; }
      QFrame#floating-panel-rule { background: #36383e; border: none; }
      QToolButton#refine-mask-new { background: transparent; border: none; color: #b4b6b8; font-size: 13px;
        padding: 10px 16px; text-align: left; }
      QToolButton#refine-mask-new:hover { color: #e6e6e6; }
      QTreeWidget#refine-mask-tree { background: transparent; border: none; color: #ececec; font-size: 13px; outline: none; }
      QPushButton#refine-mask-edit { background: transparent; border: 1px solid #3e3f42; border-radius: 2px;
        color: #ececec; font-weight: 600; min-height: 22px; margin: 2px 12px 8px 0; }
      QPushButton#refine-mask-edit:checked { border-color: #ffd534; }
      QFrame#refine-local-edit { background: #1b1d21; border-top: 1px solid #2c2e33; border-bottom: 1px solid #2c2e33; }
      QLabel#refine-local-edit-title { color: #f0f0f0; font-weight: 600; }
      QLabel#refine-subheading { color: #f4f4f4; font-size: 14px; font-weight: 600; }
      QLabel#refine-badge { color: #ffe000; background: #111214; border-radius: 2px; padding: 0 3px; font-size: 10px;
        font-weight: 600; }
      QLabel#refine-help { color: #9a9da3; border: 1px solid #9a9da3; border-radius: 8px; font-size: 10px; }
      QToolButton#refine-expander { background: transparent; border: none; padding: 0; }
      QFrame#refine-child-box { background: #1a1c20; border-radius: 4px; }
      QFrame#refine-preview-status { background: transparent; border-radius: 6px; min-height: 28px; }
      QFrame#refine-preview-status:hover { background: #25282e; }
      QLabel#refine-preview-status-info { color: #B8B8BF; font-size: 14px; }
      QLabel#refine-preview-status-text { color: #B8B8BF; font-size: 14px; }
      QCheckBox#refine-switch::indicator { width: 28px; height: 16px; border-radius: 8px; background: #3a3d44; border: none; }
      QCheckBox#refine-switch::indicator:checked { background: #2f7cf6; }
      QToolButton#refine-card { background: #33363d; border: 1px solid transparent; border-radius: 3px; color: #d0d0d0;
        padding: 0 0 6px 0; }
      QToolButton#refine-card:checked { background: #2a2d33; border-color: #ffe000; }
      QToolButton#refine-card:disabled { color: #9a9da3; }
      QToolButton#refine-swatch { background: #33363d; border: 1px solid transparent; border-radius: 3px; }
      QToolButton#refine-swatch:checked { border-color: #ffe000; }
      QPushButton#refine-tab, QPushButton#refine-person { background: #33363d; border: 1px solid transparent;
        border-radius: 3px; min-height: 26px; padding: 0 8px; color: #d0d0d0; }
      QPushButton#refine-tab:checked { background: #1f2126; border-color: #e6e6e6; color: #ffffff; }
      QPushButton#refine-person { min-height: 30px; }
      QPushButton#refine-person:checked { background: #2f7cf6; color: #ffffff; }
      QPushButton#refine-ai-button { background: #2a2d33; border: 1px solid #3a3d44; border-radius: 6px; min-height: 40px;
        text-align: left; padding: 0 16px; color: #e6c35a; }
      QPushButton#refine-local-tab { background: #33363d; border: 1px solid transparent; border-radius: 3px;
        min-height: 26px; padding: 0 12px; color: #d0d0d0; }
      QPushButton#refine-local-tab:checked { background: #1f2126; border-color: #e6e6e6; color: #ffffff; }
      QPushButton#refine-local-part { background: #33363d; border: 1px solid transparent; border-radius: 3px;
        min-height: 28px; padding: 0 10px; text-align: left; color: #e0e0e0; }
      QPushButton#refine-local-part:hover { background: #3c3f47; }
      QPushButton#refine-local-part:checked { background: #2a2d33; border-color: #e6e6e6; }
      QPushButton#refine-local-part:disabled { background: #2a2d33; color: #6f7277; }
      QPushButton#refine-local-new { background: #33363d; border: none; border-radius: 3px; min-height: 32px; }
      QPushButton#refine-local-new:hover { background: #3c3f47; }
      QLabel#refine-local-common, QLabel#refine-local-list-title { color: #f4f4f4; font-size: 14px; font-weight: 600; }
      QFrame#refine-local-labels QListWidget { background: #16181c; border: 1px solid #2a2c31; border-radius: 3px; }
      QFrame#refine-side-bottom QPushButton { background: #33363d; border: none; border-radius: 4px; min-height: 30px; }
      QFrame#refine-side-bottom QPushButton:hover { background: #3c3f47; }
      QFrame#refine-side-bottom QPushButton:disabled { color: #6f7277; background: #2a2d33; }
      QToolButton#refine-section-toggle { spacing: 8px; }
      QFrame[fold-section="true"] { background: #1f2126; border-bottom: 1px solid #2a2c31; }
      QFrame#refine-section-header { min-height: 48px; max-height: 48px; }
      QToolButton#refine-section-toggle { background: transparent; border: none; color: #f4f4f4; font-size: 14px;
        font-weight: 600; padding: 0; }
      QToolButton#refine-section-action { background: transparent; border: none; color: #b8b8b8; font-size: 11px; }
      QToolButton#refine-section-action:disabled { color: #6f7277; }
      QWidget#refine-section-body QLabel { color: #d0d0d0; }
      QWidget#refine-section-body QLabel:disabled { color: #6f7277; }
      QWidget#refine-section-body QSlider::groove:horizontal { height: 2px; background: #36383d; border-radius: 1px; }
      QWidget#refine-section-body QSlider::sub-page:horizontal { background: #36383d; border-radius: 1px; }
      QWidget#refine-section-body QSlider::handle:horizontal { width: 10px; height: 10px; margin: -6px 0;
        background: transparent; border: 2px solid #b4b6b8; border-radius: 7px; }
      QWidget#refine-section-body QSlider::handle:horizontal:hover { border-color: #ffffff; }
      QWidget#refine-section-body QSlider::handle:horizontal:disabled { border-color: #5a5d63; }
      /* Slider tracks, color bands, and the initial marker are painted by PixCakeSlider. */
    )QSS");
  }

  static void apply(QApplication& app) { app.setStyleSheet(globalStyle()); }
};

}  // namespace pixcake::shell
