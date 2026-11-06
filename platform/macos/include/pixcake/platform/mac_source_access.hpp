#pragma once

#include "pixcake/project/source_access.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace pixcake::platform {

// macOS 的原图访问凭据（安全清单 S-10，需求第 118 行）。导入时为用户选中的文件生成书签：沙盒构建里
// 带安全范围，非沙盒构建是普通书签；第一个字节标明是哪一种。读取原图前用 SourceScope 打开安全范围。
class MacSourceAccess final : public SourceAccess {
 public:
  // 路径必须是绝对路径；文件不在返回原文件缺失；系统拒绝生成返回权限不足。
  static ResultCode create_bookmark(const std::string& absolute_path, std::vector<std::uint8_t>& out);
  ResolvedSource resolve(const std::vector<std::uint8_t>& bookmark) override;
};

// 读取原图期间持有系统授予的安全范围；离开作用域时归还。普通书签不需要打开范围。
class SourceScope {
 public:
  explicit SourceScope(const std::vector<std::uint8_t>& bookmark);
  ~SourceScope();
  SourceScope(const SourceScope&) = delete;
  SourceScope& operator=(const SourceScope&) = delete;

  ResultCode code() const { return code_; }
  const std::string& path() const { return path_; }

 private:
  const void* url_ = nullptr;
  bool started_ = false;
  ResultCode code_ = ResultCode::PermissionDenied;
  std::string path_;
};

}  // namespace pixcake::platform
