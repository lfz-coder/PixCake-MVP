#include "pixcake/renderd/image_io.hpp"

#include <cassert>

int main() {
  using namespace pixcake;
  using namespace pixcake::renderd;

  std::string diagnostic;
  DecodeRequest request;
  request.source_path = "/tmp/input.jpg";
  request.limits = {4000, 4000, 16'000'000, 64'000'000};
  assert(validate_decode_request(request, diagnostic) == ResultCode::Ok);

  DecodedImage image;
  image.source_format = SourceFormat::Jpeg;
  image.width = 2;
  image.height = 1;
  image.stride = 8;
  image.bytes.assign(8, 255);
  assert(validate_decoded_image(image, request.limits, diagnostic) == ResultCode::Ok);

  image.stride = 4;
  assert(validate_decoded_image(image, request.limits, diagnostic) == ResultCode::ImageCorrupt);
  image.stride = 8;
  image.bytes.resize(7);
  assert(validate_decoded_image(image, request.limits, diagnostic) == ResultCode::ImageCorrupt);

  request.limits = {};
  assert(validate_decode_request(request, diagnostic) == ResultCode::StateConflict);
  request.limits = {4000, 4000, 16'000'000, 64'000'000};
  request.source_path.clear();
  assert(validate_decode_request(request, diagnostic) == ResultCode::SourceMissing);
  return 0;
}
