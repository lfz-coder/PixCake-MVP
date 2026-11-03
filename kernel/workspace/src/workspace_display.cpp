#include "pixcake/workspace/workspace_display.hpp"

#include <utility>

namespace pixcake {

namespace {

DisplayEntry item(std::string id, std::string label) {
  DisplayEntry entry;
  entry.id = std::move(id);
  entry.label = std::move(label);
  return entry;
}

DisplayRegion area(std::string id, std::string label, std::vector<DisplayEntry> entries, bool closed = true) {
  DisplayRegion region;
  region.id = std::move(id);
  region.label = std::move(label);
  region.closed = closed;
  region.entries = std::move(entries);
  return region;
}

bool library_toggle(const std::string& label) {
  return label == "显示RAW格式" || label == "显示修图状态" || label == "显示导出状态";
}

}  // namespace

const char* workspace_page_label(WorkspacePage page) {
  switch (page) {
    case WorkspacePage::Login:
      return "登录页面";
    case WorkspacePage::Home:
      return "主页面";
    case WorkspacePage::Library:
      return "图库";
    case WorkspacePage::Refine:
      return "精修";
    case WorkspacePage::Creative:
      return "创意";
  }
  return "";
}

WorkspaceDisplay WorkspaceDisplay::logged_out() { return WorkspaceDisplay{}; }

WorkspaceDisplay WorkspaceDisplay::home() {
  WorkspaceDisplay display;
  display.page_ = WorkspacePage::Home;
  return display;
}

WorkspaceDisplay WorkspaceDisplay::entered_project() {
  WorkspaceDisplay display;
  display.page_ = WorkspacePage::Refine;
  display.in_project_ = true;
  return display;
}

WorkspacePage WorkspaceDisplay::page() const { return page_; }
bool WorkspaceDisplay::in_project() const { return in_project_; }

ResultCode WorkspaceDisplay::show(WorkspacePage target) {
  if (page_ == WorkspacePage::Login || target == WorkspacePage::Login) {
    return ResultCode::StateConflict;
  }
  if (target == WorkspacePage::Home) {
    if (page_ != WorkspacePage::Refine) {
      return ResultCode::StateConflict;
    }
    in_project_ = false;
    page_ = WorkspacePage::Home;
    return ResultCode::Ok;
  }
  if (!in_project_ || (target != WorkspacePage::Library && target != WorkspacePage::Refine &&
                       target != WorkspacePage::Creative)) {
    return ResultCode::StateConflict;
  }
  page_ = target;
  return ResultCode::Ok;
}

ResultCode WorkspaceDisplay::open_project() {
  if (page_ != WorkspacePage::Home || in_project_) {
    return ResultCode::StateConflict;
  }
  in_project_ = true;
  page_ = WorkspacePage::Refine;
  return ResultCode::Ok;
}

int verification_code_digit_count() { return 6; }

std::vector<std::string> personal_center_actions() {
  return {"查看剩余张数", "查看已购买套餐", "当前软件版本", "升级软件", "退出登录"};
}

std::vector<DisplayRegion> WorkspaceDisplay::regions() const {
  if (page_ == WorkspacePage::Login) {
    return {area("login-account", "账号输入框", {}),
            area("login-code", "验证码输入框", {item("get-code", "获取验证码")}),
            area("login-methods", "登录方式切换",
                 {item("sms", "手机验证码登录"), item("wechat", "微信登录"), item("merchant", "商家账号登录")}),
            area("login-help", "帮助", {item("support", "联系售后"), item("shortcuts", "查看快捷键")}, false)};
  }
  if (page_ == WorkspacePage::Home) {
    return {area("home-nav", "导航栏",
                 {item("home", "主页"), item("toolbox", "AI工具箱"), item("local", "本地项目"), item("cloud", "云空间"),
                  item("workflow", "工作流"), item("delivery", "选片交付"), item("trash", "回收站")}),
            area("home-import", "导入照片创建新项目", {}),
            area("home-search", "搜索", {}),
            area("home-global", "全局功能区",
                 {item("support", "技术支持"), item("account", "个人中心"), item("messages", "消息中心"),
                  item("transfers", "传输列表"), item("export-list", "导出列表")}),
            area("home-common", "常用功能", {}),
            area("home-quick", "快捷功能", {}),
            area("home-projects", "项目列表", {})};
  }
  if (page_ == WorkspacePage::Library) {
    return {area("library-status", "显示状态",
                 {item("show-raw", "显示RAW格式"), item("show-edit", "显示修图状态"), item("show-export", "显示导出状态")}),
            area("library-effect", "全效果模式", {}),
            area("library-thumbs", "缩略图区",
                 {item("raw", "RAW图"), item("exported", "已导出"), item("edited", "已编辑"), item("flag", "旗标"),
                  item("star", "星标"), item("color", "色标")}),
            area("library-delete", "批量删除", {}),
            area("library-side", "侧边栏", {item("cull", "AI挑图"), item("preset", "预设"), item("meta", "元数据")}),
            area("library-tools", "导入新图片/筛选/排序",
                 {item("import", "导入新图片"), item("filter", "筛选"), item("sort", "排序")}),
            area("library-views", "视图切换",
                 {item("grid", "网格视图"), item("single", "单视图"), item("filter-view", "筛选视图")})};
  }
  if (page_ == WorkspacePage::Creative) {
    return {area("creative-tools", "手动工具栏",
                 {item("zoom", "显示比例调整"), item("screen", "更改屏幕模式"), item("hand", "抓手"),
                  item("move", "移动图层"), item("scale", "缩放工具"), item("liquify", "液化"),
                  item("spot", "污点修复画笔"), item("patch", "修补工具"), item("clone", "仿制图章"),
                  item("marquee", "矩形选区工具&椭圆选区工具"), item("lasso", "套索工具&选区画笔工具"),
                  item("crop", "裁切")},
                 false),
            area("creative-canvas", "画布", {}),
            area("creative-thumbs", "缩略图区",
                 {item("import", "导入新图片"), item("filter", "筛选"), item("sort", "排序")}),
            area("creative-toolbox", "AI工具箱", {}),
            area("creative-side", "侧边栏",
                 {item("ai", "AI工具"), item("history", "历史记录"), item("batch", "批量设计"), item("help", "帮助"),
                  item("guide", "新手引导"), item("feedback", "反馈")}),
            area("creative-layers", "图层面板", {})};
  }
  return {area("refine-top", "返回主页面、搜索和像素助手",
               {item("back", "返回主页面"), item("search", "搜索"), item("assistant", "像素助手")}),
          area("refine-switch", "图库/精修/创意页面切换",
               {item("library", "图库"), item("refine", "精修"), item("creative", "创意")}),
          area("refine-global", "全局功能区",
               {item("support", "技术支持"), item("account", "个人中心"), item("messages", "消息中心"),
                item("transfers", "传输列表"), item("export-list", "导出列表"), item("export", "导出")}),
          area("refine-toolbar", "工具栏",
               {item("zoom", "显示比例调整"), item("hand", "抓手"), item("fringe", "祛边"), item("guides", "参考线"),
                item("liquify", "液化"), item("spot", "污点修复"), item("patch", "修补"), item("clone", "仿制图章"),
                item("remove", "智能消除"), item("point", "AI点修"), item("reset", "重置效果"), item("preset", "预设")}),
          area("refine-canvas", "画布", {item("compare", "查看对比")}),
          area("refine-thumbs", "缩略图区",
               {item("add", "+"), item("filter", "筛选"), item("sort", "排序"), item("cull", "去挑图"),
                item("star", "星标"), item("flag", "旗标"), item("color", "色标"), item("single", "单视图"),
                item("compare-view", "对比视图"), item("reference", "参考视图")}),
          area("refine-modules", "功能区", {})};
}

std::string WorkspaceDisplay::library_view() const { return library_view_; }
std::optional<int> WorkspaceDisplay::grid_per_row() const { return grid_per_row_; }

std::optional<bool> WorkspaceDisplay::shown(const std::string& label) const {
  if (label == "显示RAW格式") {
    return show_raw_;
  }
  if (label == "显示修图状态") {
    return show_edit_;
  }
  if (label == "显示导出状态") {
    return show_export_;
  }
  return std::nullopt;
}

std::optional<bool> WorkspaceDisplay::full_effect() const { return full_effect_; }

ResultCode WorkspaceDisplay::set_library_view(const std::string& view) {
  if (page_ != WorkspacePage::Library) {
    return ResultCode::StateConflict;
  }
  if (view != "网格视图" && view != "单视图" && view != "筛选视图") {
    return ResultCode::StateConflict;
  }
  library_view_ = view;
  return ResultCode::Ok;
}

ResultCode WorkspaceDisplay::set_grid_per_row(int count) {
  if (page_ != WorkspacePage::Library || library_view_ != "网格视图" || count < 1 || count > 7) {
    return ResultCode::StateConflict;
  }
  grid_per_row_ = count;
  return ResultCode::Ok;
}

ResultCode WorkspaceDisplay::set_shown(const std::string& label, bool on) {
  if (page_ != WorkspacePage::Library || !library_toggle(label)) {
    return ResultCode::StateConflict;
  }
  if (label == "显示RAW格式") {
    show_raw_ = on;
  } else if (label == "显示修图状态") {
    show_edit_ = on;
  } else {
    show_export_ = on;
  }
  return ResultCode::Ok;
}

ResultCode WorkspaceDisplay::set_full_effect(bool on) {
  if (page_ != WorkspacePage::Library) {
    return ResultCode::StateConflict;
  }
  full_effect_ = on;
  return ResultCode::Ok;
}

}  // namespace pixcake
