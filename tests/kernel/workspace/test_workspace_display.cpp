#include "pixcake/workspace/workspace_display.hpp"

#include <cstdio>
#include <string>

namespace {

int g_failures = 0;

void expect(bool cond, const char* text, int line) {
  if (!cond) {
    std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, line, text);
    ++g_failures;
  }
}

#define EXPECT(cond) expect(static_cast<bool>(cond), #cond, __LINE__)

}  // namespace

int main() {
  EXPECT(std::string(pixcake::workspace_page_label(pixcake::WorkspacePage::Login)) == "登录页面");
  EXPECT(std::string(pixcake::workspace_page_label(pixcake::WorkspacePage::Home)) == "主页面");
  EXPECT(std::string(pixcake::workspace_page_label(pixcake::WorkspacePage::Library)) == "图库");
  EXPECT(std::string(pixcake::workspace_page_label(pixcake::WorkspacePage::Refine)) == "精修");
  EXPECT(std::string(pixcake::workspace_page_label(pixcake::WorkspacePage::Creative)) == "创意");

  auto login = pixcake::WorkspaceDisplay::logged_out();
  EXPECT(login.page() == pixcake::WorkspacePage::Login);
  EXPECT(!login.in_project());
  EXPECT(login.show(pixcake::WorkspacePage::Home) == pixcake::ResultCode::StateConflict);
  EXPECT(login.page() == pixcake::WorkspacePage::Login);
  EXPECT(login.open_project() == pixcake::ResultCode::StateConflict);

  auto home = pixcake::WorkspaceDisplay::home();
  EXPECT(home.page() == pixcake::WorkspacePage::Home);
  EXPECT(home.show(pixcake::WorkspacePage::Refine) == pixcake::ResultCode::StateConflict);
  EXPECT(home.show(pixcake::WorkspacePage::Home) == pixcake::ResultCode::StateConflict);
  EXPECT(home.open_project() == pixcake::ResultCode::Ok);
  EXPECT(home.in_project());
  EXPECT(home.page() == pixcake::WorkspacePage::Refine);
  EXPECT(home.open_project() == pixcake::ResultCode::StateConflict);

  EXPECT(home.show(pixcake::WorkspacePage::Library) == pixcake::ResultCode::Ok);
  EXPECT(home.page() == pixcake::WorkspacePage::Library);
  EXPECT(home.show(pixcake::WorkspacePage::Home) == pixcake::ResultCode::StateConflict);
  EXPECT(home.page() == pixcake::WorkspacePage::Library);
  EXPECT(home.show(pixcake::WorkspacePage::Creative) == pixcake::ResultCode::Ok);
  EXPECT(home.show(pixcake::WorkspacePage::Refine) == pixcake::ResultCode::Ok);
  EXPECT(home.show(pixcake::WorkspacePage::Home) == pixcake::ResultCode::Ok);
  EXPECT(!home.in_project());
  EXPECT(home.page() == pixcake::WorkspacePage::Home);
  EXPECT(home.show(pixcake::WorkspacePage::Login) == pixcake::ResultCode::StateConflict);

  auto project = pixcake::WorkspaceDisplay::entered_project();
  EXPECT(project.page() == pixcake::WorkspacePage::Refine);
  EXPECT(project.in_project());

  if (g_failures != 0) {
    std::fprintf(stderr, "%d failure(s)\n", g_failures);
    return 1;
  }
  return 0;
}
