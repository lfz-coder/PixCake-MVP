#pragma once

#include "pixcake/persist/file_system.hpp"

#include <cstdint>
#include <functional>
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

}  // namespace pixcake
