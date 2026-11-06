#include "pixcake/ui/shell/shell_display.hpp"

#include <utility>

namespace pixcake::ui::shell {
namespace {

RegionEntry entry(std::string id, std::string label) {
  return RegionEntry{std::move(id), std::move(label)};
}

Region region(std::string id, std::string label, std::vector<RegionEntry> entries = {}, bool collapsible = false,
              RegionPlacement placement = RegionPlacement::Center) {
  return Region{std::move(id), std::move(label), collapsible, placement, std::move(entries)};
}

}  // namespace

const char* page_label(Page page) {
  switch (page) {
    case Page::Login:
      return "登录页面";
    case Page::Home:
      return "主页面";
    case Page::Library:
      return "图库";
    case Page::Refine:
      return "精修";
    case Page::Creative:
      return "创意";
  }
  return "";
}

ShellDisplay ShellDisplay::logged_out() {
  ShellDisplay display;
  display.rebuild_regions();
  return display;
}

ShellDisplay ShellDisplay::for_session(const SessionState& session) {
  ShellDisplay display;
  if (session.signed_in()) display.page_ = Page::Home;
  display.rebuild_regions();
  return display;
}

ShellDisplay ShellDisplay::dev_preview(Page page) {
  ShellDisplay display;
  display.page_ = page == Page::Login ? Page::Login : Page::Home;
  if (page != Page::Login && page != Page::Home) {
    display.page_ = page;
    display.in_project_ = true;
  }
  display.rebuild_regions();
  return display;
}

ResultCode ShellDisplay::signed_in(const SessionState& session) {
  if (page_ != Page::Login || !session.signed_in()) return ResultCode::StateConflict;
  page_ = Page::Home;
  in_project_ = false;
  rebuild_regions();
  return ResultCode::Ok;
}

ResultCode ShellDisplay::signed_out() {
  page_ = Page::Login;
  in_project_ = false;
  rebuild_regions();
  return ResultCode::Ok;
}

Page ShellDisplay::page() const { return page_; }
bool ShellDisplay::in_project() const { return in_project_; }
const std::vector<Region>& ShellDisplay::regions() const { return regions_; }

WindowContract ShellDisplay::contract() const {
  return WindowContract{"像素蛋糕", true, page_, in_project_, regions_};
}

ResultCode ShellDisplay::open_project() {
  if (page_ != Page::Home || in_project_) {
    return ResultCode::StateConflict;
  }
  page_ = Page::Refine;
  in_project_ = true;
  rebuild_regions();
  return ResultCode::Ok;
}

ResultCode ShellDisplay::navigate(Page target) {
  if (page_ == Page::Login || target == Page::Login) {
    return ResultCode::StateConflict;
  }
  if (target == Page::Home) {
    if (!in_project_) {
      return ResultCode::StateConflict;
    }
    page_ = Page::Home;
    in_project_ = false;
    rebuild_regions();
    return ResultCode::Ok;
  }
  if (!in_project_ || (target != Page::Library && target != Page::Refine && target != Page::Creative)) {
    return ResultCode::StateConflict;
  }
  page_ = target;
  rebuild_regions();
  return ResultCode::Ok;
}

void ShellDisplay::rebuild_regions() {
  if (page_ == Page::Login) {
    regions_ = {
        region("login-account", "账号输入框", {}, false, RegionPlacement::Center),
        region("login-code", "验证码输入框", {entry("get-code", "获取验证码")}, false, RegionPlacement::Center),
        region("login-methods", "登录方式切换",
               {entry("sms", "手机验证码登录"), entry("wechat", "微信登录"), entry("merchant", "商家账号登录")}, false, RegionPlacement::Center),
        region("login-help", "帮助", {entry("support", "联系售后"), entry("shortcuts", "查看快捷键")}, false, RegionPlacement::Bottom),
    };
    return;
  }

  if (page_ == Page::Home) {
    regions_ = {
        region("home-nav", "导航栏",
               {entry("home", "主页"), entry("toolbox", "AI工具箱"), entry("local", "本地项目"),
                entry("cloud", "云空间"), entry("workflow", "工作流"), entry("delivery", "选片交付"),
                entry("trash", "回收站")}, false, RegionPlacement::Left),
        region("home-import", "导入照片创建新项目", {}, false, RegionPlacement::Center),
        region("home-search", "搜索", {}, false, RegionPlacement::Top),
        region("home-global", "全局功能区",
              {entry("support", "技术支持"), entry("account", "个人中心"), entry("messages", "消息中心"),
              entry("transfers", "传输列表"), entry("export-list", "导出列表")}, false, RegionPlacement::Top),
        region("home-common", "常用功能", {}, false, RegionPlacement::Center),
        region("home-quick", "快捷功能", {}, false, RegionPlacement::Center),
        region("home-projects", "项目列表", {}, false, RegionPlacement::Bottom),
    };
    return;
  }

  if (page_ == Page::Library) {
    regions_ = {
        region("library-nav", "项目内导航", {entry("home", "首页"), entry("search", "搜索"), entry("library", "图库"),
                                                entry("refine", "精修"), entry("creative", "创意")}, false, RegionPlacement::Top),
        region("library-status", "显示状态",
               {entry("show-raw", "显示RAW格式"), entry("show-edit", "显示修图状态"),
                entry("show-export", "显示导出状态")}, false, RegionPlacement::Left),
        region("library-effect", "全效果模式", {}, false, RegionPlacement::Left),
        region("library-thumbs", "缩略图区",
               {entry("raw", "RAW图"), entry("exported", "已导出"), entry("edited", "已编辑"),
                entry("flag", "旗标"), entry("star", "星标"), entry("color", "色标")}, false, RegionPlacement::Bottom),
        region("library-delete", "批量删除", {}, false, RegionPlacement::Left),
        region("library-side", "侧边栏", {entry("cull", "AI挑图"), entry("preset", "预设"), entry("meta", "元数据")}, false, RegionPlacement::Right),
        region("library-tools", "导入新图片/筛选/排序",
               {entry("import", "导入新图片"), entry("filter", "筛选"), entry("sort", "排序")}, false, RegionPlacement::Bottom),
        region("library-views", "视图切换", {entry("grid", "网格视图"), entry("single", "单视图"),
                                               entry("filter-view", "筛选视图")}, false, RegionPlacement::Bottom),
    };
    return;
  }

  if (page_ == Page::Creative) {
    regions_ = {
        region("creative-tools", "手动工具栏",
               {entry("zoom", "显示比例调整"), entry("screen", "更改屏幕模式"), entry("hand", "抓手"),
                entry("move", "移动图层"), entry("scale", "缩放工具"), entry("liquify", "液化"),
                entry("spot", "污点修复画笔"), entry("patch", "修补工具"), entry("clone", "仿制图章"),
                entry("marquee", "矩形选区工具&椭圆选区工具"), entry("lasso", "套索工具&选区画笔工具"),
                entry("crop", "裁切")}, true),
        region("creative-canvas", "画布", {}, false, RegionPlacement::Center),
        region("creative-thumbs", "缩略图区",
               {entry("import", "导入新图片"), entry("filter", "筛选"), entry("sort", "排序")}, false, RegionPlacement::Bottom),
        region("creative-toolbox", "AI工具箱", {}, false, RegionPlacement::Right),
        region("creative-side", "侧边栏",
               {entry("ai", "AI工具"), entry("history", "历史记录"), entry("batch", "批量设计"),
                entry("help", "帮助"), entry("guide", "新手引导"), entry("feedback", "反馈")}),
        region("creative-layers", "图层面板", {}, false, RegionPlacement::Right),
    };
    return;
  }

  regions_ = {
      region("refine-top", "返回主页面、搜索和像素助手",
             {entry("back", "返回主页面"), entry("search", "搜索"), entry("assistant", "像素助手")}, false, RegionPlacement::Top),
      region("refine-switch", "图库/精修/创意页面切换",
             {entry("library", "图库"), entry("refine", "精修"), entry("creative", "创意")}, false, RegionPlacement::Top),
      region("refine-global", "全局功能区",
             {entry("support", "技术支持"), entry("account", "个人中心"), entry("messages", "消息中心"),
              entry("transfers", "传输列表"), entry("export-list", "导出列表"), entry("export", "导出")}, false, RegionPlacement::Top),
      region("refine-toolbar", "工具栏",
             {entry("zoom", "显示比例调整"), entry("hand", "抓手"), entry("fringe", "祛边"), entry("guides", "参考线"),
              entry("liquify", "液化"), entry("spot", "污点修复"), entry("patch", "修补"), entry("clone", "仿制图章"),
              entry("remove", "智能消除"), entry("point", "AI点修"), entry("reset", "重置效果"), entry("preset", "预设")}, false, RegionPlacement::Left),
      region("refine-canvas", "画布", {entry("compare", "查看对比")}, false, RegionPlacement::Center),
      region("refine-thumbs", "缩略图区",
             {entry("add", "+"), entry("filter", "筛选"), entry("sort", "排序"), entry("cull", "去挑图"),
              entry("star", "星标"), entry("flag", "旗标"), entry("color", "色标"), entry("single", "单视图"),
              entry("compare-view", "对比视图"), entry("reference", "参考视图")}, false, RegionPlacement::Bottom),
      region("refine-modules", "功能区", {}, false, RegionPlacement::Right),
  };
}

}  // namespace pixcake::ui::shell
