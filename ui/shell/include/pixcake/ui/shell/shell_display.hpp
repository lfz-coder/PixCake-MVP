#pragma once

#include "pixcake/support/result_code.hpp"

#include <string>
#include <vector>

namespace pixcake::ui::shell {

// These are page identities only. Rendering and business actions stay outside
// this contract; the shell only describes what the window should expose.
enum class Page { Login, Home, Library, Refine, Creative };
enum class RegionPlacement { Top, Left, Center, Right, Bottom };

const char* page_label(Page page);

struct RegionEntry {
  std::string id;
  std::string label;
};

struct Region {
  std::string id;
  std::string label;
  bool collapsible = false;
  RegionPlacement placement = RegionPlacement::Center;
  std::vector<RegionEntry> entries;
};

struct WindowContract {
  std::string title = "像素蛋糕";
  bool single_instance = true;
  Page page = Page::Login;
  bool in_project = false;
  std::vector<Region> regions;
};

class ShellDisplay {
 public:
  static ShellDisplay logged_out();
  static ShellDisplay home();
  static ShellDisplay entered_project();

  Page page() const;
  bool in_project() const;
  WindowContract contract() const;
  const std::vector<Region>& regions() const;

  // Navigation is the only state-changing operation in this batch.
  ResultCode open_project();
  ResultCode navigate(Page target);

 private:
  Page page_ = Page::Login;
  bool in_project_ = false;
  std::vector<Region> regions_;

  void rebuild_regions();
};

}  // namespace pixcake::ui::shell
