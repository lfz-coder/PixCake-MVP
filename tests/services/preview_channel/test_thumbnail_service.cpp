// Thumbnails from a real renderd: originals and committed edits, every request
// answered, a re-request makes the older result stale, failures per photo.
#include "pixcake/editdoc/edit_document.hpp"
#include "pixcake/persist/file_system.hpp"
#include "pixcake/preview_channel/thumbnail_service.hpp"

#include <unistd.h>

#include <cassert>
#include <cstdio>
#include <fstream>
#include <map>
#include <string>
#include <vector>

using namespace pixcake;
using namespace pixcake::preview_channel;

namespace {

void write_grey_bmp(const std::string& path, int w, int h, unsigned char grey) {
  const int row = (w * 3 + 3) / 4 * 4;
  const int size = 54 + row * h;
  std::vector<unsigned char> b(static_cast<std::size_t>(size), grey);
  auto put = [&](int at, int value, int bytes) {
    for (int i = 0; i < bytes; ++i) b[static_cast<std::size_t>(at + i)] = static_cast<unsigned char>(value >> (8 * i));
  };
  std::fill(b.begin(), b.begin() + 54, 0);
  b[0] = 'B'; b[1] = 'M';
  put(2, size, 4); put(10, 54, 4); put(14, 40, 4); put(18, w, 4); put(22, h, 4);
  put(26, 1, 2); put(28, 24, 2); put(34, row * h, 4);
  std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(b.data()), size);
}

}  // namespace

int main() {
  const std::string base = std::string(PIXCAKE_TEST_WORK_DIR) + "/thumbnails-" + std::to_string(::getpid());
  const std::string a = base + "-a.bmp";
  const std::string b = base + "-b.bmp";
  write_grey_bmp(a, 200, 100, 90);
  write_grey_bmp(b, 100, 200, 90);

  // B has a committed +1 EV exposure.
  PosixFileSystem files;
  auto document = *EditDocument::create("photo-b");
  assert(document.set_preview("color.global", "exposure", "1.00") == ResultCode::Ok);
  assert(document.commit_preview(0, "color.global", "exposure", phase_one_version_limits(), files,
                                 base + "-b.pxedit") == ResultCode::Ok);

  std::unique_ptr<ThumbnailService> thumbnails;
  std::string diagnostic;
  assert(ThumbnailService::start(PIXCAKE_RENDERD_EXE, base + "-logs", 10'000, thumbnails, diagnostic) == ResultCode::Ok);
  assert(thumbnails->request(1, "photo-a", a, {}, 64, diagnostic) == ResultCode::Ok);
  assert(thumbnails->request(2, "photo-b", b, document.encode(), 64, diagnostic) == ResultCode::Ok);
  assert(thumbnails->request(3, "photo-c", base + "-missing.bmp", {}, 64, diagnostic) == ResultCode::Ok);
  assert(thumbnails->request(1, "photo-a", a, {}, 32, diagnostic) == ResultCode::Ok);  // supersedes the first
  assert(thumbnails->pending() == 4);

  std::map<std::int64_t, ThumbnailService::Thumbnail> got;
  while (got.size() < 3) {
    ThumbnailService::Thumbnail thumbnail;
    assert(thumbnails->wait(thumbnail, 15'000, diagnostic) == ResultCode::Ok);
    assert(got.count(thumbnail.key) == 0);  // the stale result for key 1 never surfaces
    got[thumbnail.key] = std::move(thumbnail);
  }
  assert(thumbnails->pending() == 0);
  assert(got[1].code == ResultCode::Ok && got[1].width == 32 && got[1].height == 16);  // the newer, smaller one
  assert(got[1].rgba.size() == 32u * 4u * 16u && got[1].rgba[0] >= 88 && got[1].rgba[0] <= 92);
  assert(got[2].code == ResultCode::Ok && got[2].width == 32 && got[2].height == 64);
  assert(got[2].rgba[0] > 120);  // the committed exposure is in the thumbnail
  assert(got[3].code == ResultCode::SourceMissing && got[3].rgba.empty());

  ThumbnailService::Thumbnail none;
  assert(thumbnails->take(none, diagnostic) == ResultCode::Cancelled);
  thumbnails.reset();  // renderd exits with the connection
  std::remove(a.c_str());
  std::remove(b.c_str());
  std::printf("thumbnail service ok\n");
  return 0;
}
