#pragma once

#include "pixcake/support/result_code.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string>

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


// 导出设置（需求第 19 节）。品质与文件大小限制二选一：最多给一个。
struct ExportSettings {
  ExportFormat format = ExportFormat::Original;
  std::optional<int> quality;                  // 0 到 14 的整数，范围来自参数模式 export/quality
  std::optional<std::uint64_t> size_limit_kb;  // 目标文件大小，正整数
  bool convert_to_srgb = false;
  int print_copies = 0;                        // 0 表示不打印
  std::optional<ExportWatermark> watermark;    // 有值表示加水印
};

// 文本格式：以 ';' 分隔的 key=value，键为 format、quality、size_limit_kb、srgb、copies，
// 以及水印 wm（素材名）与 wm.l、wm.p、wm.s（横、竖、方三套，各为
// "size,fit,opacity,anchor,x,y,turns" 七个整数；有 wm 时三套都要有）；
// 空字符串表示全部默认。未声明的键、重复的键、空值或越界值返回状态冲突。
ResultCode parse_export_settings(const std::string& text, ExportSettings& out);

// 规范编码：format 在前，其后依次是 quality 或 size_limit_kb、srgb（仅为 1 时）、copies（仅大于 0 时）、
// wm、wm.l、wm.p、wm.s（仅有水印时）。
std::string encode_export_settings(const ExportSettings& settings);

}  // namespace pixcake
