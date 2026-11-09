#include "pixcake/ui/panels/histogram.hpp"

#include <cassert>
#include <vector>

int main() {
  using namespace pixcake::ui::panels;
  // 3x2 frame with a padded row stride of 16 bytes.
  std::vector<std::uint8_t> frame(16 * 2, 0xEE);
  const std::uint8_t px[6][4] = {{10, 20, 30, 255}, {10, 200, 30, 255}, {255, 0, 0, 255},
                                 {10, 20, 30, 255}, {1, 2, 3, 0}, {0, 0, 255, 128}};
  for (int i = 0; i < 6; ++i) {
    for (int c = 0; c < 4; ++c) frame[(i / 3) * 16 + (i % 3) * 4 + c] = px[i][c];
  }
  const auto h = rgb_histogram(frame.data(), 3, 2, 16, frame.size());
  assert(h.pixels == 5);  // the transparent pixel is skipped
  assert(h.red[10] == 3 && h.red[255] == 1 && h.red[0] == 1 && h.red[1] == 0);
  assert(h.green[20] == 2 && h.green[200] == 1 && h.green[0] == 2);
  assert(h.blue[30] == 3 && h.blue[0] == 1 && h.blue[255] == 1);
  assert(h.peak == 3);
  // Display height: linear, full height = counted pixels / 40, clipped at 1 (measured on PixCake 9.3.0).
  RgbHistogram tall;
  tall.pixels = 4000;  // full height is 100 pixels
  assert(histogram_bar_height(tall, 0) == 0.0);
  assert(histogram_bar_height(tall, 25) == 0.25);
  assert(histogram_bar_height(tall, 100) == 1.0);
  assert(histogram_bar_height(tall, 3000) == 1.0);  // a background peak is clipped, not used as the scale
  assert(histogram_bar_height(RgbHistogram{}, 7) == 0.0);
  // Invalid input: empty.
  assert(rgb_histogram(frame.data(), 3, 2, 11, frame.size()).pixels == 0);
  assert(rgb_histogram(frame.data(), 3, 3, 16, frame.size()).pixels == 0);
  assert(rgb_histogram(nullptr, 3, 2, 16, frame.size()).peak == 0);
  return 0;
}
