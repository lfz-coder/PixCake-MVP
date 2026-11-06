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

// 找不到的键返回空串或空列表，并记进 missing_page_copy()；冒烟检查要求它为空。
const std::string& page_text(std::string_view key);
const std::vector<CopyItem>& page_list(std::string_view key);
const CopyItem& page_item(std::string_view list, std::string_view id);
std::vector<std::string> missing_page_copy();

}  // namespace pixcake::ui::shell
