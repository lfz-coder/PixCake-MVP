#include "pixcake/exportd/image_io.hpp"

#include <cassert>

int main() {
  using namespace pixcake;
  using namespace pixcake::exportd;

  std::string diagnostic;
  ExportEncodeRequest request;
  request.format = ExportFormat::Jpeg;
  request.quality = 8;
  request.limits.max_output_bytes = 1024;
  assert(validate_encode_request(request, diagnostic) == ResultCode::Ok);

  RgbaSource source;
  source.width = 2;
  source.height = 1;
  source.stride = 8;
  source.rgba.assign(8, 255);
  assert(validate_rgba_source(source, diagnostic) == ResultCode::Ok);

  EncodedProduct product;
  product.format = ExportFormat::Jpeg;
  product.bytes.assign(16, 1);
  assert(validate_encoded_product(product, request.limits, diagnostic) == ResultCode::Ok);
  product.bytes.assign(1025, 1);
  assert(validate_encoded_product(product, request.limits, diagnostic) == ResultCode::IoFailed);

  request.format = ExportFormat::Original;
  assert(validate_encode_request(request, diagnostic) == ResultCode::UnsupportedFormat);
  request.format = ExportFormat::Jpeg;
  request.quality = 15;
  assert(validate_encode_request(request, diagnostic) == ResultCode::StateConflict);
  request.quality = 8;
  source.rgba.resize(7);
  assert(validate_rgba_source(source, diagnostic) == ResultCode::StateConflict);
  return 0;
}
