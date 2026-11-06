#pragma once

#include "pixcake/support/result_code.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace pixcake {

// 原图访问凭据（安全清单 S-10，需求第 118 行）：项目库只保存绝对路径和系统授予的书签或安全范围，
// 不把任意路径的读取权扩成整个磁盘。书签的创建与解析由平台实现（platform/macos），内核只看结果。
struct ResolvedSource {
  // Ok：书签可用；SourceMissing：书签指向的文件不在了；PermissionDenied：系统不再授予读取。
  ResultCode code = ResultCode::Ok;
  std::string absolute_path;              // 书签现在指向的路径
  bool stale = false;                     // 书签需要换新（例如系统更新后）
  std::vector<std::uint8_t> refreshed;    // stale 时平台给出的新书签
};

class SourceAccess {
 public:
  virtual ~SourceAccess() = default;
  virtual ResolvedSource resolve(const std::vector<std::uint8_t>& bookmark) = 0;
};

}  // namespace pixcake
