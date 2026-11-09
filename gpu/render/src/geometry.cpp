#include <pixcake/gpu/geometry.hpp>
#include <pixcake/mask/combined_mask.hpp>
#include <pixcake/mask/gradient_mask.hpp>
#include <pixcake/mask/range_mask.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>

namespace pixcake::gpu {
namespace {

constexpr double kPi = 3.14159265358979323846;

// 一张 8 位多通道的紧凑平面（图像四通道，蒙版一通道）。
struct Plane {
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  int channels = 1;
  std::vector<std::uint8_t> data;
  std::uint8_t* at(std::uint32_t x, std::uint32_t y) {
    return data.data() + (static_cast<std::size_t>(y) * width + x) * static_cast<std::size_t>(channels);
  }
  const std::uint8_t* at(std::uint32_t x, std::uint32_t y) const {
    return data.data() + (static_cast<std::size_t>(y) * width + x) * static_cast<std::size_t>(channels);
  }
};

Plane make(std::uint32_t width, std::uint32_t height, int channels) {
  Plane plane;
  plane.width = width;
  plane.height = height;
  plane.channels = channels;
  plane.data.assign(static_cast<std::size_t>(width) * height * static_cast<std::size_t>(channels), 0);
  return plane;
}

Plane flip_h(const Plane& in) {
  Plane out = make(in.width, in.height, in.channels);
  for (std::uint32_t y = 0; y < in.height; ++y) {
    for (std::uint32_t x = 0; x < in.width; ++x) {
      std::copy_n(in.at(in.width - 1 - x, y), in.channels, out.at(x, y));
    }
  }
  return out;
}

Plane flip_v(const Plane& in) {
  Plane out = make(in.width, in.height, in.channels);
  for (std::uint32_t y = 0; y < in.height; ++y) {
    for (std::uint32_t x = 0; x < in.width; ++x) {
      std::copy_n(in.at(x, in.height - 1 - y), in.channels, out.at(x, y));
    }
  }
  return out;
}

// 顺时针转 90°：新图宽是旧图高；新 (x, y) 取自旧 (y, 旧高 - 1 - x)... 按“旧左下角变成新左上角”写。
Plane turn_clockwise(const Plane& in) {
  Plane out = make(in.height, in.width, in.channels);
  for (std::uint32_t y = 0; y < out.height; ++y) {
    for (std::uint32_t x = 0; x < out.width; ++x) {
      std::copy_n(in.at(y, in.height - 1 - x), in.channels, out.at(x, y));
    }
  }
  return out;
}

Plane crop(const Plane& in, std::uint32_t left, std::uint32_t top, std::uint32_t width, std::uint32_t height) {
  Plane out = make(width, height, in.channels);
  for (std::uint32_t y = 0; y < height; ++y) {
    std::copy_n(in.at(left, top + y), static_cast<std::size_t>(width) * static_cast<std::size_t>(in.channels), out.at(0, y));
  }
  return out;
}

// 任意角度：绕中心转 angle 度（正数顺时针转内容），输出是同比例的最大内接矩形，双线性取样。
Plane rotate(const Plane& in, double degrees) {
  const double radians = degrees * kPi / 180.0;
  const double c = std::cos(radians);
  const double s = std::sin(radians);
  const double w = in.width;
  const double h = in.height;
  const double fit = std::min(w / (w * std::abs(c) + h * std::abs(s)), h / (w * std::abs(s) + h * std::abs(c)));
  const auto out_w = static_cast<std::uint32_t>(std::max(1.0, std::floor(w * fit + 0.5)));
  const auto out_h = static_cast<std::uint32_t>(std::max(1.0, std::floor(h * fit + 0.5)));
  Plane out = make(out_w, out_h, in.channels);
  for (std::uint32_t y = 0; y < out_h; ++y) {
    for (std::uint32_t x = 0; x < out_w; ++x) {
      // 输出像素中心相对输出中心；内容顺时针转 angle，所以取样时逆时针转回去。
      const double dx = x + 0.5 - out_w / 2.0;
      const double dy = y + 0.5 - out_h / 2.0;
      const double sx = dx * c + dy * s + w / 2.0 - 0.5;
      const double sy = -dx * s + dy * c + h / 2.0 - 0.5;
      const double fx = std::clamp(sx, 0.0, w - 1.0);
      const double fy = std::clamp(sy, 0.0, h - 1.0);
      const auto x0 = static_cast<std::uint32_t>(fx);
      const auto y0 = static_cast<std::uint32_t>(fy);
      const std::uint32_t x1 = std::min(x0 + 1, in.width - 1);
      const std::uint32_t y1 = std::min(y0 + 1, in.height - 1);
      const double tx = fx - x0;
      const double ty = fy - y0;
      for (int ch = 0; ch < in.channels; ++ch) {
        const double top = in.at(x0, y0)[ch] * (1 - tx) + in.at(x1, y0)[ch] * tx;
        const double bottom = in.at(x0, y1)[ch] * (1 - tx) + in.at(x1, y1)[ch] * tx;
        out.at(x, y)[ch] = static_cast<std::uint8_t>(std::clamp(top * (1 - ty) + bottom * ty + 0.5, 0.0, 255.0));
      }
    }
  }
  return out;
}

bool ratio_of(const std::string& text, double& value) {
  if (text == "original") return false;
  const auto colon = text.find(':');
  if (colon == std::string::npos) return false;
  const double a = std::atof(text.substr(0, colon).c_str());
  const double b = std::atof(text.substr(colon + 1).c_str());
  if (a <= 0 || b <= 0) return false;
  value = a / b;
  return true;
}

Plane apply(const Geometry& geometry, Plane plane) {
  if (geometry.flip_horizontal) plane = flip_h(plane);
  if (geometry.flip_vertical) plane = flip_v(plane);
  for (int i = 0; i < geometry.quarter_turns; ++i) plane = turn_clockwise(plane);
  if (geometry.angle != 0.0) plane = rotate(plane, geometry.angle);
  double target = 0;
  if (ratio_of(geometry.ratio, target)) {
    const double current = static_cast<double>(plane.width) / plane.height;
    std::uint32_t width = plane.width;
    std::uint32_t height = plane.height;
    if (current > target) {
      width = std::max<std::uint32_t>(1, static_cast<std::uint32_t>(std::floor(plane.height * target + 0.5)));
    } else if (current < target) {
      height = std::max<std::uint32_t>(1, static_cast<std::uint32_t>(std::floor(plane.width / target + 0.5)));
    }
    width = std::min(width, plane.width);
    height = std::min(height, plane.height);
    plane = crop(plane, (plane.width - width) / 2, (plane.height - height) / 2, width, height);
  }
  return plane;
}

}  // namespace

Geometry geometry_of(const pixcake::render::RenderDescription& description) {
  Geometry geometry;
  for (const auto& operation : description.operations) {
    if (operation.capability_id != "crop" || !operation.enabled) continue;
    for (const auto& value : operation.values) {
      if (value.channel == "rotate") geometry.quarter_turns = ((static_cast<int>(std::lround(value.numeric_value)) % 4) + 4) % 4;
      if (value.channel == "flip_horizontal") geometry.flip_horizontal = value.display_value == "on";
      if (value.channel == "flip_vertical") geometry.flip_vertical = value.display_value == "on";
      if (value.channel == "angle") geometry.angle = value.numeric_value;
      if (value.channel == "ratio") geometry.ratio = value.display_value;
    }
  }
  return geometry;
}

bool apply_geometry(const Geometry& geometry, RgbaImage& image, std::vector<RenderMask>& masks) {
  if (geometry.identity()) return true;
  if (geometry.quarter_turns < 0 || geometry.quarter_turns > 3 || std::abs(geometry.angle) > 45.0 ||
      image.width == 0 || image.height == 0) {
    return false;
  }
  const std::size_t stride = image.row_stride == 0 ? static_cast<std::size_t>(image.width) * 4 : image.row_stride;
  if (image.pixels.size() < stride * image.height) return false;
  Plane plane = make(image.width, image.height, 4);
  for (std::uint32_t y = 0; y < image.height; ++y) {
    std::copy_n(image.pixels.data() + static_cast<std::size_t>(y) * stride, static_cast<std::size_t>(image.width) * 4, plane.at(0, y));
  }
  Plane out = apply(geometry, std::move(plane));
  image.width = out.width;
  image.height = out.height;
  image.row_stride = out.width * 4;
  image.pixels = std::move(out.data);
  for (auto& mask : masks) {
    if (mask.width == 0 || mask.height == 0 || mask.alpha.size() < static_cast<std::size_t>(mask.width) * mask.height) continue;
    Plane alpha = make(mask.width, mask.height, 1);
    alpha.data.assign(mask.alpha.begin(), mask.alpha.begin() + static_cast<std::ptrdiff_t>(alpha.data.size()));
    Plane moved = apply(geometry, std::move(alpha));
    mask.width = moved.width;
    mask.height = moved.height;
    mask.alpha = std::move(moved.data);
  }
  return true;
}

namespace {

double smooth(double t) { return t * t * (3.0 - 2.0 * t); }

// 线性：沿方向 (cos, sin) 的有符号距离 d，d 从 -b 到 +b 由 1 降到 0（b 是过渡带半宽）；柔和度把线性过渡
// 混向平滑曲线。径向：半径内为 1，边缘柔和带里降到 0。
RenderMask gradient_mask(const std::string& id, const pixcake::mask::GradientMask& g, std::uint32_t width,
                         std::uint32_t height) {
  RenderMask mask;
  mask.id = id;
  mask.width = width;
  mask.height = height;
  mask.alpha.assign(static_cast<std::size_t>(width) * height, 0);
  const double w = width;
  const double h = height;
  const double diagonal = std::sqrt(w * w + h * h);
  const double cx = g.x / 100.0 * w;
  const double cy = g.y / 100.0 * h;
  const double soft = g.feather / 100.0;
  const double radians = g.angle * kPi / 180.0;
  const double dx = std::cos(radians);
  const double dy = std::sin(radians);
  const double band = std::max(0.5, g.size / 100.0 * diagonal / 2.0);
  const double radius = std::max(0.5, g.size / 100.0 * diagonal / 2.0);  // 横向半径；纵向半径 = 横向 × ratio
  for (std::uint32_t y = 0; y < height; ++y) {
    for (std::uint32_t x = 0; x < width; ++x) {
      const double px = x + 0.5 - cx;
      const double py = y + 0.5 - cy;
      double alpha = 0.0;
      if (!g.radial) {
        const double linear = std::clamp(0.5 - (px * dx + py * dy) / (2.0 * band), 0.0, 1.0);
        alpha = linear + (smooth(linear) - linear) * soft;
      } else {
        // 椭圆：把纵向按比例拉回圆，再按半径衡量；内椭圆（满强度）是外椭圆的 (1 - 羽化)。
        const double distance = std::sqrt(px * px + (py / (g.ratio / 100.0)) * (py / (g.ratio / 100.0)));
        const double inner = radius * (1.0 - soft);
        alpha = distance <= inner ? 1.0 : distance >= radius ? 0.0 : 1.0 - smooth((distance - inner) / (radius - inner));
      }
      mask.alpha[static_cast<std::size_t>(y) * width + x] = static_cast<std::uint8_t>(std::lround(alpha * 255.0));
    }
  }
  return mask;
}

}  // namespace

void add_gradient_masks(const pixcake::render::RenderDescription& description, std::uint32_t width,
                        std::uint32_t height, std::vector<RenderMask>& masks) {
  if (width == 0 || height == 0) return;
  for (const auto& operation : description.operations) {
    if (!operation.enabled) continue;
    for (const auto& whole : operation.mask_ids) {
      for (const auto& id : pixcake::mask::leaf_mask_ids(whole)) {  // 组合编号里的渐变子项也要生成
        pixcake::mask::GradientMask gradient;
        if (!pixcake::mask::parse_gradient_mask_id(id, gradient)) continue;
        const bool present = std::any_of(masks.begin(), masks.end(), [&id](const RenderMask& m) { return m.id == id; });
        if (!present) masks.push_back(gradient_mask(id, gradient, width, height));
      }
    }
  }
}

namespace {

// sRGB 编码值到线性（Lab 用）：256 项表。
const std::array<float, 256>& linear_table() {
  static const std::array<float, 256> table = [] {
    std::array<float, 256> t{};
    for (int i = 0; i < 256; ++i) {
      const double c = i / 255.0;
      t[static_cast<std::size_t>(i)] = static_cast<float>(c > 0.04045 ? std::pow((c + 0.055) / 1.055, 2.4) : c / 12.92);
    }
    return t;
  }();
  return table;
}

struct Lab { double l, a, b; };

Lab to_lab(int r, int g, int b) {
  const auto& table = linear_table();
  const double lr = table[static_cast<std::size_t>(r)];
  const double lg = table[static_cast<std::size_t>(g)];
  const double lb = table[static_cast<std::size_t>(b)];
  const double x = (0.4124 * lr + 0.3576 * lg + 0.1805 * lb) / 0.9505;
  const double y = 0.2126 * lr + 0.7152 * lg + 0.0722 * lb;
  const double z = (0.0193 * lr + 0.1192 * lg + 0.9505 * lb) / 1.089;
  const auto f = [](double t) { return t > 0.008856 ? std::cbrt(t) : 7.787 * t + 16.0 / 116.0; };
  const double fx = f(x), fy = f(y), fz = f(z);
  return {116.0 * fy - 16.0, 500.0 * (fx - fy), 200.0 * (fy - fz)};
}

RenderMask color_range_mask(const std::string& id, const pixcake::mask::ColorRangeMask& range, const RgbaImage& image) {
  RenderMask mask;
  mask.id = id;
  mask.width = image.width;
  mask.height = image.height;
  mask.alpha.assign(static_cast<std::size_t>(image.width) * image.height, 0);
  const Lab sample = to_lab(range.r, range.g, range.b);
  const double reach = pixcake::mask::color_range_reach(range.adjust);
  for (std::uint32_t y = 0; y < image.height; ++y) {
    const std::uint8_t* row = image.pixels.data() + static_cast<std::size_t>(y) * image.row_stride;
    for (std::uint32_t x = 0; x < image.width; ++x) {
      const Lab lab = to_lab(row[x * 4], row[x * 4 + 1], row[x * 4 + 2]);
      const double distance = std::sqrt((lab.l - sample.l) * (lab.l - sample.l) + (lab.a - sample.a) * (lab.a - sample.a) +
                                        (lab.b - sample.b) * (lab.b - sample.b));
      const double alpha = std::clamp(1.0 - distance / reach, 0.0, 1.0);
      mask.alpha[static_cast<std::size_t>(y) * image.width + x] = static_cast<std::uint8_t>(std::lround(alpha * 255.0));
    }
  }
  return mask;
}

RenderMask luminance_range_mask(const std::string& id, const pixcake::mask::LuminanceRangeMask& range,
                                const RgbaImage& image) {
  RenderMask mask;
  mask.id = id;
  mask.width = image.width;
  mask.height = image.height;
  mask.alpha.assign(static_cast<std::size_t>(image.width) * image.height, 0);
  for (std::uint32_t y = 0; y < image.height; ++y) {
    const std::uint8_t* row = image.pixels.data() + static_cast<std::size_t>(y) * image.row_stride;
    for (std::uint32_t x = 0; x < image.width; ++x) {
      const double luminance = (0.2126 * row[x * 4] + 0.7152 * row[x * 4 + 1] + 0.0722 * row[x * 4 + 2]) / 255.0 * 100.0;
      double alpha = 0.0;
      if (luminance >= range.b && luminance <= range.c) {
        alpha = 1.0;
      } else if (luminance > range.a && luminance < range.b) {
        alpha = (luminance - range.a) / (range.b - range.a);
      } else if (luminance > range.c && luminance < range.d) {
        alpha = (range.d - luminance) / (range.d - range.c);
      }
      mask.alpha[static_cast<std::size_t>(y) * image.width + x] = static_cast<std::uint8_t>(std::lround(alpha * 255.0));
    }
  }
  return mask;
}

}  // namespace

void add_range_masks(const pixcake::render::RenderDescription& description, const RgbaImage& image,
                     std::vector<RenderMask>& masks) {
  if (image.width == 0 || image.height == 0 ||
      image.pixels.size() < static_cast<std::size_t>(image.height - 1) * image.row_stride + image.width * 4ull) {
    return;
  }
  for (const auto& operation : description.operations) {
    if (!operation.enabled) continue;
    for (const auto& whole : operation.mask_ids) {
      for (const auto& id : pixcake::mask::leaf_mask_ids(whole)) {
        const bool present = std::any_of(masks.begin(), masks.end(), [&id](const RenderMask& m) { return m.id == id; });
        if (present) continue;
        pixcake::mask::ColorRangeMask color;
        pixcake::mask::LuminanceRangeMask luminance;
        if (pixcake::mask::parse_color_range_mask_id(id, color)) {
          masks.push_back(color_range_mask(id, color, image));
        } else if (pixcake::mask::parse_luminance_range_mask_id(id, luminance)) {
          masks.push_back(luminance_range_mask(id, luminance, image));
        }
      }
    }
  }
}

void add_invert_masks(const pixcake::render::RenderDescription& description, std::vector<RenderMask>& masks) {
  for (const auto& operation : description.operations) {
    if (!operation.enabled) continue;
    for (const auto& whole : operation.mask_ids) {
      std::vector<std::string> candidates;
      std::vector<pixcake::mask::CombinedPart> parts;
      if (pixcake::mask::parse_combined_mask_id(whole, parts)) {
        for (const auto& part : parts) candidates.push_back(part.id);
      } else {
        candidates.push_back(whole);
      }
      for (const auto& id : candidates) {
        std::string inner;
        if (!pixcake::mask::parse_invert_mask_id(id, inner)) continue;
        if (std::any_of(masks.begin(), masks.end(), [&id](const RenderMask& m) { return m.id == id; })) continue;
        const auto found = std::find_if(masks.begin(), masks.end(), [&inner](const RenderMask& m) { return m.id == inner; });
        if (found == masks.end()) continue;
        RenderMask out;
        out.id = id;
        out.width = found->width;
        out.height = found->height;
        out.alpha.resize(found->alpha.size());
        for (std::size_t i = 0; i < out.alpha.size(); ++i) out.alpha[i] = static_cast<std::uint8_t>(255 - found->alpha[i]);
        masks.push_back(std::move(out));
      }
    }
  }
}

void add_combined_masks(const pixcake::render::RenderDescription& description, std::vector<RenderMask>& masks) {
  for (const auto& operation : description.operations) {
    if (!operation.enabled) continue;
    for (const auto& id : operation.mask_ids) {
      std::vector<pixcake::mask::CombinedPart> parts;
      if (!pixcake::mask::parse_combined_mask_id(id, parts)) continue;
      if (std::any_of(masks.begin(), masks.end(), [&id](const RenderMask& m) { return m.id == id; })) continue;
      std::vector<const RenderMask*> sources;
      for (const auto& part : parts) {
        const auto found = std::find_if(masks.begin(), masks.end(), [&part](const RenderMask& m) { return m.id == part.id; });
        if (found == masks.end() || found->alpha.size() < static_cast<std::size_t>(found->width) * found->height) break;
        sources.push_back(&*found);
      }
      if (sources.size() != parts.size() || sources.empty()) continue;
      RenderMask out;
      out.id = id;
      out.width = sources[0]->width;
      out.height = sources[0]->height;
      bool same_size = true;
      for (const auto* source : sources) same_size = same_size && source->width == out.width && source->height == out.height;
      if (!same_size) continue;
      out.alpha.assign(sources[0]->alpha.size(), 0);
      for (std::size_t p = 0; p < parts.size(); ++p) {
        const auto& from = sources[p]->alpha;
        for (std::size_t i = 0; i < out.alpha.size(); ++i) {
          out.alpha[i] = parts[p].subtract
                             ? static_cast<std::uint8_t>(std::lround(out.alpha[i] * (255 - from[i]) / 255.0))
                             : std::max(out.alpha[i], from[i]);
        }
      }
      masks.push_back(std::move(out));
    }
  }
}

}  // namespace pixcake::gpu
