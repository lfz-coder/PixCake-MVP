#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace pixcake::ui::panels {

// Per-channel counts of an RGBA8 frame for the refine panel's RGB line
// histogram.  Fully transparent pixels are not counted.
struct RgbHistogram {
  std::array<std::uint32_t, 256> red{};
  std::array<std::uint32_t, 256> green{};
  std::array<std::uint32_t, 256> blue{};
  std::uint32_t peak = 0;     // largest bin over the three channels
  std::uint64_t pixels = 0;   // counted pixels
};

// stride is in bytes; returns an empty histogram for invalid input.
RgbHistogram rgb_histogram(const std::uint8_t* rgba, std::uint32_t width, std::uint32_t height,
                           std::uint32_t stride, std::size_t size);

}  // namespace pixcake::ui::panels
