#pragma once

#include "pixcake/support/result_code.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace pixcake {

inline constexpr char kOriginLocal[] = "本地";
inline constexpr char kOriginCloudUploaded[] = "云上传";
inline constexpr char kOriginCloudDownloaded[] = "云下载";
inline constexpr char kPresenceEditable[] = "可编辑";
inline constexpr char kPresenceMissing[] = "原文件缺失";
inline constexpr char kTrashProject[] = "项目";
inline constexpr char kTrashPhoto[] = "图片";
inline constexpr char kTrashVirtualCopy[] = "虚拟副本";
inline constexpr char kDiskFileRemains[] = "磁盘文件还在";
inline constexpr int kRatingMin = 0;
inline constexpr int kRatingMax = 5;
inline constexpr char kFlagPick[] = "选取";
inline constexpr char kFlagExclude[] = "排除";
inline constexpr char kColorRed[] = "红";
inline constexpr char kColorYellow[] = "黄";
inline constexpr char kColorGreen[] = "绿";
inline constexpr char kColorBlue[] = "蓝";
inline constexpr char kColorPurple[] = "紫";
inline constexpr std::int64_t kTrashRetentionSeconds = 2592000;

struct ProjectRow {
  std::int64_t id = 0;
  std::string name;
  std::string origin;
  bool cloud_marked = false;
  std::optional<std::int64_t> deleted_at;
};

struct PhotoRow {
  std::int64_t id = 0;
  std::int64_t project_id = 0;
  std::string absolute_path;
  std::string file_name;
  std::vector<std::uint8_t> access_credential;
  std::string presence;
  int rating = 0;
  std::string flag;
  std::string color_label;
};

struct TrashRow {
  std::int64_t id = 0;
  std::string subject;
  std::int64_t subject_id = 0;
  std::int64_t deleted_at = 0;
};

struct VirtualCopyRow {
  std::int64_t id = 0;
  std::int64_t project_id = 0;
  std::int64_t source_photo_id = 0;
  std::int64_t sequence = 0;
  std::string document_id;
};

struct VirtualCopyDraft {
  std::int64_t project_id = 0;
  std::int64_t source_photo_id = 0;
  std::int64_t sequence = 0;
  std::string document_id;
};

struct ProjectDraft {
  std::string name;
  std::string origin;
  bool cloud_marked = false;
};

struct PhotoDraft {
  std::int64_t project_id = 0;
  std::string absolute_path;
  std::string file_name;
  std::vector<std::uint8_t> access_credential;
  std::string presence;
};

struct ProjectResult {
  ResultCode code = ResultCode::Ok;
  std::string notice;
  std::int64_t id = 0;
};

}  // namespace pixcake
