#pragma once

#include "pixcake/support/fast_export_latch.hpp"
#include "pixcake/support/result_code.hpp"

#include <memory>
#include <mutex>
#include <string>

struct sqlite3;

namespace pixcake {

// 跨进程的极速导出开关：每次判断都从导出事件表重读最后一条 fast-on / fast-off。
// 任何进程（主进程、导出进程）写入的开关，下一次判断就能看到（边界清单 B-62）。
// 开关只由用户关闭；队列变空不影响这里的结果（契约 C-03）。
class SqliteFastExportLatch : public FastExportLatch {
 public:
  // path 是已由 SqliteExportLog 建好的导出库，只读打开。文件不存在或打不开返回 IoFailed。
  static ResultCode open(const std::string& path, std::unique_ptr<SqliteFastExportLatch>& out);
  ~SqliteFastExportLatch() override;

  SqliteFastExportLatch(const SqliteFastExportLatch&) = delete;
  SqliteFastExportLatch& operator=(const SqliteFastExportLatch&) = delete;

  // 读不出开关状态时按“开着”处理：宁可拒绝编辑，也不在状态不明时放行。
  bool enabled() const override;
  // 读失败原样返回结果码；没有任何开关事件时 on 为假。
  ResultCode read(bool& on) const;

 private:
  explicit SqliteFastExportLatch(sqlite3* db);

  sqlite3* db_;
  mutable std::mutex mutex_;
};

}  // namespace pixcake
