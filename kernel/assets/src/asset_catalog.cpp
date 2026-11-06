#include "pixcake/assets/asset_catalog.hpp"

#include <algorithm>

namespace pixcake::assets {
namespace {

bool valid_identifier(const std::string& value) {
  return !value.empty() && value.find('\0') == std::string::npos;
}

bool valid_kind(AssetKind kind) {
  switch (kind) {
    case AssetKind::Sample:
    case AssetKind::Cube:
    case AssetKind::Sky:
    case AssetKind::IdPhotoBackground:
    case AssetKind::Scene:
    case AssetKind::Watermark:
      return true;
  }
  return false;
}

}  // namespace

const char* asset_kind_name(AssetKind kind) {
  switch (kind) {
    case AssetKind::Sample:
      return "样片";
    case AssetKind::Cube:
      return "CUBE";
    case AssetKind::Sky:
      return "天空";
    case AssetKind::IdPhotoBackground:
      return "证件照背景";
    case AssetKind::Scene:
      return "布景";
    case AssetKind::Watermark:
      return "水印";
  }
  return "";
}

ResultCode AssetCatalog::add(AssetRecord record) {
  if (!valid_identifier(record.asset_id) || !valid_identifier(record.category) ||
      !valid_kind(record.kind)) {
    return ResultCode::StateConflict;
  }
  AssetRecord existing;
  if (find(record.asset_id, existing)) {
    return ResultCode::StateConflict;
  }
  if (record.kind == AssetKind::Sample && count(AssetKind::Sample) >= kSampleLimit) {
    return ResultCode::StateConflict;
  }
  records_.push_back(std::move(record));
  return ResultCode::Ok;
}

ImportResult AssetCatalog::import_samples(const std::vector<AssetRecord>& records) {
  ImportResult result;
  result.rejected = records.size();
  if (records.empty() || records.size() > kSampleBatchLimit) {
    result.code = ResultCode::StateConflict;
    result.notice = "样片单次导入上限为 1000 张";
    return result;
  }
  if (count(AssetKind::Sample) > kSampleLimit - records.size()) {
    result.code = ResultCode::StateConflict;
    result.notice = "样片库上限为 2000 张，请先删除样片";
    return result;
  }
  std::vector<std::string> ids;
  for (const auto& record : records) {
    if (record.kind != AssetKind::Sample || !valid_identifier(record.asset_id) ||
        !valid_identifier(record.category) || !valid_kind(record.kind) ||
        std::find(ids.begin(), ids.end(), record.asset_id) != ids.end()) {
      result.code = ResultCode::StateConflict;
      result.notice = "样片编号或类型无效";
      return result;
    }
    AssetRecord existing;
    if (find(record.asset_id, existing)) {
      result.code = ResultCode::StateConflict;
      result.notice = "素材编号重复";
      return result;
    }
    ids.push_back(record.asset_id);
  }
  records_.insert(records_.end(), records.begin(), records.end());
  result.code = ResultCode::Ok;
  result.imported = records.size();
  result.rejected = 0;
  result.notice.clear();
  return result;
}

ResultCode AssetCatalog::set_available(const std::string& asset_id, bool available) {
  for (auto& record : records_) {
    if (record.asset_id == asset_id) {
      record.available = available;
      return ResultCode::Ok;
    }
  }
  return ResultCode::StateConflict;
}

std::size_t AssetCatalog::count(AssetKind kind) const {
  return static_cast<std::size_t>(std::count_if(records_.begin(), records_.end(), [kind](const AssetRecord& record) {
    return record.kind == kind;
  }));
}

std::vector<AssetRecord> AssetCatalog::records() const { return records_; }

std::vector<AssetRecord> AssetCatalog::records(AssetKind kind) const {
  std::vector<AssetRecord> result;
  for (const auto& record : records_) {
    if (record.kind == kind) {
      result.push_back(record);
    }
  }
  return result;
}

bool AssetCatalog::find(const std::string& asset_id, AssetRecord& out) const {
  const auto found = std::find_if(records_.begin(), records_.end(), [&asset_id](const AssetRecord& record) {
    return record.asset_id == asset_id;
  });
  if (found == records_.end()) {
    return false;
  }
  out = *found;
  return true;
}

}  // namespace pixcake::assets
