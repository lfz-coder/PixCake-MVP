// 测试清单 T-08：恢复为空的图层文档保存后能原样读回（从 tests/kernel/layerdoc/test_layer_document.cpp 迁来）。
#include "pixcake/editdoc/edit_document.hpp"
#include "pixcake/layerdoc/layer_document.hpp"
#include "pixcake/support/checksum.hpp"
#include "support/fake_file_system.hpp"

#include <cstdio>
#include <string>
#include <vector>

namespace {

int g_failures = 0;

void expect(bool cond, const char* text, int line) {
  if (!cond) {
    std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, line, text);
    ++g_failures;
  }
}

#define EXPECT(cond) expect(static_cast<bool>(cond), #cond, __LINE__)

class Latch final : public pixcake::FastExportLatch {
 public:
  bool on = false;
  bool enabled() const override { return on; }
};











// 去掉某一类记录并重算校验和，用来构造旧版本写出的图层文件。



}  // namespace

void test_recovered_empty_save_round_trips() {
  pixcake::test::FakeFileSystem files;
  files.files["/creative/bad.pxlayer"] = {1, 2, 3, 4};
  pixcake::LayerDocument destination;
  const pixcake::LoadResult corrupt = pixcake::LayerDocument::load(destination, files, "/creative/bad.pxlayer");
  EXPECT(corrupt.recovered_empty);
  EXPECT(corrupt.code == pixcake::ResultCode::DocumentCorrupt);
  EXPECT(destination.recovered_empty());
  EXPECT(destination.source_image_id().empty());
  EXPECT(destination.save(files, "/creative/bad.pxlayer") == pixcake::ResultCode::Ok);
  pixcake::LayerDocument second;
  const pixcake::LoadResult ok = pixcake::LayerDocument::load(second, files, "/creative/bad.pxlayer");
  EXPECT(ok.code == pixcake::ResultCode::Ok);
  EXPECT(second.recovered_empty());
  EXPECT(second.source_image_id().empty());
  EXPECT(second.layers().empty());
}

int main() {
  test_recovered_empty_save_round_trips();
  return 0;
}
