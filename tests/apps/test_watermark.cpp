// Export watermark placement and drawing, against what the test build showed
// (docs/superpowers/evidence/2026-11-07-watermark-observation.md).
#include "pixcake/exportd/watermark.hpp"

#include <cassert>
#include <cstdio>
#include <vector>

namespace {

using namespace pixcake;
using namespace pixcake::exportd;

RgbaSource solid(std::uint32_t width, std::uint32_t height, std::uint8_t r, std::uint8_t g, std::uint8_t b,
                 std::uint8_t a = 255) {
  RgbaSource image;
  image.width = width;
  image.height = height;
  image.stride = width * 4;
  image.rgba.resize(static_cast<std::size_t>(width) * height * 4);
  for (std::size_t i = 0; i < image.rgba.size(); i += 4) {
    image.rgba[i] = r;
    image.rgba[i + 1] = g;
    image.rgba[i + 2] = b;
    image.rgba[i + 3] = a;
  }
  return image;
}

const std::uint8_t* at(const RgbaSource& image, std::uint32_t x, std::uint32_t y) {
  return image.rgba.data() + static_cast<std::size_t>(y) * image.stride + x * 4;
}

// The bounding box of pixels that are not the background (white).
struct Box {
  std::int64_t left = -1, top = -1, right = -1, bottom = -1;
};
Box changed(const RgbaSource& image) {
  Box box;
  for (std::uint32_t y = 0; y < image.height; ++y) {
    for (std::uint32_t x = 0; x < image.width; ++x) {
      const auto* p = at(image, x, y);
      if (p[0] == 255 && p[1] == 255 && p[2] == 255) continue;
      if (box.left < 0 || x < box.left) box.left = x;
      if (box.top < 0) box.top = y;
      box.right = std::max<std::int64_t>(box.right, x);
      box.bottom = y;
    }
  }
  return box;
}

}  // namespace

int main() {
  std::string diagnostic;
  WatermarkPlacement defaults;  // size 10, opacity 100, top-right, 3 / 3, no turn

  // Portrait export measured in the test build: 1024x1536, a 300x100 green
  // material -> 461x154 at x 532..992, y 46..199, exactly the material's colour.
  {
    auto image = solid(1024, 1536, 255, 255, 255);
    const auto material = solid(300, 100, 0, 192, 0);
    const auto rect = place_watermark(300, 100, defaults, 1024, 1536);
    assert(rect.width == 461 && rect.height == 154 && rect.left == 532 && rect.top == 46);
    assert(draw_watermark(material, defaults, image, diagnostic) == ResultCode::Ok);
    const Box box = changed(image);
    std::printf("portrait: %lld..%lld x %lld..%lld\n", static_cast<long long>(box.left),
                static_cast<long long>(box.right), static_cast<long long>(box.top), static_cast<long long>(box.bottom));
    assert(box.left == 532 && box.right == 992 && box.top == 46 && box.bottom == 199);
    const auto* inside = at(image, 760, 120);
    assert(inside[0] == 0 && inside[1] == 192 && inside[2] == 0 && inside[3] == 255);
  }

  // Square preview (814 px): 400x200 -> short edge 10% of the long edge.
  {
    const auto rect = place_watermark(400, 200, defaults, 814, 814);
    assert(rect.height == 81 && rect.width == 163);
    // Size 100 runs past the left and bottom edges (clipped when drawn).
    WatermarkPlacement big = defaults;
    big.size = 100;
    const auto huge = place_watermark(400, 200, big, 814, 814);
    assert(huge.width == 1628 && huge.left < 0);
    auto image = solid(814, 814, 255, 255, 255);
    assert(draw_watermark(solid(400, 200, 255, 0, 0), big, image, diagnostic) == ResultCode::Ok);
    // Covers the left and bottom edges; keeps 3% from the right and top.
    assert(at(image, 0, 400)[1] == 0 && at(image, 780, 813)[1] == 0);
    assert(at(image, 813, 400)[1] == 255 && at(image, 400, 0)[1] == 255);
  }

  // Horizontal 50 on the right column: right margin half the width.
  {
    WatermarkPlacement half = defaults;
    half.x = 50;
    const auto rect = place_watermark(400, 200, half, 814, 814);
    assert(814 - (rect.left + rect.width) == 407);
    // +-100 move it out of the picture: nothing drawn.
    for (int x : {100, -100}) {
      WatermarkPlacement out = defaults;
      out.x = x;
      auto image = solid(814, 814, 255, 255, 255);
      assert(draw_watermark(solid(400, 200, 255, 0, 0), out, image, diagnostic) == ResultCode::Ok);
      assert(changed(image).left < 0);
    }
  }

  // Grid: centre (0 / 0) is centred; bottom-left keeps 3% from the left and bottom.
  {
    WatermarkPlacement centre = defaults;
    centre.anchor = 4;
    centre.x = centre.y = 0;
    const auto middle = place_watermark(400, 200, centre, 1000, 1000);
    assert(middle.left == (1000 - middle.width) / 2 && middle.top == (1000 - middle.height) / 2);
    WatermarkPlacement corner = defaults;
    corner.anchor = 6;
    const auto low = place_watermark(400, 200, corner, 1000, 1000);
    assert(low.left == 30 && 1000 - (low.top + low.height) == 30);
  }

  // Clockwise turn: a material red on top, blue below ends red on the right;
  // the size still comes from the material's short edge before turning.
  {
    RgbaSource material = solid(200, 400, 0, 0, 255);
    for (std::uint32_t y = 0; y < 200; ++y) {
      for (std::uint32_t x = 0; x < 200; ++x) {
        auto* p = material.rgba.data() + static_cast<std::size_t>(y) * material.stride + x * 4;
        p[0] = 255;
        p[2] = 0;
      }
    }
    WatermarkPlacement turned = defaults;
    turned.anchor = 4;
    turned.x = turned.y = 0;
    turned.quarter_turns = 1;
    const auto rect = place_watermark(200, 400, turned, 1000, 1000);
    assert(rect.width == 200 && rect.height == 100);
    auto image = solid(1000, 1000, 255, 255, 255);
    assert(draw_watermark(material, turned, image, diagnostic) == ResultCode::Ok);
    const auto* right = at(image, 500 + 60, 500);
    const auto* left = at(image, 500 - 60, 500);
    assert(right[0] == 255 && right[2] == 0 && left[0] == 0 && left[2] == 255);
  }

  // Fit: as wide as the square picture, x ignored, y still 3%; size unused.
  {
    WatermarkPlacement fit = defaults;
    fit.fit = true;
    fit.size = 1;
    const auto rect = place_watermark(400, 200, fit, 814, 814);
    assert(rect.width == 814 && rect.height == 407 && rect.left == 0 && rect.top == 24);
  }

  // Opacity 50 over white; a transparent material leaves the picture alone.
  {
    WatermarkPlacement half = defaults;
    half.opacity = 50;
    auto image = solid(1024, 1536, 255, 255, 255);
    assert(draw_watermark(solid(300, 100, 0, 192, 0), half, image, diagnostic) == ResultCode::Ok);
    const auto* p = at(image, 760, 120);
    std::printf("opacity 50: %d %d %d\n", p[0], p[1], p[2]);
    assert(p[0] == 128 && p[1] == 224 && p[2] == 128 && p[3] == 255);
    auto untouched = solid(1024, 1536, 255, 255, 255);
    assert(draw_watermark(solid(300, 100, 0, 0, 0, 0), defaults, untouched, diagnostic) == ResultCode::Ok);
    assert(changed(untouched).left < 0);
  }

  // Shrinking averages: a 1-pixel checkerboard becomes mid grey, not aliased.
  {
    RgbaSource checker = solid(400, 200, 0, 0, 0);
    for (std::uint32_t y = 0; y < 200; ++y) {
      for (std::uint32_t x = 0; x < 400; ++x) {
        if ((x + y) % 2 == 0) {
          auto* p = checker.rgba.data() + static_cast<std::size_t>(y) * checker.stride + x * 4;
          p[0] = p[1] = p[2] = 255;
        }
      }
    }
    auto image = solid(1000, 1000, 255, 255, 255);
    WatermarkPlacement centre = defaults;
    centre.anchor = 4;
    centre.x = centre.y = 0;
    assert(draw_watermark(checker, centre, image, diagnostic) == ResultCode::Ok);
    const auto* p = at(image, 500, 500);
    assert(p[0] > 110 && p[0] < 145);
  }

  // Empty material: refused.
  {
    auto image = solid(10, 10, 255, 255, 255);
    assert(draw_watermark(RgbaSource{}, defaults, image, diagnostic) == ResultCode::StateConflict);
  }
  std::printf("watermark ok\n");
  return 0;
}
