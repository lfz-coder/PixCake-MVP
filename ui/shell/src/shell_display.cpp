#include "pixcake/ui/shell/shell_display.hpp"

#include "pixcake/ui/shell/page_copy.hpp"

#include <algorithm>
#include <utility>

namespace pixcake::ui::shell {
namespace {

// 区域名取 region.<编号>，条目取文案表里的一组；条目有手册叫法时用手册叫法。
Region region(const std::string& id, RegionPlacement placement, std::string_view list = {}, bool collapsible = false) {
  Region value;
  value.id = id;
  value.label = page_text("region." + id);
  value.collapsible = collapsible;
  value.placement = placement;
  if (!list.empty()) {
    for (const auto& entry : page_list(list)) {
      value.entries.push_back(RegionEntry{entry.id, entry.guide.empty() ? entry.text : entry.guide});
    }
  }
  return value;
}

}  // namespace

const char* page_label(Page page) {
  switch (page) {
    case Page::Login:
      return page_text("page.login").c_str();
    case Page::Home:
      return page_text("page.home").c_str();
    case Page::Library:
      return page_text("page.library").c_str();
    case Page::Refine:
      return page_text("page.refine").c_str();
    case Page::Creative:
      return page_text("page.creative").c_str();
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
  notices_.clear();
  page_ = Page::Home;
  in_project_ = false;
  rebuild_regions();
  return ResultCode::Ok;
}

void ShellDisplay::post_notice(const UserNotice& notice) {
  if (!notice.text.empty()) notices_.push_back(notice.text);
}

const std::vector<std::string>& ShellDisplay::notices() const { return notices_; }

ResultCode ShellDisplay::signed_out() {
  notices_.clear();
  page_ = Page::Login;
  in_project_ = false;
  rebuild_regions();
  return ResultCode::Ok;
}

Page ShellDisplay::page() const { return page_; }
bool ShellDisplay::in_project() const { return in_project_; }
const std::vector<Region>& ShellDisplay::regions() const { return regions_; }

WindowContract ShellDisplay::contract() const {
  return WindowContract{page_text("app.title"), true, page_, in_project_, regions_};
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
  notices_.clear();
  rebuild_regions();
  return ResultCode::Ok;
}

void ShellDisplay::rebuild_regions() {
  using P = RegionPlacement;
  switch (page_) {
    case Page::Login:
      regions_ = {region("login-account", P::Center), region("login-code", P::Center, "login-code"),
                  region("login-methods", P::Center, "login-methods"), region("login-help", P::Bottom, "login-help")};
      return;
    case Page::Home:
      regions_ = {region("home-nav", P::Left, "home-nav"), region("home-import", P::Center),
                  region("home-search", P::Top),          region("home-global", P::Top, "global"),
                  region("home-common", P::Center),       region("home-quick", P::Center),
                  region("home-projects", P::Bottom)};
      return;
    case Page::Library:
      regions_ = {region("library-nav", P::Top, "library-nav"),
                  region("library-status", P::Left, "library-status"),
                  region("library-effect", P::Left),
                  region("library-thumbs", P::Bottom, "library-thumbs"),
                  region("library-delete", P::Left),
                  region("library-side", P::Right, "library-side"),
                  region("library-tools", P::Bottom, "library-tools"),
                  region("library-views", P::Bottom, "library-views")};
      return;
    case Page::Creative:
      regions_ = {region("creative-tools", P::Center, "creative-tools", true), region("creative-canvas", P::Center),
                  region("creative-thumbs", P::Bottom, "library-tools"),       region("creative-toolbox", P::Right),
                  region("creative-side", P::Center, "creative-side"),         region("creative-layers", P::Right)};
      return;
    case Page::Refine:
      regions_ = {region("refine-top", P::Top, "refine-top"),         region("refine-switch", P::Top, "page-switch"),
                  region("refine-global", P::Top, "refine-global"),   region("refine-toolbar", P::Left, "refine-tools"),
                  region("refine-canvas", P::Center, "refine-canvas"), region("refine-thumbs", P::Bottom, "refine-thumbs"),
                  region("refine-modules", P::Right)};
      return;
  }
}

const std::string& ShellDisplay::library_view() const { return library_view_; }
std::optional<int> ShellDisplay::grid_per_row() const { return grid_per_row_; }
std::optional<bool> ShellDisplay::full_effect() const { return full_effect_; }

std::optional<bool> ShellDisplay::shown(const std::string& id) const {
  if (id == "show-raw") return show_raw_;
  if (id == "show-edit") return show_edit_;
  if (id == "show-export") return show_export_;
  return std::nullopt;
}

ResultCode ShellDisplay::set_library_view(const std::string& id) {
  if (page_ != Page::Library || (id != "grid" && id != "single" && id != "filter-view")) {
    return ResultCode::StateConflict;
  }
  library_view_ = id;
  return ResultCode::Ok;
}

ResultCode ShellDisplay::set_grid_per_row(int count) {
  if (page_ != Page::Library || library_view_ != "grid") return ResultCode::StateConflict;
  grid_per_row_ = std::max(1, std::min(7, count));
  return ResultCode::Ok;
}

ResultCode ShellDisplay::set_shown(const std::string& id, bool on) {
  if (page_ != Page::Library) return ResultCode::StateConflict;
  if (id == "show-raw") {
    show_raw_ = on;
  } else if (id == "show-edit") {
    show_edit_ = on;
  } else if (id == "show-export") {
    show_export_ = on;
  } else {
    return ResultCode::StateConflict;
  }
  return ResultCode::Ok;
}

ResultCode ShellDisplay::set_full_effect(bool on) {
  if (page_ != Page::Library) return ResultCode::StateConflict;
  full_effect_ = on;
  return ResultCode::Ok;
}

}  // namespace pixcake::ui::shell
