#pragma once

#include "pixcake/exportd/image_io.hpp"
#include "pixcake/exportq/export_settings.hpp"
#include "pixcake/support/result_code.hpp"

#include <cstdint>
#include <string>

namespace pixcake::exportd {

// Draws `material` (premultiplied RGBA, as the decoder returns it) over
// `image` (premultiplied RGBA) with the placement for the image's shape:
// rotated, resampled (area-weighted when shrinking, bilinear when enlarging),
// multiplied by the opacity and composited "over"; parts outside the image are
// clipped.
ResultCode draw_watermark(const RgbaSource& material, const WatermarkPlacement& placement, RgbaSource& image,
                          std::string& diagnostic);

}  // namespace pixcake::exportd
