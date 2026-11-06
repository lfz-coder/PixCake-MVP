#pragma once

#include "pixcake/support/result_code.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace pixcake::assets {

enum class AssetKind {
  Sample,
  Cube,
  Sky,
  IdPhotoBackground,
  Scene,
  Watermark,
};

struct AssetRecord {
  std::string asset_id;
  AssetKind kind = AssetKind::Sample;
  std::string category;
  std::string source_path;
  bool available = true;
};

struct ImportResult {
  ResultCode code = ResultCode::Ok;
  std::size_t imported = 0;
  std::size_t rejected = 0;
  std::string notice;
};

const char* asset_kind_name(AssetKind kind);

class AssetCatalog {
 public:
  static constexpr std::size_t kSampleLimit = 2000;
  static constexpr std::size_t kSampleBatchLimit = 1000;

  ResultCode add(AssetRecord record);
  ImportResult import_samples(const std::vector<AssetRecord>& records);
  ResultCode set_available(const std::string& asset_id, bool available);

  std::size_t count(AssetKind kind) const;
  std::vector<AssetRecord> records() const;
  std::vector<AssetRecord> records(AssetKind kind) const;
  bool find(const std::string& asset_id, AssetRecord& out) const;

 private:
  std::vector<AssetRecord> records_;
};

}  // namespace pixcake::assets
