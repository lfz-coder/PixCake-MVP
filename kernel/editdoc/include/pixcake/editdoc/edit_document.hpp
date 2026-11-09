#pragma once

#include "pixcake/persist/file_system.hpp"
#include "pixcake/support/fast_export_latch.hpp"
#include "pixcake/support/log_sink.hpp"
#include "pixcake/support/result_code.hpp"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace pixcake {

struct VersionLimit {
  std::string capability_id;
  std::uint32_t max_readable_version = 1;
};

// 阶段一每个已注册能力可读的片段版本：都是 1（commit_preview 写入版本 1）。打开、加载文档的
// 进程都用这一张表，不各自拼。
std::vector<VersionLimit> phase_one_version_limits();

struct FragmentView {
  std::string capability_id;
  std::uint32_t version = 0;
  std::vector<std::uint8_t> inner;
};

struct LoadResult {
  // 没有这份文件时 code 为空，absent 为真。不要把它显示成成功或文档损坏。
  std::optional<ResultCode> code;
  bool absent = false;
  bool used_backup = false;
  bool recovered_empty = false;
};

class EditDocument;

// 打开一张图时的三种状态（运维清单 O-15）：原图缺失、文档恢复为空、能力需要更新。三者互相独立，
// 可以同时出现；恢复为空时提示画面是原图，不把空文档当成原来的精修效果。
struct OpenState {
  bool source_missing = false;
  bool recovered_empty = false;
  std::vector<std::string> needs_update;  // 片段版本比本程序新的能力
  std::vector<std::string> notices;
};
OpenState describe_open(const LoadResult& loaded, const EditDocument& document, ResultCode source_status);

// 修订号只增。预览副本不写盘，也不能被导出取走。
class EditDocument {
 public:
  static std::optional<EditDocument> create(std::string image_id);

  // 日志可以为空。不为空时，每次写盘（提交、片段替换、烘焙复位、快照）结束写一条 editdoc.commit，
  // 带文档路径、修订号、结果码和耗时；写盘和恢复的细节由 persist 记。load 和 copy_committed
  // 沿用目标对象上已经设置的日志。日志指针不写进文档，也不随 encode 复制。
  void set_log(LogSink* log) { log_ = log; }
  LogSink* log() const { return log_; }
  static LoadResult load(EditDocument& destination, FileSystem& files, const std::string& path,
                         const std::vector<VersionLimit>& limits);

  // 把源路径上已提交的文档写成目标路径上的另一份。内存里的预览不写入。
  // 路径为空或两条路径相同：返回状态冲突，不写文件。
  static LoadResult copy_committed(FileSystem& files, const std::string& source_path,
                                   const std::string& destination_path,
                                   const std::vector<VersionLimit>& limits, EditDocument& copy,
                                   const FastExportLatch* latch = nullptr);

  std::vector<std::uint8_t> encode() const;
  static std::optional<EditDocument> decode(const std::vector<std::uint8_t>& bytes);

  // 不增加修订号。预览副本不会被这次写入带走。
  ResultCode write_snapshot(FileSystem& files, const std::string& path,
                            const FastExportLatch* latch = nullptr) const;

  std::string image_id() const;
  // 计费原图编号与编辑实例编号分开。原图创建时两者相同；复制已提交文档时只生成新的编辑实例编号。
  std::string editing_instance_id() const;
  std::uint64_t revision() const;
  std::uint32_t document_version() const;
  bool recovered_empty() const;
  std::uint64_t content_checksum() const;
  bool fragment_too_new(const std::string& capability_id) const;

  void add_mask_id(std::string id);
  std::vector<std::string> mask_ids() const;
  void set_subject_override(std::vector<std::uint8_t> bytes);
  std::vector<std::uint8_t> subject_override() const;
  void add_manual_ref(std::string id);
  std::vector<std::string> manual_refs() const;
  void add_preset_ref(std::string id);
  std::vector<std::string> preset_refs() const;
  void append_raw_record(std::uint32_t type, std::vector<std::uint8_t> payload);
  std::vector<std::vector<std::uint8_t>> payloads_of_type(std::uint32_t type) const;

  void store_fragment(std::string capability_id, std::uint32_t version, std::vector<std::uint8_t> inner);

  // 删掉列出的能力片段，再写入新片段。修订号加一。写盘失败时调用方看到的文档仍是上一份。
  ResultCode commit_fragment_replacement(std::uint64_t expected_revision,
                                         const std::vector<std::string>& remove_capability_ids,
                                         const std::vector<FragmentView>& replacements, FileSystem& files,
                                         const std::string& path);
  std::optional<FragmentView> fragment(const std::string& capability_id) const;
  std::vector<FragmentView> fragments() const;
  // 追色写回记录（类型 2）里的值；没有时为空。
  std::optional<std::string> match_written_parameter(const std::string& capability_id,
                                                     const std::string& parameter_id) const;
  // 参与编译与显示的值：预览值；精准控制打开且有追色写回时取写回值；否则取用户参数记录（契约 C-33）。
  std::optional<std::string> effective_parameter(const std::string& capability_id,
                                                 const std::string& parameter_id) const;
  std::optional<std::string> committed_parameter(const std::string& capability_id,
                                                 const std::string& parameter_id) const;

  ResultCode set_preview(const std::string& capability_id, const std::string& parameter_id,
                         const std::string& display_value);
  std::optional<std::string> preview(const std::string& capability_id,
                                     const std::string& parameter_id) const;
  // 这个能力当前有预览值的参数编号（含局部调色带 m<N>: 前缀的）。
  std::vector<std::string> preview_parameter_ids(const std::string& capability_id) const;
  ResultCode commit_preview(std::uint64_t expected_revision, const std::string& capability_id,
                            const std::string& parameter_id, const std::vector<VersionLimit>& limits,
                            FileSystem& files, const std::string& path);

  // 删掉能力片段里属于 prefix 的全部参数（局部调色删除一个蒙版）：prefix 是 "m<N>:"，空串是没有前缀的那一组；
  // 没有这样的参数返回状态冲突。修订号加一。
  ResultCode commit_parameter_prefix_removal(std::uint64_t expected_revision, const std::string& capability_id,
                                             const std::string& prefix, FileSystem& files, const std::string& path);
  // 删掉能力的预览值里编号以 prefix 开头的（不写盘）。
  void clear_preview_prefix(const std::string& capability_id, const std::string& prefix);

  bool effects_baked() const;
  // 清掉能力片段和预览，留下烘焙标记，修订号加一。已经烘焙且没有片段时不写第二次。
  ResultCode commit_baked_reset(std::uint64_t expected_revision, FileSystem& files, const std::string& path);

 private:
  friend class HistoryLog;
  ResultCode write_snapshot_steps(FileSystem& files, const std::string& path, const FastExportLatch* latch) const;
  ResultCode commit_preview_steps(std::uint64_t expected_revision, const std::string& capability_id,
                                  const std::string& parameter_id, const std::vector<VersionLimit>& limits,
                                  FileSystem& files, const std::string& path);
  ResultCode commit_fragment_replacement_steps(std::uint64_t expected_revision,
                                               const std::vector<std::string>& remove_capability_ids,
                                               const std::vector<FragmentView>& replacements, FileSystem& files,
                                               const std::string& path);
  ResultCode commit_baked_reset_steps(std::uint64_t expected_revision, FileSystem& files, const std::string& path);
  void note_commit(const std::string& path, ResultCode code, std::uint64_t duration_ms) const;
  LogSink* log_ = nullptr;
  struct Record {
    std::uint32_t type = 0;
    std::vector<std::uint8_t> payload;
  };

  static EditDocument make_recovered_empty();
  std::vector<std::uint8_t> encode_body() const;
  void set_revision(std::uint64_t value);
  void clear_recovered_flag();
  void set_editing_instance_id(std::string value);

  std::vector<Record> records_;
  std::string editing_instance_id_;
  std::map<std::pair<std::string, std::string>, std::string> preview_;
  std::vector<VersionLimit> limits_;
};

}  // namespace pixcake
