#pragma once

#include "pixcake/exportd/image_io.hpp"
#include "pixcake/exportq/export_settings.hpp"
#include "pixcake/support/result_code.hpp"

#include <string>

namespace pixcake::exportd {

// 把一张成品（预乘 RGBA）排到一张相纸上（规则见 ExportLayout 的注释和
// docs/superpowers/evidence/2026-11-07-print-layout-observation.md）：
// - 相纸白底不透明；照片带透明度时先垫白。
// - 普通照片排版：整张照片缩放到正好放进相纸（面积平均缩小、双线性放大），居中。
// - 证件照排版：rows×cols 张原始大小的照片（格间 gap_px）拼成一整块画布，画布中心对准相纸中心，
//   相纸是这块画布上的窗口：画布大了裁、小了留白（所以 2 行时缝在纸中间，奇数行只看到中间一张）。
// - 边距是裁切窗口：窗口外变白，照片本身不动。
// 结果替换 image。
ResultCode compose_layout(const ExportLayout& layout, RgbaSource& image, std::string& diagnostic);

}  // namespace pixcake::exportd
