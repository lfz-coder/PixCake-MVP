#include "pixcake/export_session/export_charge.hpp"

#include "pixcake/capability/capability_registry.hpp"
#include "pixcake/editdoc/edit_document.hpp"
#include "pixcake/platform/mac_image_decoder.hpp"
#include "pixcake/render/render_description.hpp"

#include <algorithm>
#include <bitset>
#include <cmath>
#include <cstdio>

namespace pixcake::export_session {
namespace {

bool free_capability(const std::string& id) {
  return id == "color.global" || id == "detail" || id == "crop" || id.rfind("manual.", 0) == 0;
}

bool neutral_capability(const std::string& id) { return id == "mask" || id == "layer"; }

bool value_used(const std::string& capability, const render::RenderValue& value) {
  const ParameterMode* mode = find_parameter_mode(capability, value.parameter_id);
  if (!mode) return true;
  if (!mode->choices.empty()) return value.display_value != mode->default_choice;
  return std::fabs(value.numeric_value - mode->default_value) > 1e-9;
}

bool operation_used(const render::RenderOperation& operation) {
  if (!operation.enabled) return false;
  if (operation.values.empty()) return true;  // strokes, masks, generated layers: their presence is the edit
  for (const auto& value : operation.values) {
    if (value_used(operation.capability_id, value)) return true;
  }
  return false;
}

constexpr int kHashWidth = 16;
constexpr int kHashHeight = 16;

int hex_digit(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return -1;
}

}  // namespace

ExportEffect classify_export(const std::vector<std::uint8_t>& document_snapshot) {
  const auto document = EditDocument::decode(document_snapshot);
  if (!document) return ExportEffect::Refined;
  const render::CompileResult compiled = render::compile(*document);
  if (compiled.code != ResultCode::Ok) return ExportEffect::Refined;
  bool any = false;
  for (const auto& operation : compiled.description.operations) {
    if (neutral_capability(operation.capability_id) || !operation_used(operation)) continue;
    if (!free_capability(operation.capability_id)) return ExportEffect::Refined;
    any = true;
  }
  return any ? ExportEffect::Free : ExportEffect::Original;
}

ResultCode picture_fingerprint(const std::string& path, std::string& out, std::string& diagnostic) {
  out.clear();
  platform::MacDecodeOptions options;
  options.max_edge = 64;
  options.max_source_width = 65535;
  options.max_source_height = 65535;
  options.max_source_pixels = 1ull << 32;
  platform::MacDecodedImage image;
  const ResultCode code = platform::mac_decode_image(path, options, image, diagnostic);
  if (code != ResultCode::Ok) return code;
  if (image.width == 0 || image.height == 0) {
    diagnostic = "empty image";
    return ResultCode::ImageCorrupt;
  }
  // Grey on white (premultiplied RGB plus the uncovered white), then box
  // averages on a (16 + 1) × 16 grid; each bit says "brighter than the cell to the right".
  std::vector<double> grey(static_cast<std::size_t>(image.width) * image.height);
  for (std::uint32_t y = 0; y < image.height; ++y) {
    for (std::uint32_t x = 0; x < image.width; ++x) {
      const std::uint8_t* p = image.rgba.data() + static_cast<std::size_t>(y) * image.stride + x * 4;
      const double white = image.has_alpha ? 255.0 - p[3] : 0.0;
      grey[static_cast<std::size_t>(y) * image.width + x] =
          0.299 * (p[0] + white) + 0.587 * (p[1] + white) + 0.114 * (p[2] + white);
    }
  }
  const auto cell = [&](int column, int row, int columns, int rows) {
    std::uint32_t x0 = image.width * column / columns;
    std::uint32_t x1 = std::max(x0 + 1, image.width * (column + 1) / columns);
    std::uint32_t y0 = image.height * row / rows;
    std::uint32_t y1 = std::max(y0 + 1, image.height * (row + 1) / rows);
    x0 = std::min(x0, image.width - 1);
    y0 = std::min(y0, image.height - 1);
    x1 = std::min(x1, image.width);
    y1 = std::min(y1, image.height);
    double sum = 0.0;
    for (std::uint32_t y = y0; y < y1; ++y) {
      for (std::uint32_t x = x0; x < x1; ++x) sum += grey[static_cast<std::size_t>(y) * image.width + x];
    }
    return sum / static_cast<double>((x1 - x0) * (y1 - y0));
  };
  std::string bits;
  for (int row = 0; row < kHashHeight; ++row) {
    for (int column = 0; column < kHashWidth; ++column) {
      bits.push_back(cell(column, row, kHashWidth + 1, kHashHeight) >
                             cell(column + 1, row, kHashWidth + 1, kHashHeight) + 0.5
                         ? '1'
                         : '0');
    }
  }
  std::string hex;
  for (std::size_t i = 0; i < bits.size(); i += 4) {
    const int nibble = (bits[i] - '0') << 3 | (bits[i + 1] - '0') << 2 | (bits[i + 2] - '0') << 1 | (bits[i + 3] - '0');
    hex.push_back("0123456789abcdef"[nibble]);
  }
  out = "pic-" + std::to_string(image.source_width) + "x" + std::to_string(image.source_height) + "-" + hex;
  diagnostic.clear();
  return ResultCode::Ok;
}

bool same_picture(const std::string& a, const std::string& b) {
  if (a == b) return true;
  const std::size_t dash_a = a.rfind('-');
  const std::size_t dash_b = b.rfind('-');
  if (a.rfind("pic-", 0) != 0 || b.rfind("pic-", 0) != 0 || dash_a == std::string::npos ||
      dash_b == std::string::npos || a.compare(0, dash_a, b, 0, dash_b) != 0 || a.size() != b.size()) {
    return false;
  }
  int differing = 0;
  for (std::size_t i = dash_a + 1; i < a.size(); ++i) {
    const int x = hex_digit(a[i]);
    const int y = hex_digit(b[i]);
    if (x < 0 || y < 0) return false;
    differing += static_cast<int>(std::bitset<4>(static_cast<unsigned>(x ^ y)).count());
  }
  return differing <= kSamePictureBits;
}

}  // namespace pixcake::export_session
