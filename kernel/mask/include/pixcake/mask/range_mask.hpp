#pragma once

#include <string>
#include <string_view>

namespace pixcake::mask {

// 颜色范围、亮度范围蒙版：像素由画面本身决定，所以编号只带取样值，和分辨率无关。像素由 gpu/render 在画幅
// （几何变换之后）确定、原图可读时生成。
//
// 颜色范围 "colorrange:<r>:<g>:<b>:<调整>"：取样色 RGB（0..255）和调整（1..100）。选区强度 =
// clamp(1 − 到取样色的 Lab 距离 / T，0，1)，T = 18 + 0.4 × 调整（官方实测近似，见 2026-11-08-range-mask-observation.md）。
struct ColorRangeMask {
  int r = 128;
  int g = 128;
  int b = 128;
  int adjust = 50;  // 1..100
};

// 亮度范围 "lumrange:<a>:<b>:<c>:<d>"：亮度（0..100，Rec.709 权重作用在编码值上）的梯形：a 以下不选，a→b 线性上升，
// b→c 全选，c→d 线性下降，d 以上不选；要求 a ≤ b ≤ c ≤ d。
struct LuminanceRangeMask {
  int a = 20;
  int b = 45;
  int c = 65;
  int d = 90;
};

inline constexpr std::string_view kColorRangePrefix = "colorrange:";
inline constexpr std::string_view kLuminanceRangePrefix = "lumrange:";

std::string color_range_mask_id(const ColorRangeMask& range);
std::string luminance_range_mask_id(const LuminanceRangeMask& range);
bool is_color_range_mask_id(std::string_view mask_id);
bool is_luminance_range_mask_id(std::string_view mask_id);
// 编号不合法（字段个数、数值范围、梯形次序）时返回 false，参数不变。
bool parse_color_range_mask_id(std::string_view mask_id, ColorRangeMask& range);
bool parse_luminance_range_mask_id(std::string_view mask_id, LuminanceRangeMask& range);

// 颜色范围的过渡距离 T（Lab 单位）。
double color_range_reach(int adjust);

}  // namespace pixcake::mask
