// Export photo layout, against what the test build showed
// (docs/superpowers/evidence/2026-11-07-print-layout-observation.md).
#include "pixcake/exportd/layout.hpp"

#include <cassert>
#include <cstdio>

namespace {

using namespace pixcake;
using namespace pixcake::exportd;

// A photo whose red channel runs 0..255 down the rows (so which part of it shows is readable),
// green is the column, blue is 7.
RgbaSource ramp(std::uint32_t width, std::uint32_t height) {
  RgbaSource image;
  image.width = width;
  image.height = height;
  image.stride = width * 4;
  image.rgba.resize(static_cast<std::size_t>(width) * height * 4);
  for (std::uint32_t y = 0; y < height; ++y) {
    for (std::uint32_t x = 0; x < width; ++x) {
      std::uint8_t* p = image.rgba.data() + (static_cast<std::size_t>(y) * width + x) * 4;
      p[0] = static_cast<std::uint8_t>(y * 255 / (height - 1));
      p[1] = static_cast<std::uint8_t>(x * 255 / (width - 1));
      p[2] = 7;
      p[3] = 255;
    }
  }
  return image;
}
const std::uint8_t* at(const RgbaSource& image, std::uint32_t x, std::uint32_t y) {
  return image.rgba.data() + static_cast<std::size_t>(y) * image.stride + x * 4;
}
bool white(const RgbaSource& image, std::uint32_t x, std::uint32_t y) {
  const auto* p = at(image, x, y);
  return p[0] == 255 && p[1] == 255 && p[2] == 255;
}
// Bounding box of the non-white pixels.
struct Box { long left = -1, top = -1, right = -1, bottom = -1; };
Box content(const RgbaSource& image) {
  Box box;
  for (std::uint32_t y = 0; y < image.height; ++y) {
    for (std::uint32_t x = 0; x < image.width; ++x) {
      if (white(image, x, y)) continue;
      if (box.left < 0 || static_cast<long>(x) < box.left) box.left = x;
      if (static_cast<long>(x) > box.right) box.right = x;
      if (box.top < 0) box.top = y;
      box.bottom = y;
    }
  }
  return box;
}

}  // namespace

int main() {
  std::string why;
  ExportLayout layout;  // 6寸 横 152x102 mm, 300 dpi, 1x1, gap 10
  assert(layout_page_width(layout) == 1795 && layout_page_height(layout) == 1205);

  // Settings text round trip and refusals.
  {
    ExportSettings settings;
    settings.layout = layout;
    settings.layout->rows = 2;
    settings.layout->margin_top_mm = 11;
    const std::string text = encode_export_settings(settings);
    assert(text == "format=original;layout=1,152,102,300,2,1,10,11,0,0,0");
    ExportSettings back;
    assert(parse_export_settings(text, back) == ResultCode::Ok && back.layout && *back.layout == *settings.layout);
    ExportSettings bad;
    assert(parse_export_settings("layout=1,152,102,300,2,1,10,11,0,0", bad) != ResultCode::Ok);   // ten numbers
    assert(parse_export_settings("layout=2,152,102,300,2,1,10,11,0,0,0", bad) != ResultCode::Ok); // kind 2
    assert(parse_export_settings("layout=1,152,102,300,0,1,10,0,0,0,0", bad) != ResultCode::Ok);  // 0 rows
    assert(parse_export_settings("layout=1,2000,2000,1200,1,1,10,0,0,0,0", bad) != ResultCode::Ok);  // page too big
  }

  // Normal layout: the 1024x1536 photo fits the 1795x1205 page by height (803 wide), centred, x 496..1298 (measured).
  {
    auto image = ramp(1024, 1536);
    layout.id_photo = false;
    assert(compose_layout(layout, image, why) == ResultCode::Ok);
    assert(image.width == 1795 && image.height == 1205);
    const Box box = content(image);
    assert(box.left >= 495 && box.left <= 497 && box.right >= 1297 && box.right <= 1299);
    assert(box.top == 0 && box.bottom == 1204);
    assert(white(image, 100, 600) && white(image, 1700, 600));
  }
  // Margins are a window: 11 mm top / bottom / left, 0 right leave the photo where it was (measured: y 130..1074).
  {
    auto image = ramp(1024, 1536);
    layout.margin_top_mm = layout.margin_bottom_mm = layout.margin_left_mm = 11;
    assert(compose_layout(layout, image, why) == ResultCode::Ok);
    const Box box = content(image);
    assert(box.left >= 495 && box.left <= 497 && box.right >= 1297 && box.right <= 1299);
    assert(box.top >= 130 && box.top <= 131 && box.bottom >= 1073 && box.bottom <= 1074);
    layout.margin_top_mm = layout.margin_bottom_mm = layout.margin_left_mm = 0;
  }
  // Id layout 1x1: the middle 1795x1205 window of the 1:1 photo; the photo is only 1024 wide so it sits centred
  // with white on both sides.
  {
    auto image = ramp(1024, 1536);
    layout.id_photo = true;
    assert(compose_layout(layout, image, why) == ResultCode::Ok);
    const Box box = content(image);
    assert(box.left == 385 && box.right == 1408 && box.top == 0 && box.bottom == 1204);
    // The window starts at photo row (1536 - 1205) / 2 = 165.
    assert(at(image, 800, 0)[0] == 165 * 255 / 1535);
  }
  // Id layout 2 rows x 1: the 3082-high canvas is centred on the page (origin -939), so the seam falls mid-page:
  // the top half shows the photo's bottom, the bottom half its top.
  {
    auto image = ramp(1024, 1536);
    layout.rows = 2;
    assert(compose_layout(layout, image, why) == ResultCode::Ok);
    // Last row of the top cell (y = 596) is the photo's last row; first row of the bottom cell (y = 607) is its first.
    assert(at(image, 800, 596)[0] == 255);
    assert(at(image, 800, 607)[0] == 0);
    for (std::uint32_t x = 0; x < image.width; ++x) assert(white(image, x, 600));  // the gap
    // Cut lines: pure black at y 598 and 606 beside the photos, none across the photos' columns (官方 _(6)).
    for (const std::uint32_t y : {598u, 606u}) {
      assert(at(image, 0, y)[0] == 0 && at(image, 384, y)[0] == 0 && at(image, 1409, y)[0] == 0 &&
             at(image, 1794, y)[0] == 0);
      assert(white(image, 385, y) && white(image, 800, y) && white(image, 1408, y));
    }
    layout.rows = 1;
  }
  // Id layout 2x2: the cut lines skip the whole block of columns, the gap between them too (官方 _(2)).
  {
    auto image = ramp(1024, 1536);
    layout.rows = layout.cols = 2;
    assert(compose_layout(layout, image, why) == ResultCode::Ok);
    assert(white(image, 896, 598) && white(image, 896, 606));
    layout.rows = layout.cols = 1;
  }
  // Id layout 1 row x 2 columns: two 892-wide cells, the photo centred in each, vertically centred.
  {
    auto image = ramp(1024, 1536);
    layout.cols = 2;
    assert(compose_layout(layout, image, why) == ResultCode::Ok);
    assert(!white(image, 10, 600) && !white(image, 1790, 600));
    assert(white(image, 896, 600));  // the 10 px gap between the cells (x 892..901)
    layout.cols = 1;
  }
  // Even row counts put the seam mid-page, so 4 rows look like 2; odd counts centre on the middle photo: one photo.
  {
    auto two = ramp(1024, 1536);
    layout.rows = 2;
    assert(compose_layout(layout, two, why) == ResultCode::Ok);
    auto four = ramp(1024, 1536);
    layout.rows = 4;
    assert(compose_layout(layout, four, why) == ResultCode::Ok);
    assert(two.rgba == four.rgba);
    auto three = ramp(1024, 1536);
    layout.rows = 3;
    assert(compose_layout(layout, three, why) == ResultCode::Ok);
    auto one = ramp(1024, 1536);
    layout.rows = 1;
    assert(compose_layout(layout, one, why) == ResultCode::Ok);
    assert(three.rgba == one.rgba);
  }
  // A transparent photo is laid over white.
  {
    RgbaSource clear;
    clear.width = clear.height = 4;
    clear.stride = 16;
    clear.rgba.assign(64, 0);
    layout.id_photo = false;
    assert(compose_layout(layout, clear, why) == ResultCode::Ok && white(clear, 900, 600));
  }
  // The geometry the preview shares: the 2-row id layout on a 1024x1536 photo puts the photos at y -939 and 607.
  {
    ExportLayout two;
    two.rows = 2;
    LayoutGeometry geometry;
    assert(layout_geometry(two, 1024, 1536, geometry));
    assert(geometry.page_width == 1795 && geometry.page_height == 1205 && !geometry.scaled);
    assert(geometry.photos.size() == 2 && geometry.photos[0].top == -939 && geometry.photos[1].top == 607);
    assert(geometry.photos[0].left == 385 && geometry.photos[0].width == 1024 && geometry.photos[0].height == 1536);
    // Cut lines as the test build drew them: 598 and 606 (官方 _(6)); with an 11 mm top margin also 129 (_(9)).
    assert((geometry.cut_rows == std::vector<std::int64_t>{598, 606}));
    assert(geometry.cut_skip_left == 385 && geometry.cut_skip_right == 1409);
    two.margin_top_mm = 11;
    assert(layout_geometry(two, 1024, 1536, geometry));
    assert((geometry.cut_rows == std::vector<std::int64_t>{129, 598, 606}));
    two.margin_top_mm = 0;
    two.id_photo = false;
    assert(layout_geometry(two, 1024, 1536, geometry) && geometry.scaled && geometry.photos.size() == 1);
    assert(geometry.cut_rows.empty());
    assert(geometry.photos[0].height == 1205 && geometry.photos[0].width == 803 && geometry.photos[0].left == 496);
    two.paper_width_mm = 0;
    assert(!layout_geometry(two, 1024, 1536, geometry));
  }
  std::puts("layout ok");
  return 0;
}
