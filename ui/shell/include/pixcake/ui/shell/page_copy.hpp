#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace pixcake::ui::shell {

// 页面文字的唯一来源（代码质量清单 Q-11）。壳层（apps/shell）不写中文文案，按 ASCII 键取用；
// 显示模型（ShellDisplay）的区域名和条目也从这里取。精修面板滑杆的显示名在 ui/panels。
struct CopyItem {
  std::string id;           // 条目编号，ASCII
  std::string text;         // 界面上显示的文字
  std::string guide;        // 官方手册里的叫法；与 text 不同时才填，显示模型的区域条目用它
  std::string accessible;   // 无障碍名；与 text 不同时才填
  std::string description;  // 无障碍说明
};

// 快捷键表（需求 §23.1）：壳层（apps/shell）不写按键，按编号从这里取。key 是 Qt 的写法（Ctrl 在 macOS 上就是
// Cmd）。scope 说明在哪一页生效：global（所有页）、home、project（图库和精修）、library、refine。
// 同一作用范围里不能有两个功能用同一个键（test_shell_display 检查）；Cmd+N 按页面分流（需求 §2.3 第 5 条）。
struct ShortcutEntry {
  std::string id;
  std::string key;
  std::string name;
  std::string scope;
};
const std::vector<ShortcutEntry>& shortcut_table();
// 找不到编号返回空串，并记进 missing_page_copy()。
const std::string& shortcut_key(std::string_view id);

// 找不到的键返回空串或空列表，并记进 missing_page_copy()；冒烟检查要求它为空。
const std::string& page_text(std::string_view key);
const std::vector<CopyItem>& page_list(std::string_view key);
const CopyItem& page_item(std::string_view list, std::string_view id);
std::vector<std::string> missing_page_copy();

}  // namespace pixcake::ui::shell
