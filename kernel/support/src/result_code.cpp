#include "pixcake/support/result_code.hpp"

namespace pixcake {

const char* result_code_label(ResultCode code) {
  switch (code) {
    case ResultCode::Ok:
      return "成功";
    case ResultCode::UnsupportedFormat:
      return "格式不支持";
    case ResultCode::ImageCorrupt:
      return "图片损坏";
    case ResultCode::PermissionDenied:
      return "权限不足";
    case ResultCode::DiskFull:
      return "磁盘已满";
    case ResultCode::SourceMissing:
      return "原文件缺失";
    case ResultCode::DocumentCorrupt:
      return "文档损坏";
    case ResultCode::CapabilityTooNew:
      return "能力版本过新";
    case ResultCode::Cancelled:
      return "已取消";
    case ResultCode::NetworkFailed:
      return "网络失败";
    case ResultCode::InsufficientBalance:
      return "点数或张数不足";
    case ResultCode::FastExportBusy:
      return "极速导出占用";
    case ResultCode::StateConflict:
      return "状态冲突";
    case ResultCode::IoFailed:
      return "输入输出失败";
  }
  return "";
}

}  // namespace pixcake
