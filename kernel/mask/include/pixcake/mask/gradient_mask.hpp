#pragma once

#include <string>
#include <string_view>

namespace pixcake::mask {

// 渐变蒙版（线性、径向）：位置是画面百分比，和分辨率无关，所以预览和导出算出同一块蒙版。蒙版编号
// "gradient:<linear|radial>:x:y:angle:size:feather"，整数。像素由 gpu/render 在画幅确定后生成。
struct GradientMask {
  bool radial = false;
  int x = 50;         // 中心横向，0..100
  int y = 50;         // 中心纵向，0..100
  int angle = 0;      // 线性渐变的方向，-180..180 度
  int size = 50;      // 线性：过渡带宽度；径向：半径（0..100，画面对角线的百分比）
  int feather = 50;   // 线性：过渡曲线的柔和度；径向：边缘柔和带占半径的百分比
};

inline constexpr std::string_view kGradientMaskPrefix = "gradient:";

std::string gradient_mask_id(const GradientMask& gradient);
bool is_gradient_mask_id(std::string_view mask_id);
// 编号不合法（形状、字段个数、数值范围）时返回 false，gradient 不变。
bool parse_gradient_mask_id(std::string_view mask_id, GradientMask& gradient);

}  // namespace pixcake::mask
