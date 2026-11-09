// 几何变换（需求 §12.6）：翻转、旋转 90°、拉直和比例裁剪，图像和蒙版同步；描述里的 crop 参数读成几何。
#include <pixcake/gpu/geometry.hpp>
#include <pixcake/mask/combined_mask.hpp>
#include <pixcake/mask/gradient_mask.hpp>
#include <pixcake/mask/range_mask.hpp>

#include <cmath>
#include <cstdio>

using namespace pixcake;

namespace {
int failures = 0;
void expect(bool ok, const char* text, int line) {
  if (!ok) {
    std::fprintf(stderr, "FAIL %d: %s\n", line, text);
    ++failures;
  }
}
#define EXPECT(value) expect(static_cast<bool>(value), #value, __LINE__)

// 3 列 2 行：像素 (x, y) 的红色是 10*y + x，便于认出位置。
gpu::RgbaImage image_3x2() {
  gpu::RgbaImage image;
  image.width = 3;
  image.height = 2;
  image.row_stride = 3 * 4 + 4;  // padded rows: the transform must honour the stride
  image.pixels.assign(static_cast<std::size_t>(image.row_stride) * 2, 7);
  for (std::uint32_t y = 0; y < 2; ++y) {
    for (std::uint32_t x = 0; x < 3; ++x) {
      auto* p = image.pixels.data() + y * image.row_stride + x * 4;
      p[0] = static_cast<std::uint8_t>(10 * y + x);
      p[1] = 0;
      p[2] = 0;
      p[3] = 255;
    }
  }
  return image;
}

int red(const gpu::RgbaImage& image, std::uint32_t x, std::uint32_t y) {
  return image.pixels[static_cast<std::size_t>(y) * image.row_stride + x * 4];
}

gpu::RenderMask mask_3x2() {
  gpu::RenderMask mask;
  mask.id = "m";
  mask.width = 3;
  mask.height = 2;
  mask.alpha = {1, 2, 3, 11, 12, 13};
  return mask;
}

void test_flips_and_turns_move_image_and_mask_together() {
  std::vector<gpu::RenderMask> masks = {mask_3x2()};
  auto image = image_3x2();
  gpu::Geometry flip;
  flip.flip_horizontal = true;
  EXPECT(gpu::apply_geometry(flip, image, masks));
  EXPECT(image.width == 3 && image.height == 2 && red(image, 0, 0) == 2 && red(image, 2, 1) == 10);
  EXPECT(masks[0].alpha == (std::vector<std::uint8_t>{3, 2, 1, 13, 12, 11}));

  masks = {mask_3x2()};
  image = image_3x2();
  gpu::Geometry vertical;
  vertical.flip_vertical = true;
  EXPECT(gpu::apply_geometry(vertical, image, masks));
  EXPECT(red(image, 0, 0) == 10 && red(image, 0, 1) == 0);
  EXPECT(masks[0].alpha == (std::vector<std::uint8_t>{11, 12, 13, 1, 2, 3}));

  // One clockwise quarter turn: the old bottom-left becomes the new top-left; 3x2 becomes 2x3.
  masks = {mask_3x2()};
  image = image_3x2();
  gpu::Geometry turn;
  turn.quarter_turns = 1;
  EXPECT(gpu::apply_geometry(turn, image, masks));
  EXPECT(image.width == 2 && image.height == 3 && image.row_stride == 8);
  EXPECT(red(image, 0, 0) == 10 && red(image, 1, 0) == 0 && red(image, 0, 2) == 12 && red(image, 1, 2) == 2);
  EXPECT(masks[0].width == 2 && masks[0].height == 3 && masks[0].alpha == (std::vector<std::uint8_t>{11, 1, 12, 2, 13, 3}));

  // Four turns are the identity; two flips undo each other.
  image = image_3x2();
  masks.clear();
  gpu::Geometry full;
  full.quarter_turns = 3;
  EXPECT(gpu::apply_geometry(full, image, masks));
  gpu::Geometry back;
  back.quarter_turns = 1;
  EXPECT(gpu::apply_geometry(back, image, masks));
  const auto original = image_3x2();
  EXPECT(image.width == 3 && image.height == 2);
  for (std::uint32_t y = 0; y < 2; ++y) {
    for (std::uint32_t x = 0; x < 3; ++x) EXPECT(red(image, x, y) == red(original, x, y));
  }
}

void test_straighten_crops_to_the_inscribed_rectangle() {
  gpu::RgbaImage image;
  image.width = 400;
  image.height = 200;
  image.pixels.assign(static_cast<std::size_t>(400) * 200 * 4, 200);
  std::vector<gpu::RenderMask> masks;
  gpu::Geometry slight;
  slight.angle = 10.0;
  EXPECT(gpu::apply_geometry(slight, image, masks));
  const double c = std::cos(10.0 * 3.14159265358979 / 180.0);
  const double s = std::sin(10.0 * 3.14159265358979 / 180.0);
  const double fit = std::min(400.0 / (400 * c + 200 * s), 200.0 / (400 * s + 200 * c));
  EXPECT(image.width == static_cast<std::uint32_t>(std::lround(400 * fit)) &&
         image.height == static_cast<std::uint32_t>(std::lround(200 * fit)));
  EXPECT(image.width < 400 && image.height < 200);
  EXPECT(std::abs(static_cast<double>(image.width) / image.height - 2.0) < 0.02);  // the aspect ratio is kept
  // A flat image stays flat (the crop never reaches outside the source): every pixel keeps its value.
  bool flat = true;
  for (std::size_t i = 0; i < image.pixels.size(); ++i) flat = flat && image.pixels[i] == 200;
  EXPECT(flat);
  // Out of range is refused and changes nothing.
  gpu::Geometry too_much;
  too_much.angle = 60.0;
  const auto before = image.width;
  EXPECT(!gpu::apply_geometry(too_much, image, masks) && image.width == before);
}

void test_ratio_crops_the_centre() {
  gpu::RgbaImage image;
  image.width = 400;
  image.height = 200;
  image.pixels.assign(static_cast<std::size_t>(400) * 200 * 4, 0);
  for (std::uint32_t y = 0; y < 200; ++y) {
    for (std::uint32_t x = 0; x < 400; ++x) image.pixels[(static_cast<std::size_t>(y) * 400 + x) * 4] = static_cast<std::uint8_t>(x / 2);
  }
  std::vector<gpu::RenderMask> masks;
  gpu::Geometry square;
  square.ratio = "1:1";
  EXPECT(gpu::apply_geometry(square, image, masks));
  EXPECT(image.width == 200 && image.height == 200 && red(image, 0, 0) == 50 && red(image, 199, 0) == 149);
  gpu::Geometry tall;
  tall.ratio = "9:16";
  EXPECT(gpu::apply_geometry(tall, image, masks));
  EXPECT(image.width == 113 && image.height == 200);
  gpu::Geometry unknown;
  unknown.ratio = "nonsense";
  const auto width = image.width;
  EXPECT(gpu::apply_geometry(unknown, image, masks) && image.width == width);  // an unknown ratio crops nothing
}

void test_description_values_become_a_geometry() {
  render::RenderDescription description;
  render::RenderOperation crop;
  crop.capability_id = "crop";
  crop.values = {{"rotate", "rotate", "3", 3.0}, {"flip_horizontal", "flip_horizontal", "on", 0.0},
                 {"flip_vertical", "flip_vertical", "off", 0.0}, {"angle", "angle", "-12.5", -12.5},
                 {"ratio", "ratio", "4:3", 0.0}};
  description.operations = {crop};
  const auto geometry = gpu::geometry_of(description);
  EXPECT(geometry.quarter_turns == 3 && geometry.flip_horizontal && !geometry.flip_vertical && geometry.angle == -12.5 &&
         geometry.ratio == "4:3" && !geometry.identity());
  description.operations[0].enabled = false;
  EXPECT(gpu::geometry_of(description).identity());
  EXPECT(gpu::geometry_of(render::RenderDescription{}).identity());
}

// 渐变蒙版：编号往返；线性从一侧到另一侧单调下降、中心约一半；径向中心满、边缘空；位置按画幅百分比。
void test_gradient_masks() {
  mask::GradientMask g{false, 50, 50, 0, 50, 0, 100};
  mask::GradientMask parsed;
  EXPECT(mask::parse_gradient_mask_id(mask::gradient_mask_id(g), parsed) && !parsed.radial && parsed.size == 50);
  EXPECT(!mask::parse_gradient_mask_id("gradient:sideways:1:2:3:4:5:100", parsed));
  EXPECT(!mask::parse_gradient_mask_id("gradient:linear:101:0:0:0:0:100", parsed));
  EXPECT(!mask::parse_gradient_mask_id("gradient:linear:1:2:3:4:5", parsed));
  EXPECT(!mask::parse_gradient_mask_id("semantic:person.hair", parsed));

  render::RenderDescription description;
  render::RenderOperation operation;
  operation.capability_id = "color.local";
  operation.mask_ids = {mask::gradient_mask_id(g), mask::gradient_mask_id({true, 25, 50, 0, 40, 0, 100}), mask::gradient_mask_id({true, 75, 50, 0, 40, 0, 400}), "gradient:bad", "semantic:x"};
  description.operations = {operation};
  std::vector<gpu::RenderMask> masks;
  gpu::add_gradient_masks(description, 200, 100, masks);
  EXPECT(masks.size() == 3);
  gpu::add_gradient_masks(description, 200, 100, masks);
  EXPECT(masks.size() == 3);  // generated once
  if (masks.size() == 3) {
    const auto at = [](const gpu::RenderMask& m, std::uint32_t x, std::uint32_t y) { return m.alpha[y * m.width + x]; };
    const auto& linear = masks[0];
    EXPECT(linear.width == 200 && linear.height == 100);
    EXPECT(at(linear, 0, 50) == 255 && at(linear, 199, 50) == 0);
    EXPECT(std::abs(static_cast<int>(at(linear, 100, 50)) - 128) <= 2);
    for (std::uint32_t x = 1; x < 200; ++x) EXPECT(at(linear, x, 50) <= at(linear, x - 1, 50));
    const auto& radial = masks[1];  // centre at 25% across, hard edge, radius 20% of the diagonal
    EXPECT(at(radial, 50, 50) == 255 && at(radial, 150, 50) == 0 && at(radial, 50, 0) == 0);
    // An ellipse four times as tall as wide: 20 px up from the centre is still inside, 20 px sideways is not.
    const auto& tall = masks[2];
    EXPECT(at(tall, 150, 50) == 255 && at(tall, 150, 15) > 0 && at(tall, 150 + 50, 50) == 0);
  }
}

// 组合蒙版：编号往返；添加取并集，减去乘 (1 − 子项)；缺子项蒙版时不生成；渐变子项在组合里也会被生成。
void test_combined_masks() {
  std::vector<mask::CombinedPart> parts = {{false, "gradient:linear:50:50:0:50:0:100"}, {true, "gradient:radial:25:50:0:40:0:100"}};
  const std::string id = mask::combined_mask_id(parts);
  std::vector<mask::CombinedPart> parsed;
  EXPECT(mask::parse_combined_mask_id(id, parsed) && parsed.size() == 2 && !parsed[0].subtract && parsed[1].subtract &&
         parsed[1].id == parts[1].id);
  EXPECT(!mask::parse_combined_mask_id("combined:", parsed) && !mask::parse_combined_mask_id("combined:x", parsed) &&
         !mask::parse_combined_mask_id("combined:+a|", parsed) && !mask::parse_combined_mask_id("semantic:a", parsed));
  EXPECT(mask::leaf_mask_ids(id) == (std::vector<std::string>{parts[0].id, parts[1].id}));
  EXPECT(mask::leaf_mask_ids("semantic:person.hair") == std::vector<std::string>{"semantic:person.hair"});

  render::RenderDescription description;
  render::RenderOperation operation;
  operation.capability_id = "color.local";
  operation.mask_ids = {id};
  description.operations = {operation};
  std::vector<gpu::RenderMask> masks;
  gpu::add_combined_masks(description, masks);
  EXPECT(masks.empty());  // the leaves are not there yet
  gpu::add_gradient_masks(description, 200, 100, masks);
  EXPECT(masks.size() == 2);  // the gradients inside the combination are generated
  gpu::add_combined_masks(description, masks);
  EXPECT(masks.size() == 3 && masks[2].id == id);
  if (masks.size() == 3) {
    const auto at = [](const gpu::RenderMask& m, std::uint32_t x, std::uint32_t y) { return m.alpha[y * m.width + x]; };
    // Left half bright (linear), minus a hard-edged-ish circle at 25%: inside the circle the result is cut away.
    EXPECT(at(masks[2], 5, 5) == 255);                  // far from the circle, linear is full
    EXPECT(at(masks[2], 50, 50) < 30);                  // circle centre: linear 255 x (1 - 255/255) ~ 0
    EXPECT(at(masks[2], 150, 50) < 30);                 // linear is nearly empty here
    // Union: add both.
    description.operations[0].mask_ids = {mask::combined_mask_id({{false, parts[0].id}, {false, parts[1].id}})};
    gpu::add_combined_masks(description, masks);
    EXPECT(masks.size() == 4 && at(masks[3], 5, 5) == 255 && at(masks[3], 150, 50) < 30);
  }
}

}  // namespace

// 颜色范围、亮度范围蒙版：编号往返和非法编号；颜色范围离取样色越远越弱、调整越大越宽；亮度范围是梯形；
// 组合编号里的范围子项也生成，并且能和别的子项组合。
void test_range_masks() {
  mask::ColorRangeMask color;
  mask::LuminanceRangeMask luminance;
  EXPECT(mask::parse_color_range_mask_id(mask::color_range_mask_id({10, 20, 30, 70}), color) && color.r == 10 &&
         color.g == 20 && color.b == 30 && color.adjust == 70);
  EXPECT(!mask::parse_color_range_mask_id("colorrange:256:0:0:50", color));
  EXPECT(!mask::parse_color_range_mask_id("colorrange:1:2:3:0", color));  // 调整最小是 1
  EXPECT(!mask::parse_color_range_mask_id("colorrange:1:2:3", color));
  EXPECT(mask::parse_luminance_range_mask_id(mask::luminance_range_mask_id({20, 45, 65, 90}), luminance) && luminance.c == 65);
  EXPECT(!mask::parse_luminance_range_mask_id("lumrange:50:40:60:90", luminance));  // 次序不对
  EXPECT(!mask::parse_luminance_range_mask_id("lumrange:0:10:20:101", luminance));

  // 一行 6 个像素：取样色本身、稍偏、较远、很远（白）、黑、40% 灰。
  gpu::RgbaImage image;
  image.width = 6;
  image.height = 1;
  image.row_stride = 6 * 4;
  const std::uint8_t pixels[6][3] = {{123, 141, 163}, {133, 141, 163}, {160, 150, 150}, {250, 250, 250}, {0, 0, 0}, {102, 102, 102}};
  for (const auto& p : pixels) image.pixels.insert(image.pixels.end(), {p[0], p[1], p[2], 255});
  render::RenderDescription description;
  render::RenderOperation operation;
  operation.capability_id = "color.local";
  const std::string narrow = mask::color_range_mask_id({123, 141, 163, 1});
  const std::string wide = mask::color_range_mask_id({123, 141, 163, 100});
  const std::string lum = mask::luminance_range_mask_id({20, 45, 65, 90});
  operation.mask_ids = {narrow, wide, lum, "colorrange:bad"};
  description.operations = {operation};
  std::vector<gpu::RenderMask> masks;
  gpu::add_range_masks(description, image, masks);
  gpu::add_range_masks(description, image, masks);
  EXPECT(masks.size() == 3);  // generated once, bad id ignored
  const auto find = [&](const std::string& id) -> const gpu::RenderMask* {
    for (const auto& m : masks) if (m.id == id) return &m;
    return nullptr;
  };
  const auto* n = find(narrow);
  const auto* w = find(wide);
  const auto* l = find(lum);
  EXPECT(n && w && l && n->alpha.size() == 6);
  if (n && w && l && n->alpha.size() == 6) {
    EXPECT(n->alpha[0] == 255 && w->alpha[0] == 255);            // the sample itself
    EXPECT(n->alpha[0] > n->alpha[1] && n->alpha[1] >= n->alpha[2] && n->alpha[2] >= n->alpha[3]);  // farther is weaker
    EXPECT(n->alpha[3] == 0 && n->alpha[4] == 0);                // white and black are far from the sample
    for (int i = 0; i < 6; ++i) EXPECT(w->alpha[static_cast<std::size_t>(i)] >= n->alpha[static_cast<std::size_t>(i)]);  // wider adjust never selects less
    EXPECT(w->alpha[2] > n->alpha[2]);
    // Luminance: the sample is about 55 (inside 45..65: full); white 98 and black 0 are outside.
    EXPECT(l->alpha[0] == 255 && l->alpha[3] == 0 && l->alpha[4] == 0);
    EXPECT(l->alpha[2] == 255);                                  // 59.7 is inside 45..65
    EXPECT(l->alpha[5] > 190 && l->alpha[5] < 215);              // 40: (40-20)/(45-20) = 0.8 on the rising edge
  }
  // Combined ids carry range leaves: they are generated, then subtracted from each other.
  render::RenderOperation combined_operation;
  combined_operation.capability_id = "color.local";
  combined_operation.mask_ids = {mask::combined_mask_id({{false, wide}, {true, narrow}})};
  render::RenderDescription combined;
  combined.operations = {combined_operation};
  std::vector<gpu::RenderMask> pieces;
  gpu::add_range_masks(combined, image, pieces);
  gpu::add_combined_masks(combined, pieces);
  EXPECT(pieces.size() == 3);
  if (pieces.size() == 3) {
    EXPECT(pieces[2].alpha[0] == 0);         // sample: wide 255 × (1 − narrow 255) = 0
    EXPECT(pieces[2].alpha[2] > 0);          // a farther pixel stays: wide minus a weak narrow
  }
}

// 反相：编号往返；leaf_mask_ids 展开成里面的叶子；反相像素是 255 − 子项；可以放进组合编号；子项缺了不生成。
void test_invert_masks() {
  std::string inner;
  EXPECT(mask::parse_invert_mask_id(mask::invert_mask_id("gradient:linear:50:50:0:10:0:100"), inner) &&
         inner == "gradient:linear:50:50:0:10:0:100");
  EXPECT(!mask::parse_invert_mask_id("invert:", inner) && !mask::parse_invert_mask_id("semantic:person.hair", inner));
  const auto leaves = mask::leaf_mask_ids("combined:+invert:semantic:person.hair|-gradient:linear:50:50:0:10:0:100");
  EXPECT(leaves.size() == 2 && leaves[0] == "semantic:person.hair" && leaves[1] == "gradient:linear:50:50:0:10:0:100");
  EXPECT(mask::leaf_mask_ids("invert:semantic:person.hair") == std::vector<std::string>{"semantic:person.hair"});

  const std::string gradient = "gradient:linear:50:50:0:10:0:100";
  render::RenderDescription description;
  render::RenderOperation operation;
  operation.capability_id = "color.local";
  operation.mask_ids = {mask::invert_mask_id(gradient), mask::combined_mask_id({{false, mask::invert_mask_id(gradient)}, {false, "semantic:person.hair"}}),
                        mask::invert_mask_id("semantic:person.eyes")};  // the last one's source never exists
  description.operations = {operation};
  std::vector<gpu::RenderMask> masks;
  gpu::add_gradient_masks(description, 40, 10, masks);
  gpu::add_invert_masks(description, masks);
  gpu::add_invert_masks(description, masks);
  EXPECT(masks.size() == 2);  // the gradient and its inverse, once; no inverse of a missing mask
  const gpu::RenderMask* base = nullptr;
  const gpu::RenderMask* inverted = nullptr;
  for (const auto& m : masks) (m.id == gradient ? base : inverted) = &m;
  EXPECT(base && inverted && base->alpha.size() == inverted->alpha.size());
  if (base && inverted && base->alpha.size() == inverted->alpha.size()) {
    bool exact = true;
    for (std::size_t i = 0; i < base->alpha.size(); ++i) exact = exact && base->alpha[i] + inverted->alpha[i] == 255;
    EXPECT(exact);
    EXPECT(base->alpha[5] == 255 && inverted->alpha[5] == 0);  // left side: selected -> not selected
  }
}

int main() {
  test_invert_masks();
  test_range_masks();
  test_combined_masks();
  test_gradient_masks();
  test_flips_and_turns_move_image_and_mask_together();
  test_straighten_crops_to_the_inscribed_rectangle();
  test_ratio_crops_the_centre();
  test_description_values_become_a_geometry();
  if (failures != 0) return 1;
  std::printf("geometry ok\n");
  return 0;
}
