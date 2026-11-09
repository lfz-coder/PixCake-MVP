#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace pixcake::mask {

// 一个蒙版由多个子项组合：每项是语义部位或渐变的蒙版编号，按“添加”（并集）或“减去”依次叠上。编号
// "combined:+<子项编号>|-<子项编号>|…"，子项编号本身不含 '|'。组合的像素由 gpu/render 在子项蒙版都有之后生成。
struct CombinedPart {
  bool subtract = false;
  std::string id;
};

inline constexpr std::string_view kCombinedMaskPrefix = "combined:";

std::string combined_mask_id(const std::vector<CombinedPart>& parts);
bool is_combined_mask_id(std::string_view mask_id);
// 编号不合法（没有子项、空子项编号、缺少符号）时返回 false，parts 不变。
bool parse_combined_mask_id(std::string_view mask_id, std::vector<CombinedPart>& parts);
// 绘制步骤引用的蒙版编号对应的“叶子”编号：组合编号展开成子项编号，其余原样返回一个。
std::vector<std::string> leaf_mask_ids(std::string_view mask_id);

}  // namespace pixcake::mask
