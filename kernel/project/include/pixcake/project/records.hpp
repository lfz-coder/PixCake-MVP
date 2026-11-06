#pragma once

#include "pixcake/persist/file_system.hpp"
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
inline constexpr char kPresenceDeletePending[] = "删除待完成";
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
// 修图状态与导出状态（需求 §6.2 筛选、§6.4 标识）。空串表示未编辑、未导出。
inline constexpr char kEditStatusEdited[] = "已编辑";
inline constexpr char kExportStatusExported[] = "已导出";

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
  FileIdentity source_identity;
  // 准入时按内容识别的格式（JPEG、PNG、TIFF、BMP）；旧库按扩展名回填，认不出为空。
  std::string format;
  // 拍摄时间（Unix 秒）。内核不读 EXIF，0 表示未知，等解码适配器回填。
  std::int64_t capture_time = 0;
  std::string edit_status;
  std::int64_t edited_at = 0;
  std::string export_status;
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
  FileIdentity source_identity;
  std::string format;
  std::int64_t capture_time = 0;
};

// 需求 §6.3 的排序键。ImportOrder 是导入顺序（照片编号）。同值按编号，与排序方向一致。
enum class PhotoSortKey { ImportOrder, CaptureTime, EditTime, Rating, FileType, FileSize, FileName };

// 需求 §6.2 的筛选条件，多个条件取交集；列表为空表示不限，非空时取其中任一值。
// 只返回图库元数据行，不读精修参数正文（性能清单 P-12）。
struct PhotoQuery {
  std::optional<std::int64_t> project_id;
  int rating_min = kRatingMin;
  int rating_max = kRatingMax;
  std::vector<std::string> flags;
  std::vector<std::string> colors;
  std::vector<std::string> formats;
  std::vector<std::string> edit_statuses;
  std::vector<std::string> export_statuses;
  PhotoSortKey sort = PhotoSortKey::ImportOrder;
  bool descending = false;
};

struct ProjectResult {
  ResultCode code = ResultCode::Ok;
  std::string notice;
  std::int64_t id = 0;
};

}  // namespace pixcake
