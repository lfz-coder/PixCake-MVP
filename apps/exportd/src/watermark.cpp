#include "pixcake/exportd/watermark.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace pixcake::exportd {
namespace {

// One axis of a separable resampler: for each target sample the source
// samples it covers and their weights (a triangle filter widened by the scale
// when shrinking, so every source pixel contributes; plain bilinear when
// enlarging).
struct Taps {
  std::vector<std::uint32_t> first;
  std::vector<std::vector<float>> weights;
};

Taps taps(std::uint32_t source, std::uint32_t target) {
  Taps out;
  out.first.resize(target);
  out.weights.resize(target);
  const double scale = static_cast<double>(source) / target;
  const double support = std::max(1.0, scale);
  for (std::uint32_t t = 0; t < target; ++t) {
    const double centre = (t + 0.5) * scale;
    const auto low = static_cast<std::int64_t>(std::floor(centre - support));
    const auto high = static_cast<std::int64_t>(std::ceil(centre + support));
    const std::int64_t from = std::max<std::int64_t>(0, low);
    const std::int64_t to = std::min<std::int64_t>(source - 1, high);
    std::vector<float> w;
    double total = 0.0;
    for (std::int64_t s = from; s <= to; ++s) {
      const double distance = std::abs((s + 0.5) - centre) / support;
      const double weight = std::max(0.0, 1.0 - distance);
      w.push_back(static_cast<float>(weight));
      total += weight;
    }
    if (total <= 0.0) {  // degenerate: nearest sample
      const auto nearest = std::clamp<std::int64_t>(static_cast<std::int64_t>(centre), 0, source - 1);
      out.first[t] = static_cast<std::uint32_t>(nearest);
      out.weights[t] = {1.0f};
      continue;
    }
    for (float& value : w) value = static_cast<float>(value / total);
    out.first[t] = static_cast<std::uint32_t>(from);
    out.weights[t] = std::move(w);
  }
  return out;
}

// The material turned clockwise, as floats (premultiplied, 0..1).
std::vector<float> rotated(const RgbaSource& material, int turns, std::uint32_t& width, std::uint32_t& height) {
  const std::uint32_t w = material.width;
  const std::uint32_t h = material.height;
  const bool swap = turns % 2 == 1;
  width = swap ? h : w;
  height = swap ? w : h;
  std::vector<float> out(static_cast<std::size_t>(width) * height * 4);
  for (std::uint32_t y = 0; y < height; ++y) {
    for (std::uint32_t x = 0; x < width; ++x) {
      // Target (x, y) comes from source (sx, sy).
      std::uint32_t sx = x;
      std::uint32_t sy = y;
      switch (turns) {
        case 1: sx = y; sy = h - 1 - x; break;          // clockwise: the source's top goes right
        case 2: sx = w - 1 - x; sy = h - 1 - y; break;
        case 3: sx = w - 1 - y; sy = x; break;
        default: break;
      }
      const std::uint8_t* from = material.rgba.data() + static_cast<std::size_t>(sy) * material.stride + sx * 4;
      float* to = out.data() + (static_cast<std::size_t>(y) * width + x) * 4;
      for (int c = 0; c < 4; ++c) to[c] = from[c] / 255.0f;
    }
  }
  return out;
}

}  // namespace

ResultCode draw_watermark(const RgbaSource& material, const WatermarkPlacement& placement, RgbaSource& image,
                          std::string& diagnostic) {
  if (material.width == 0 || material.height == 0 || material.stride < material.width * 4 ||
      material.rgba.size() < static_cast<std::size_t>(material.stride) * material.height) {
    diagnostic = "水印素材是空的或尺寸不对";
    return ResultCode::StateConflict;
  }
  const std::uint32_t stride = image.stride == 0 ? image.width * 4 : image.stride;
  if (image.width == 0 || image.height == 0 || image.rgba.size() < static_cast<std::size_t>(stride) * image.height) {
    diagnostic = "导出画面是空的";
    return ResultCode::StateConflict;
  }
  const WatermarkRect rect = place_watermark(material.width, material.height, placement, image.width, image.height);
  diagnostic.clear();
  if (rect.width == 0 || rect.height == 0 || placement.opacity == 0) return ResultCode::Ok;
  // Visible part only.
  const std::int64_t x0 = std::max<std::int64_t>(0, rect.left);
  const std::int64_t y0 = std::max<std::int64_t>(0, rect.top);
  const std::int64_t x1 = std::min<std::int64_t>(image.width, rect.left + rect.width);
  const std::int64_t y1 = std::min<std::int64_t>(image.height, rect.top + rect.height);
  if (x0 >= x1 || y0 >= y1) return ResultCode::Ok;

  std::uint32_t turned_width = 0;
  std::uint32_t turned_height = 0;
  const std::vector<float> turned = rotated(material, placement.quarter_turns, turned_width, turned_height);
  const Taps across = taps(turned_width, rect.width);
  const Taps down = taps(turned_height, rect.height);
  // Horizontal pass on the rows the vertical pass needs, for the visible columns.
  const std::uint32_t row_from = down.first[static_cast<std::size_t>(y0 - rect.top)];
  const auto& last = down.weights[static_cast<std::size_t>(y1 - 1 - rect.top)];
  const std::uint32_t row_to = down.first[static_cast<std::size_t>(y1 - 1 - rect.top)] +
                               static_cast<std::uint32_t>(last.size());  // exclusive
  const std::size_t columns = static_cast<std::size_t>(x1 - x0);
  std::vector<float> rows(static_cast<std::size_t>(row_to - row_from) * columns * 4, 0.0f);
  for (std::uint32_t sy = row_from; sy < row_to; ++sy) {
    const float* source_row = turned.data() + static_cast<std::size_t>(sy) * turned_width * 4;
    float* out = rows.data() + static_cast<std::size_t>(sy - row_from) * columns * 4;
    for (std::size_t c = 0; c < columns; ++c) {
      const std::size_t t = static_cast<std::size_t>(x0 - rect.left) + c;
      const float* from = source_row + static_cast<std::size_t>(across.first[t]) * 4;
      float sum[4] = {};
      for (std::size_t k = 0; k < across.weights[t].size(); ++k) {
        for (int ch = 0; ch < 4; ++ch) sum[ch] += from[k * 4 + ch] * across.weights[t][k];
      }
      std::copy(sum, sum + 4, out + c * 4);
    }
  }
  const float opacity = placement.opacity / 100.0f;
  for (std::int64_t y = y0; y < y1; ++y) {
    const std::size_t t = static_cast<std::size_t>(y - rect.top);
    std::uint8_t* target = image.rgba.data() + static_cast<std::size_t>(y) * stride;
    for (std::size_t c = 0; c < columns; ++c) {
      float sum[4] = {};
      for (std::size_t k = 0; k < down.weights[t].size(); ++k) {
        const float* from = rows.data() + (static_cast<std::size_t>(down.first[t] - row_from + k) * columns + c) * 4;
        for (int ch = 0; ch < 4; ++ch) sum[ch] += from[ch] * down.weights[t][k];
      }
      // Premultiplied "over": out = watermark + image x (1 - watermark alpha).
      const float alpha = std::clamp(sum[3] * opacity, 0.0f, 1.0f);
      std::uint8_t* pixel = target + static_cast<std::size_t>(x0 + static_cast<std::int64_t>(c)) * 4;
      for (int ch = 0; ch < 4; ++ch) {
        const float over = std::clamp(sum[ch] * opacity, 0.0f, 1.0f) * 255.0f + pixel[ch] * (1.0f - alpha);
        pixel[ch] = static_cast<std::uint8_t>(std::clamp(std::lround(over), 0L, 255L));
      }
    }
  }
  return ResultCode::Ok;
}

}  // namespace pixcake::exportd
