// 壳层源码门禁（代码质量清单 Q-11、Q-12）：
// - apps/shell 与 ui 的源码没有超过 160 字符的行；
// - 页面文字单一来源：apps/shell 不写中文文案；ui 与 kernel/workspace 里同一段文案只出现在一个文件；
// - 页面构建文件只画控件，不改显示状态、不调命令入口（状态变化经 ShellController）。
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#ifndef PIXCAKE_SOURCE_DIR
#error "PIXCAKE_SOURCE_DIR must be defined"
#endif

namespace {

namespace fs = std::filesystem;
int g_failures = 0;

void fail(const std::string& message) {
  std::fprintf(stderr, "FAIL %s\n", message.c_str());
  ++g_failures;
}

std::vector<fs::path> sources(const std::vector<std::string>& roots) {
  std::vector<fs::path> out;
  for (const auto& root : roots) {
    const fs::path base = fs::path(PIXCAKE_SOURCE_DIR) / root;
    if (!fs::exists(base)) continue;
    for (const auto& entry : fs::recursive_directory_iterator(base)) {
      const auto ext = entry.path().extension();
      if (entry.is_regular_file() && (ext == ".cpp" || ext == ".hpp")) out.push_back(entry.path());
    }
  }
  return out;
}

std::string read(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  std::ostringstream text;
  text << in.rdbuf();
  return text.str();
}

std::size_t code_points(const std::string& line) {
  std::size_t count = 0;
  for (const unsigned char c : line) {
    if ((c & 0xC0) != 0x80) ++count;
  }
  return count;
}

// 跳过注释与字符字面量，取出字符串字面量（只处理本仓库用到的普通字面量）。
std::vector<std::string> string_literals(const std::string& text) {
  std::vector<std::string> out;
  for (std::size_t i = 0; i < text.size(); ++i) {
    if (text.compare(i, 2, "//") == 0) {
      i = text.find('\n', i);
      if (i == std::string::npos) break;
    } else if (text.compare(i, 2, "/*") == 0) {
      i = text.find("*/", i + 2);
      if (i == std::string::npos) break;
      ++i;
    } else if (text[i] == '\'') {
      for (++i; i < text.size() && text[i] != '\''; ++i) {
        if (text[i] == '\\') ++i;
      }
    } else if (text[i] == '"') {
      std::string literal;
      for (++i; i < text.size() && text[i] != '"'; ++i) {
        if (text[i] == '\\') literal += text[i++];
        literal += text[i];
      }
      out.push_back(literal);
    }
  }
  return out;
}

bool non_ascii(const std::string& text) {
  for (const unsigned char c : text) {
    if (c >= 0x80) return true;
  }
  return false;
}

std::string repo_path(const fs::path& path) { return fs::relative(path, PIXCAKE_SOURCE_DIR).string(); }

void test_q11_shell_lines_within_160() {
  for (const auto& path : sources({"apps/shell", "ui"})) {
    std::istringstream lines(read(path));
    std::string line;
    int number = 0;
    while (std::getline(lines, line)) {
      ++number;
      if (code_points(line) > 160) fail(repo_path(path) + ":" + std::to_string(number) + " longer than 160");
    }
  }
}

void test_q11_page_copy_single_source() {
  for (const auto& path : sources({"apps/shell"})) {
    for (const auto& literal : string_literals(read(path))) {
      if (non_ascii(literal)) fail(repo_path(path) + " writes page copy \"" + literal + "\"; take it from ui/shell");
    }
  }
  std::map<std::string, std::set<std::string>> owners;
  for (const auto& path : sources({"ui", "kernel/workspace"})) {
    for (const auto& literal : string_literals(read(path))) {
      if (non_ascii(literal)) owners[literal].insert(repo_path(path));
    }
  }
  for (const auto& [literal, files] : owners) {
    if (files.size() < 2) continue;
    std::string where;
    for (const auto& file : files) where += " " + file;
    fail("\"" + literal + "\" is written in" + where);
  }
}

// 快捷键只有一个来源：ui/shell 的快捷键表。apps/shell 里不写按键字面量（Ctrl+、Shift+、Alt+、Meta+ 开头的字符串）。
void test_shortcuts_come_from_the_table() {
  for (const auto& path : sources({"apps/shell"})) {
    for (const auto& literal : string_literals(read(path))) {
      for (const char* prefix : {"Ctrl+", "Shift+", "Alt+", "Meta+", "Cmd+"}) {
        if (literal.rfind(prefix, 0) == 0) {
          fail(repo_path(path) + " writes the key \"" + literal + "\"; take it from the shortcut table");
        }
      }
    }
  }
}

void test_q12_pages_only_draw() {
  const std::vector<std::string> forbidden = {".navigate(", ".open_project(", ".signed_in(", ".signed_out(",
                                              ".post_notice(", "CommandGate", "BusinessCommands", "ShellDisplay&"};
  int pages = 0;
  for (const auto& path : sources({"apps/shell"})) {
    const std::string name = path.filename().string();
    if (name.size() < 9 || name.compare(name.size() - 9, 9, "_page.cpp") != 0) continue;
    ++pages;
    const std::string text = read(path);
    for (const auto& token : forbidden) {
      std::size_t at = 0;
      while ((at = text.find(token, at)) != std::string::npos) {
        const bool const_ref = token == "ShellDisplay&" && at >= 6 && text.compare(at - 6, 6, "const ") == 0;
        if (!const_ref) fail(repo_path(path) + " uses " + token + "; pages draw, ShellController changes state");
        at += token.size();
      }
    }
  }
  if (pages != 6) fail("expected one page file per page (login, home, local-projects, library, refine, creative), found " +
                       std::to_string(pages));
}

}  // namespace

int main() {
  test_q11_shell_lines_within_160();
  test_q11_page_copy_single_source();
  test_shortcuts_come_from_the_table();
  test_q12_pages_only_draw();
  if (g_failures != 0) {
    std::fprintf(stderr, "%d failure(s)\n", g_failures);
    return 1;
  }
  return 0;
}
