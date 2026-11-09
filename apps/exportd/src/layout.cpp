#include "pixcake/exportd/layout.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace pixcake::exportd {
namespace {

struct Page {
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::vector<std::uint8_t> rgba;  // 不透明，白底
  std::uint8_t* at(std::int64_t x, std::int64_t y) { return &rgba[(static_cast<std::size_t>(y) * width + static_cast<std::size_t>(x)) * 4]; }
};

// 照片垫白后的 RGB（不透明）。
std::vector<std::uint8_t> flatten(const RgbaSource& image) {
  std::vector<std::uint8_t> out(static_cast<std::size_t>(image.width) * image.height * 4, 255);
  for (std::uint32_t y = 0; y < image.height; ++y) {
    const std::uint8_t* row = image.rgba.data() + static_cast<std::size_t>(y) * image.stride;
    std::uint8_t* target = out.data() + static_cast<std::size_t>(y) * image.width * 4;
    for (std::uint32_t x = 0; x < image.width; ++x) {
      const int back = 255 - row[x * 4 + 3];  // 预乘：out = src + (1 − a) × 白
      for (int c = 0; c < 3; ++c) target[x * 4 + c] = static_cast<std::uint8_t>(std::min(255, row[x * 4 + c] + back));
      target[x * 4 + 3] = 255;
    }
  }
  return out;
}

// 缩放到 dw×dh：缩小用面积平均，放大用双线性。
std::vector<std::uint8_t> resample(const std::vector<std::uint8_t>& src, std::uint32_t sw, std::uint32_t sh,
                                   std::uint32_t dw, std::uint32_t dh) {
  std::vector<std::uint8_t> out(static_cast<std::size_t>(dw) * dh * 4, 255);
  const double fx = static_cast<double>(sw) / dw;
  const double fy = static_cast<double>(sh) / dh;
  const auto pixel = [&](std::uint32_t x, std::uint32_t y, int c) {
    return static_cast<double>(src[(static_cast<std::size_t>(y) * sw + x) * 4 + static_cast<std::size_t>(c)]);
  };
  for (std::uint32_t y = 0; y < dh; ++y) {
    for (std::uint32_t x = 0; x < dw; ++x) {
      double value[3] = {0, 0, 0};
      if (fx >= 1.0 && fy >= 1.0) {
        const double x0 = x * fx, x1 = (x + 1) * fx, y0 = y * fy, y1 = (y + 1) * fy;
        double weight = 0;
        for (std::uint32_t sy = static_cast<std::uint32_t>(y0); sy < std::min<std::uint32_t>(sh, static_cast<std::uint32_t>(std::ceil(y1))); ++sy) {
          const double wy = std::min<double>(sy + 1, y1) - std::max<double>(sy, y0);
          for (std::uint32_t sx = static_cast<std::uint32_t>(x0); sx < std::min<std::uint32_t>(sw, static_cast<std::uint32_t>(std::ceil(x1))); ++sx) {
            const double w = wy * (std::min<double>(sx + 1, x1) - std::max<double>(sx, x0));
            for (int c = 0; c < 3; ++c) value[c] += w * pixel(sx, sy, c);
            weight += w;
          }
        }
        if (weight > 0) for (double& v : value) v /= weight;
      } else {
        const double sx = std::clamp((x + 0.5) * fx - 0.5, 0.0, sw - 1.0);
        const double sy = std::clamp((y + 0.5) * fy - 0.5, 0.0, sh - 1.0);
        const auto ix = static_cast<std::uint32_t>(sx), iy = static_cast<std::uint32_t>(sy);
        const std::uint32_t jx = std::min(ix + 1, sw - 1), jy = std::min(iy + 1, sh - 1);
        const double ax = sx - ix, ay = sy - iy;
        for (int c = 0; c < 3; ++c) {
          value[c] = (pixel(ix, iy, c) * (1 - ax) + pixel(jx, iy, c) * ax) * (1 - ay) +
                     (pixel(ix, jy, c) * (1 - ax) + pixel(jx, jy, c) * ax) * ay;
        }
      }
      std::uint8_t* target = &out[(static_cast<std::size_t>(y) * dw + x) * 4];
      for (int c = 0; c < 3; ++c) target[c] = static_cast<std::uint8_t>(std::lround(std::clamp(value[c], 0.0, 255.0)));
    }
  }
  return out;
}

// 把 src（sw×sh）的左上角放在 (left, top)，只画落在 clip 矩形里的部分。
struct Rect {
  std::int64_t left, top, right, bottom;  // [left, right) × [top, bottom)
};
void blit(Page& page, const std::vector<std::uint8_t>& src, std::uint32_t sw, std::uint32_t sh, std::int64_t left,
          std::int64_t top, const Rect& clip) {
  const std::int64_t x0 = std::max(left, clip.left), x1 = std::min<std::int64_t>(left + sw, clip.right);
  const std::int64_t y0 = std::max(top, clip.top), y1 = std::min<std::int64_t>(top + sh, clip.bottom);
  for (std::int64_t y = y0; y < y1; ++y) {
    for (std::int64_t x = x0; x < x1; ++x) {
      const std::uint8_t* from = &src[(static_cast<std::size_t>(y - top) * sw + static_cast<std::size_t>(x - left)) * 4];
      std::uint8_t* to = page.at(x, y);
      to[0] = from[0]; to[1] = from[1]; to[2] = from[2]; to[3] = 255;
    }
  }
}

}  // namespace

ResultCode compose_layout(const ExportLayout& layout, RgbaSource& image, std::string& diagnostic) {
  const std::uint32_t width = layout_page_width(layout);
  const std::uint32_t height = layout_page_height(layout);
  if (width == 0 || height == 0 || width > 20000 || height > 20000) {
    diagnostic = "相纸尺寸不合法";
    return ResultCode::StateConflict;
  }
  if (image.width == 0 || image.height == 0 || image.rgba.size() < static_cast<std::size_t>(image.stride) * image.height) {
    diagnostic = "排版的照片是空的";
    return ResultCode::StateConflict;
  }
  LayoutGeometry geometry;
  if (!layout_geometry(layout, image.width, image.height, geometry)) {
    diagnostic = "相纸尺寸不合法";
    return ResultCode::StateConflict;
  }
  Page page;
  page.width = width;
  page.height = height;
  page.rgba.assign(static_cast<std::size_t>(width) * height * 4, 255);
  const Rect window{geometry.window_left, geometry.window_top, geometry.window_right, geometry.window_bottom};
  const std::vector<std::uint8_t> photo = flatten(image);
  if (geometry.scaled) {
    const LayoutRect& at = geometry.photos.front();
    const auto sw = static_cast<std::uint32_t>(at.width), sh = static_cast<std::uint32_t>(at.height);
    blit(page, resample(photo, image.width, image.height, sw, sh), sw, sh, at.left, at.top, window);
  } else {
    for (const LayoutRect& at : geometry.photos) blit(page, photo, image.width, image.height, at.left, at.top, window);
  }
  image.width = width;
  image.height = height;
  image.stride = width * 4;
  image.rgba = std::move(page.rgba);
  diagnostic.clear();
  return ResultCode::Ok;
}

}  // namespace pixcake::exportd
