#pragma once

#include "pixcake/editdoc/edit_document.hpp"
#include "pixcake/support/result_code.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace pixcake {

inline constexpr char kHistoryFoldedLabel[] = "上一次保存";
inline constexpr char kHistoryOriginalLabel[] = "原图";

// 进程内的单图步骤。不写盘，撤销链不放进参数文档。
struct HistoryStep {
  std::uint64_t revision = 0;
  std::string capability_id;
  std::string parameter_id;
  std::string display_value;
  std::string label;
  std::vector<std::uint8_t> snapshot;
};

class HistoryLog {
 public:
  ResultCode record_committed(const std::string& image_id, const EditDocument& document,
                              std::string capability_id, std::string parameter_id,
                              std::string display_value, bool batch_design_busy);

  ResultCode select(const std::string& image_id, EditDocument& document, std::size_t index,
                    bool batch_design_busy);

  // 打开一张没有提交过的图：第一步是“原图”，之后的提交接在它后面。不写文件。
  ResultCode begin_original(const std::string& image_id, const EditDocument& loaded);

  // 用已经从磁盘打开的文档替换这张图的步骤。不写文件。
  ResultCode reopen_folded(const std::string& image_id, const EditDocument& loaded);

  const std::vector<HistoryStep>* steps(const std::string& image_id) const;
  std::optional<std::size_t> current_index(const std::string& image_id) const;

 private:
  struct ImageSteps {
    std::vector<HistoryStep> steps;
    std::optional<std::size_t> current;
  };

  std::map<std::string, ImageSteps> images_;
};

}  // namespace pixcake
