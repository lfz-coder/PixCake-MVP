#pragma once

#include "pixcake/support/result_code.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace pixcake {

// 原始格式、JPG、TIFF8位、TIFF16位（2026-11-06 在 test 版导出弹窗读到的四项，
// docs/superpowers/evidence/2026-11-06-export-dialog-observation.md）。
enum class ExportFormat { Original, Jpg, Tiff, Tiff16 };

// 水印的一套摆放参数（test 版实测，docs/superpowers/evidence/2026-11-07-watermark-observation.md）：
// 素材短边 = 图片长边 × size%（旋转前），再顺时针转 quarter_turns×90°；fit 时按比例缩放到正好放进画面、
// size 不用；anchor 是九宫格（0 左上 … 2 右上 … 8 右下）；x、y 是到贴靠边的距离，占图宽、图高的百分比，
// 正数往画面里。
struct WatermarkPlacement {
  int size = 10;           // 0..100
  bool fit = false;
  int opacity = 100;       // 0..100
  int anchor = 2;          // 0..8，默认右上
  int x = 3;               // -100..100
  int y = 3;               // -100..100
  int quarter_turns = 0;   // 0..3，顺时针
  bool operator==(const WatermarkPlacement& other) const {
    return size == other.size && fit == other.fit && opacity == other.opacity && anchor == other.anchor &&
           x == other.x && y == other.y && quarter_turns == other.quarter_turns;
  }
};

// 横图、竖图、方图各一套（test 版按页签分别记住），素材三者共用。
enum class WatermarkShape { Landscape = 0, Portrait = 1, Square = 2 };
WatermarkShape watermark_shape(std::uint32_t width, std::uint32_t height);

struct ExportWatermark {
  // 素材在 <数据目录>/exports/watermarks/ 里的文件名：16 位小写十六进制 + '-' + 字节数 + ".png"/".jpg"。
  std::string material;
  std::array<WatermarkPlacement, 3> placements;  // 按 WatermarkShape 下标
  bool operator==(const ExportWatermark& other) const {
    return material == other.material && placements == other.placements;
  }
};
bool valid_watermark_material(const std::string& name);

// Where a watermark lands on an image (pixels; may extend past the edges,
// which clip).  Empty when the size is 0 or the material is empty.
struct WatermarkRect {
  std::int64_t left = 0;
  std::int64_t top = 0;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
};

// The test build's rules (docs/superpowers/evidence/2026-11-07-watermark-observation.md):
// the material's short edge before rotation is the image's long edge x size%;
// rotation turns it clockwise in 90 degree steps; "fit" scales it to fit
// inside the image instead; the anchor (3x3) picks the edge or corner it sits
// against and x / y are its distances from those edges in percent of the
// image width / height, positive inwards.  A watermark as wide (tall) as the
// image under "fit" ignores x (y), as observed.  The centre column (row) moves
// right (down) for positive x (y): not observed, chosen here.
WatermarkRect place_watermark(std::uint32_t material_width, std::uint32_t material_height,
                              const WatermarkPlacement& placement, std::uint32_t image_width,
                              std::uint32_t image_height);


// 照片排版（排版印刷页，test 版实测 docs/superpowers/evidence/2026-11-07-print-layout-observation.md）：
// 一张成品是一张相纸（paper_mm 是已经按方向摆好的宽×高，dpi 换成像素）。
// 普通照片排版：整张照片缩放到正好放进相纸（不裁），居中；
// 证件照排版：rows×cols 格，格间 gap_px 像素，每格里照片按原始像素 1:1 摆放，大了裁、小了留白。
// 边距（毫米）是裁切窗口：照片的大小和位置不受边距影响，窗口外变白。
struct ExportLayout {
  bool id_photo = true;                  // true 证件照排版，false 普通照片排版
  int paper_width_mm = 152;              // 已按方向摆好：横向时宽 > 高
  int paper_height_mm = 102;
  int dpi = 300;                         // 72..1200
  int rows = 1;                          // 1..20，只在证件照排版里起作用
  int cols = 1;
  int gap_px = 10;                       // 0..200 像素
  int margin_top_mm = 0;                 // 0..400
  int margin_bottom_mm = 0;
  int margin_left_mm = 0;
  int margin_right_mm = 0;
  bool operator==(const ExportLayout& o) const {
    return id_photo == o.id_photo && paper_width_mm == o.paper_width_mm && paper_height_mm == o.paper_height_mm &&
           dpi == o.dpi && rows == o.rows && cols == o.cols && gap_px == o.gap_px &&
           margin_top_mm == o.margin_top_mm && margin_bottom_mm == o.margin_bottom_mm &&
           margin_left_mm == o.margin_left_mm && margin_right_mm == o.margin_right_mm;
  }
};
// 相纸的像素大小（宽、高）；长或宽超过 20000 像素的设置不合法（parse 拒绝）。
std::uint32_t layout_page_width(const ExportLayout& layout);
std::uint32_t layout_page_height(const ExportLayout& layout);

// 一张照片（宽×高像素）在这套排版下落在相纸的哪里：页面大小、边距窗口（[left,right)×[top,bottom)，
// 窗口外是白的）和每张照片的矩形（相纸像素，可以伸出窗口或相纸，使用者自己裁）。导出进程和导出弹窗的预览
// 共用这一份，两边不会对不上。普通照片排版 `scaled` 为真，只有一个矩形，是缩放后的大小；证件照排版是
// rows×cols 个原始大小的矩形（见上面的画布规则）。相纸大小不合法（0 或超过 20000 像素）时返回 false。
struct LayoutRect {
  std::int64_t left = 0, top = 0, width = 0, height = 0;
};
struct LayoutGeometry {
  std::uint32_t page_width = 0, page_height = 0;
  std::int64_t window_left = 0, window_top = 0, window_right = 0, window_bottom = 0;
  bool scaled = false;
  std::vector<LayoutRect> photos;
  // 证件照排版的裁切线：1 px 纯黑横线，画在 cut_rows 这些行上，横向覆盖整张纸宽（不受边距窗口限制），
  // 但跳过 [cut_skip_left, cut_skip_right)：所有照片可见部分合起来的横向范围（列与列之间的格缝也在里面）。
  // 行是每张照片可见部分（照片 ∩ 窗口 ∩ 相纸）的上边缘上方一行、下边缘下方第二行，落在纸外的不要；
  // 从小到大、不重复。普通照片排版没有线。实测见 2026-11-07 证据文档“裁切线”一节（官方 6 张成品逐点一致）。
  std::vector<std::int64_t> cut_rows;
  std::int64_t cut_skip_left = 0, cut_skip_right = 0;
};
bool layout_geometry(const ExportLayout& layout, std::uint32_t photo_width, std::uint32_t photo_height,
                     LayoutGeometry& out);

// 导出设置（需求第 19 节）。品质与文件大小限制二选一：最多给一个。
struct ExportSettings {
  ExportFormat format = ExportFormat::Original;
  std::optional<int> quality;                  // 0 到 14 的整数，范围来自参数模式 export/quality
  std::optional<std::uint64_t> size_limit_kb;  // 目标文件大小，正整数
  bool convert_to_srgb = false;
  int print_copies = 0;                        // 0 表示不打印
  std::optional<ExportWatermark> watermark;    // 有值表示加水印
  std::optional<ExportLayout> layout;          // 有值表示排版成相纸
};

// 文本格式：以 ';' 分隔的 key=value，键为 format、quality、size_limit_kb、srgb、copies，
// 以及水印 wm（素材名）与 wm.l、wm.p、wm.s（横、竖、方三套，各为
// "size,fit,opacity,anchor,x,y,turns" 七个整数；有 wm 时三套都要有）；
// 排版 layout（"证件照 1 / 普通 0,宽mm,高mm,dpi,行,列,间隔,上,下,左,右" 十一个整数，第一个是 1 或 0）。
// 空字符串表示全部默认。未声明的键、重复的键、空值或越界值返回状态冲突。
ResultCode parse_export_settings(const std::string& text, ExportSettings& out);

// 规范编码：format 在前，其后依次是 quality 或 size_limit_kb、srgb（仅为 1 时）、copies（仅大于 0 时）、
// wm、wm.l、wm.p、wm.s（仅有水印时）、layout（仅有排版时）。
std::string encode_export_settings(const ExportSettings& settings);

}  // namespace pixcake
