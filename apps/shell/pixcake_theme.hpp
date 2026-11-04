#pragma once

#include <QApplication>
#include <QColor>
#include <QFont>
#include <QString>

namespace pixcake::shell {

struct PixCakeTheme final {
  static QColor workspace() { return QColor("#080808"); }
  static QColor panel() { return QColor("#111111"); }
  static QColor panelRaised() { return QColor("#171717"); }
  static QColor field() { return QColor("#0b0b0b"); }
  static QColor border() { return QColor("#3b3b3b"); }
  static QColor text() { return QColor("#f3f3f3"); }
  static QColor muted() { return QColor("#9c9c9c"); }
  static QColor accent() { return QColor("#ffe000"); }
  static QColor accentMuted() { return QColor("#5c4e0b"); }
  static QString globalStyle() {
    return QStringLiteral(R"QSS(
      QWidget { color: #f3f3f3; font-family: "PingFang SC", "Helvetica Neue", sans-serif; font-size: 13px; }
      QMainWindow, QWidget#shell-root { background: #080808; }
      QFrame#home-sidebar, QFrame#home-content, QFrame#home-toolbar { background: #111111; }
      QFrame#home-card { background: #171717; border: 1px solid #2c2c2c; border-radius: 9px; }
      QFrame#home-import { background: #141414; border: 1px dashed #5a5a5a; border-radius: 9px; }
      QLabel#home-brand { font-size: 20px; font-weight: 600; }
      QLabel#home-section-title { font-size: 15px; font-weight: 600; }
      QLabel#home-card-title { font-size: 15px; font-weight: 600; }
      QPushButton#home-nav { text-align: left; padding: 9px 12px; border: none; background: transparent; }
      QPushButton#home-nav:hover, QPushButton#home-nav[selected="true"] { background: #3b3b3b; border-radius: 6px; }
      QPushButton#home-toolbar { min-width: 34px; max-width: 34px; padding: 4px; }
      QFrame#workspace-top, QFrame#workspace-tools, QFrame#workspace-thumbs, QFrame#workspace-side { background: #111111; }
      QFrame#workspace-canvas { background: #202020; border: 1px solid #303030; border-radius: 4px; }
      QFrame#workspace-tool-button { background: #191919; border: 1px solid #2e2e2e; border-radius: 5px; }
      QFrame#workspace-tool-button:hover { border-color: #ffe000; }
      QLabel#workspace-tool-label { color: #dddddd; }
      QFrame#library-grid, QFrame#creative-canvas { background: #242424; border: 1px solid #383838; border-radius: 4px; }
      QFrame#library-thumb { background: #1b1b1b; border: 1px solid #383838; min-height: 88px; }
      QFrame#library-thumb:hover { border-color: #ffe000; }
      QGroupBox { background: #111111; border: 1px solid #3b3b3b; border-radius: 8px; margin-top: 14px; padding: 18px 12px 12px 12px; }
      QGroupBox::title { subcontrol-origin: margin; left: 12px; padding: 0 5px; color: #f3f3f3; }
      QLabel { background: transparent; }
      QLabel#muted, QLabel[muted="true"] { color: #9c9c9c; }
      QLineEdit { background: #0b0b0b; border: 1px solid #4a4a4a; border-radius: 3px; color: #f3f3f3; padding: 0 10px; min-height: 38px; }
      QLineEdit:focus { border: 1px solid #ffe000; }
      QPushButton { background: #191919; border: 1px solid #3b3b3b; border-radius: 5px; color: #f3f3f3; padding: 7px 12px; min-height: 28px; }
      QPushButton:hover { border-color: #ffe000; }
      QPushButton:disabled { color: #757575; border-color: #2c2c2c; }
      QPushButton#primary { background: #ffe000; color: #121212; border-color: #ffe000; min-height: 40px; }
      QPushButton#primary:disabled { background: #5c4e0b; color: #2d2d2d; border-color: #5c4e0b; }
      QPushButton#icon-button { min-width: 38px; max-width: 38px; padding: 4px; }
      QCheckBox { spacing: 8px; color: #dddddd; }
      QCheckBox::indicator { width: 15px; height: 15px; border: 1px solid #777; background: #0b0b0b; }
      QScrollArea { border: none; background: #080808; }
      QSlider::groove:horizontal { height: 3px; background: #444; }
      QSlider::handle:horizontal { width: 12px; margin: -5px 0; background: #ffe000; border-radius: 6px; }
    )QSS");
  }
  static void apply(QApplication& app) { app.setStyleSheet(globalStyle()); }
};

} // namespace pixcake::shell
