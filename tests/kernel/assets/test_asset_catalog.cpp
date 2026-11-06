#include "pixcake/assets/asset_catalog.hpp"

#include <cstdio>
#include <string>
#include <vector>

namespace {

int g_failures = 0;

void expect(bool condition, const char* expression, int line) {
  if (!condition) {
    std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, line, expression);
    ++g_failures;
  }
}

#define EXPECT(expression) expect(static_cast<bool>(expression), #expression, __LINE__)

pixcake::assets::AssetRecord sample(const char* id) {
  pixcake::assets::AssetRecord value;
  value.asset_id = id;
  value.kind = pixcake::assets::AssetKind::Sample;
  value.category = "默认";
  value.source_path = "/tmp/sample.jpg";
  return value;
}

void test_types_and_missing_file_semantics() {
  using pixcake::ResultCode;
  pixcake::assets::AssetCatalog catalog;
  auto cube = sample("cube-1");
  cube.kind = pixcake::assets::AssetKind::Cube;
  cube.category = "滤镜";
  EXPECT(catalog.add(cube) == ResultCode::Ok);
  EXPECT(catalog.set_available("cube-1", false) == ResultCode::Ok);
  pixcake::assets::AssetRecord retained;
  EXPECT(catalog.find("cube-1", retained));
  EXPECT(!retained.available);
  EXPECT(retained.asset_id == "cube-1");
  EXPECT(catalog.set_available("missing", false) == ResultCode::StateConflict);
}

void test_sample_batch_limits_are_atomic() {
  using pixcake::ResultCode;
  pixcake::assets::AssetCatalog catalog;
  std::vector<pixcake::assets::AssetRecord> batch;
  for (int index = 0; index < 1000; ++index) {
    batch.push_back(sample(("sample-" + std::to_string(index)).c_str()));
  }
  const auto first = catalog.import_samples(batch);
  EXPECT(first.code == ResultCode::Ok);
  EXPECT(first.imported == 1000);
  EXPECT(catalog.count(pixcake::assets::AssetKind::Sample) == 1000);

  auto too_large = batch;
  too_large.push_back(sample("sample-too-large"));
  const auto rejected_batch = catalog.import_samples(too_large);
  EXPECT(rejected_batch.code == ResultCode::StateConflict);
  EXPECT(catalog.count(pixcake::assets::AssetKind::Sample) == 1000);

  std::vector<pixcake::assets::AssetRecord> second;
  for (int index = 1000; index < 2000; ++index) {
    second.push_back(sample(("sample-" + std::to_string(index)).c_str()));
  }
  EXPECT(catalog.import_samples(second).code == ResultCode::Ok);
  EXPECT(catalog.count(pixcake::assets::AssetKind::Sample) == 2000);
  auto over_limit = std::vector<pixcake::assets::AssetRecord>{sample("sample-over")};
  const auto rejected_limit = catalog.import_samples(over_limit);
  EXPECT(rejected_limit.code == ResultCode::StateConflict);
  EXPECT(catalog.count(pixcake::assets::AssetKind::Sample) == 2000);
}

void test_invalid_batch_and_duplicate_ids_do_not_partially_write() {
  using pixcake::ResultCode;
  pixcake::assets::AssetCatalog catalog;
  auto invalid = std::vector<pixcake::assets::AssetRecord>{sample("sample-1"), sample("sample-1")};
  EXPECT(catalog.import_samples(invalid).code == ResultCode::StateConflict);
  EXPECT(catalog.count(pixcake::assets::AssetKind::Sample) == 0);
  EXPECT(catalog.add(sample("sample-1")) == ResultCode::Ok);
  EXPECT(catalog.add(sample("sample-1")) == ResultCode::StateConflict);
}

}  // namespace

int main() {
  test_types_and_missing_file_semantics();
  test_sample_batch_limits_are_atomic();
  test_invalid_batch_and_duplicate_ids_do_not_partially_write();
  return g_failures == 0 ? 0 : 1;
}
