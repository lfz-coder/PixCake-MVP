#include "pixcake/ui/panels/histogram.hpp"

#include <algorithm>

namespace pixcake::ui::panels {

RgbHistogram rgb_histogram(const std::uint8_t* rgba, std::uint32_t width, std::uint32_t height,
                           std::uint32_t stride, std::size_t size) {
  RgbHistogram out;
  if (rgba == nullptr || width == 0 || height == 0 || stride < static_cast<std::uint64_t>(width) * 4 ||
      static_cast<std::uint64_t>(stride) * height > size) {
    return out;
  }
  for (std::uint32_t y = 0; y < height; ++y) {
    const std::uint8_t* p = rgba + static_cast<std::size_t>(y) * stride;
    for (std::uint32_t x = 0; x < width; ++x, p += 4) {
      if (p[3] == 0) continue;
      ++out.red[p[0]];
      ++out.green[p[1]];
      ++out.blue[p[2]];
      ++out.pixels;
    }
  }
  for (int i = 0; i < 256; ++i) out.peak = std::max({out.peak, out.red[i], out.green[i], out.blue[i]});
  return out;
}

double histogram_bar_height(const RgbHistogram& histogram, std::uint32_t count) {
  if (histogram.pixels == 0) return 0.0;
  const double full = static_cast<double>(histogram.pixels) / 40.0;
  return std::min(1.0, static_cast<double>(count) / full);
}

}  // namespace pixcake::ui::panels
