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
      QMainWindow, QWidget#shell-root, QWidget#login-page, QWidget#home-page,
      QWidget#refine-page, QWidget#library-page, QWidget#creative-page { background: #0b0b0b; }
      QWidget#login-panel { background: #0b0b0b; }
      QFrame#workspace-top, QFrame#workspace-tools, QFrame#workspace-thumbs,
      QFrame#workspace-side, QFrame#home-sidebar, QFrame#home-content { background: #141414; }
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
      QLabel#workspace-tool-label { color: #d5d5d5; font-size: 11px; }
      QLabel#muted, QLabel[muted="true"], QLabel#empty-state { color: #999999; }
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
      QFrame#feature-icon { background: #303030; border: 1px solid #555555; border-radius: 8px; }
      QFrame#workspace-tool-button { background: #1a1a1a; border: 1px solid #303030; border-radius: 4px; min-width: 66px; }
      QFrame#workspace-tool-button:hover { border-color: #ffe000; }
      QFrame#workspace-tool-button[emphasis="true"] { background: #292929; border-color: #ffe000; }
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
      QListWidget#thumbs-photos { background: transparent; border: none; }
      QListWidget#thumbs-photos::item { border-radius: 4px; padding: 2px; }
      QListWidget#thumbs-photos::item:selected { background: #3a3a3a; }
      QListWidget#library-photos { background: #141414; border: 1px solid #292929; border-radius: 6px; color: #d8d8d8; }
      QListWidget#library-photos::item { border-radius: 6px; padding: 6px; }
      QListWidget#library-photos::item:selected { background: #2f2f2f; color: #ffffff; }
      QSlider::groove:horizontal { height: 4px; background: #4a4a4a; border-radius: 2px; }
      QSlider::sub-page:horizontal { background: #d0d0d0; border-radius: 2px; }
      QSlider::handle:horizontal { width: 14px; height: 14px; margin: -5px 0; background: #d9d9d9; border-radius: 7px; }
    )QSS");
  }

  static void apply(QApplication& app) { app.setStyleSheet(globalStyle()); }
};

}  // namespace pixcake::shell
