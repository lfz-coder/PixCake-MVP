#pragma once

#include <pixcake/gpu/render_executor.hpp>

#include <string>
#include <vector>

namespace pixcake::gpu {

// 几何变换（需求 §12.6）：翻转、旋转 90°、任意角度（拉直）和比例裁剪。在颜色绘制之前整张图一次做完，预览、缩略图和
// 导出走同一个函数，所以三处的画幅一致。蒙版和图像同步变换。顺序固定：水平翻转、垂直翻转、旋转 90°、角度、比例裁剪。
struct Geometry {
  int quarter_turns = 0;           // 顺时针转几个 90°，0 到 3
  bool flip_horizontal = false;
  bool flip_vertical = false;
  double angle = 0.0;              // 度，-45 到 45；正数顺时针转内容（官方方向未核实）；转完裁成同比例的最大内接矩形
  std::string ratio = "original";  // 居中裁成的宽高比：original、1:1、4:3、3:4、3:2、2:3、16:9、9:16、5:4、4:5
  bool identity() const {
    return quarter_turns == 0 && !flip_horizontal && !flip_vertical && angle == 0.0 && ratio == "original";
  }
};

// 读绘制描述里 crop 能力的几何参数；没有或都是默认值时 identity() 为真。
Geometry geometry_of(const pixcake::render::RenderDescription& description);

// 变换图像和蒙版（蒙版和图像同大小）。恒等变换不动。返回 false 表示参数不合法，图像不变。
bool apply_geometry(const Geometry& geometry, RgbaImage& image, std::vector<RenderMask>& masks);

// 把描述里用到的渐变蒙版（"gradient:…"）按画幅（几何变换之后）生成并追加到 masks。已有同编号的不重复生成；
// 编号不合法的忽略（对应的绘制步骤会因蒙版缺失被跳过）。
void add_gradient_masks(const pixcake::render::RenderDescription& description, std::uint32_t width,
                        std::uint32_t height, std::vector<RenderMask>& masks);

}  // namespace pixcake::gpu
