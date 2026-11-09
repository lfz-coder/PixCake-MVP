#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace pixcake::mask {

// 画笔蒙版：一串笔触。位置按画面百分比的万分之一（0..10000，横向按宽、纵向按高），笔的大小按画面长边的万分之一，
// 所以预览和导出算出同一块蒙版（官方的“大小”是图像像素，10..300；这里存成相对长边的比例，预览最长 4000 像素时
// 一个像素 = 2.5 个单位）。
struct BrushStroke {
  bool erase = false;  // 橡皮擦：把已有蒙版擦掉一部分
  int size = 225;      // 笔的直径（羽化 0 时），长边的万分之一，1..10000
  int feather = 100;   // 0..100
  int flow = 100;      // 0..100：每次落笔的强度，同一处多次叠加才累积到密度
  int density = 100;   // 0..100：这一笔的最大强度
  std::vector<std::pair<int, int>> points;  // 至少一个点
};

inline constexpr std::string_view kBrushMaskPrefix = "brush:";
inline constexpr std::size_t kMaxBrushStrokes = 4000;
inline constexpr std::size_t kMaxBrushPoints = 20000;     // 每一笔
inline constexpr std::size_t kMaxBrushTextChars = 262144;  // 整个笔触文本

// 笔触文本：笔触之间 '/'；一笔是 "<a|e><大小>,<羽化>,<流畅度>,<密度>:<x0>,<y0>,<dx>,<dy>,…"，第一个点是绝对位置，之后是相对上一点的差。
std::string encode_brush_strokes(const std::vector<BrushStroke>& strokes);
// 文本不合法（字符、字段个数、数值范围、点越出画面、笔触或点太多）时返回 false，strokes 不变。空文本是合法的空列表。
bool decode_brush_strokes(std::string_view text, std::vector<BrushStroke>& strokes);

std::string brush_mask_id(std::string_view stroke_text);
bool is_brush_mask_id(std::string_view mask_id);
// 不是 "brush:<合法非空笔触文本>" 时返回 false。
bool parse_brush_mask_id(std::string_view mask_id, std::vector<BrushStroke>& strokes);

// 笔的剖面：距离 d（像素）处的强度 0..1。直径 diameter（像素，羽化 0 时的硬边直径）和羽化 0..100：内径 =
// 直径/2·(1 − 0.25·羽化)，外径 = 直径/2·(1 + 2.45·羽化)，之间 smoothstep；羽化 100 时半高处在 1.05×直径的半径（官方实测近似，见 evidence）。
double brush_profile(double distance, double diameter, int feather);
double brush_outer_radius(double diameter, int feather);

}  // namespace pixcake::mask
