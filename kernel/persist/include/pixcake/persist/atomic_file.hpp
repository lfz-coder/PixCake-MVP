#pragma once

#include "pixcake/persist/file_system.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace pixcake {

// 正式文件不存在时 absent 为真。这时 code 不是成功，也不是文档损坏或原文件缺失；
// 调用方先看 absent，不要把 code 直接显示给用户。
struct DurableLoad {
  ResultCode code = ResultCode::Ok;
  bool absent = false;
  bool used_backup = false;
  bool recovered_empty = false;
  bool recovery_marker = false;
  std::vector<std::uint8_t> bytes;
};

// 正式文件五步替换：写临时文件、刷文件、正式文件换成备份、临时文件换成正式文件、刷目录。
// 任何一步失败都留下上一份完整正式文件，禁止先截断正式文件再写入。
ResultCode atomic_replace(FileSystem& files, const std::string& formal_path,
                          const std::vector<std::uint8_t>& bytes);

DurableLoad open_durable_file(FileSystem& files, const std::string& formal_path,
                              const std::function<bool(const std::vector<std::uint8_t>&)>& valid);


// 提交前在同一路径的 WriteScope 内调用：重读正式文件的持久修订号，与调用方期望值比较。
// 正式文件不存在或已无可恢复内容时返回成功（没有可比较的持久版本）；读失败原样返回；
// 修订号不一致返回状态冲突，调用方不得写盘。
ResultCode check_durable_revision(
    FileSystem& files, const std::string& formal_path, std::uint64_t expected_revision,
    const std::function<std::optional<std::uint64_t>(const std::vector<std::uint8_t>&)>& revision_of);

}  // namespace pixcake
