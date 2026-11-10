#pragma once
// 点曲线参数的文本格式与校验。
// Validation ported from PhotoCraft crates/algo/src/camera_raw.rs validate_curve
// @ 885a6ddf27cc58c07faf1722e5c327cd38953657, MIT (elected from MIT OR Apache-2.0).
// Copyright (c) 2026 ArtCraft Team and the PhotoCraft contributors.
// Licence text: third_party/photocraft-LICENSES/LICENSE-MIT.
//
// 文本是 "x,y;x,y;..."：[输入, 输出]，0..255，十进制数，没有空格。合格的曲线有 2 到 16 个点
// （kMaxCurvePoints，PhotoCraft MAX_CURVE_POINTS），坐标有限且在 0..255，输入按顺序至少递增 1 级。
// 线性（没有曲线）就是不写这个参数。
#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace pixcake {

constexpr std::size_t kMaxCurvePoints = 16;

// 参数曲线以外的点曲线参数（color.global 的 curve_points_rgb/red/green/blue）。
bool is_curve_points_parameter(std::string_view parameter_id);
// 解析并按 validate_curve 校验；不合格时返回 false，out 内容不确定。
bool decode_curve_points(std::string_view text, std::vector<std::array<float, 2>>& out);
std::string encode_curve_points(const std::vector<std::array<float, 2>>& points);

}  // namespace pixcake
