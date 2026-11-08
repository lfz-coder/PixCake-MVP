// 几何变换（需求 §12.6）：翻转、旋转 90°、拉直和比例裁剪，图像和蒙版同步；描述里的 crop 参数读成几何。
#include <pixcake/gpu/geometry.hpp>
#include <pixcake/mask/gradient_mask.hpp>

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

}  // namespace

int main() {
  test_gradient_masks();
  test_flips_and_turns_move_image_and_mask_together();
  test_straighten_crops_to_the_inscribed_rectangle();
  test_ratio_crops_the_centre();
  test_description_values_become_a_geometry();
  if (failures != 0) return 1;
  std::printf("geometry ok\n");
  return 0;
}
