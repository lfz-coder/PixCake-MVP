#pragma once

#include "pixcake/persist/file_system.hpp"
#include "pixcake/support/log_sink.hpp"

#include <chrono>
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

// 已提交命令的写盘截止时间（需求第 135 行、架构第 245 行：提交后 2 秒内写盘）。
inline constexpr std::chrono::milliseconds kCommitWriteDeadline{2000};

// 正式文件五步替换：写临时文件、刷文件、正式文件换成备份、临时文件换成正式文件、刷目录。
// 任何一步失败都留下上一份完整正式文件，禁止先截断正式文件再写入。
// log 不为空时：碰文件之前写 persist.write.start，五步全部成功后写 persist.write.finish（带耗时），
// 任一步失败写 persist.write.failed（带结果码，磁盘满为 DiskFull）。
// deadline 从进入本函数（含等写锁）开始计。临时文件刷盘后、动正式文件之前已经超时：删掉临时文件，
// 正式文件不动，返回 IoFailed，并写 persist.write.deadline（边界 B-52：超过 2 秒不得返回成功）。
// 换名之后不再判超时：新内容已经落盘，再报失败会和磁盘对不上。
ResultCode atomic_replace(FileSystem& files, const std::string& formal_path,
                          const std::vector<std::uint8_t>& bytes, LogSink* log = nullptr,
                          std::chrono::milliseconds deadline = kCommitWriteDeadline);

// log 不为空时：坏文件进隔离区写 persist.quarantine，改用备份写 persist.recover.backup，
// 恢复为空写 persist.recover.empty，读失败写 persist.open.failed。
DurableLoad open_durable_file(FileSystem& files, const std::string& formal_path,
                              const std::function<bool(const std::vector<std::uint8_t>&)>& valid,
                              LogSink* log = nullptr);


// 提交前在同一路径的 WriteScope 内调用：重读正式文件的持久修订号，与调用方期望值比较。
// 正式文件不存在或已无可恢复内容时返回成功（没有可比较的持久版本）；读失败原样返回；
// 修订号不一致返回状态冲突，调用方不得写盘。
ResultCode check_durable_revision(
    FileSystem& files, const std::string& formal_path, std::uint64_t expected_revision,
    const std::function<std::optional<std::uint64_t>(const std::vector<std::uint8_t>&)>& revision_of);

}  // namespace pixcake
