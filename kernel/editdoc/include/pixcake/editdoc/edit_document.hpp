#pragma once

#include "pixcake/persist/file_system.hpp"
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

// 修订号只增。预览副本不写盘，也不能被导出取走。
class EditDocument {
 public:
  static std::optional<EditDocument> create(std::string image_id);
  static LoadResult load(EditDocument& destination, FileSystem& files, const std::string& path,
                         const std::vector<VersionLimit>& limits);

  // 把源路径上已提交的文档写成目标路径上的另一份。内存里的预览不写入。
  // 路径为空或两条路径相同：返回状态冲突，不写文件。
  static LoadResult copy_committed(FileSystem& files, const std::string& source_path,
                                   const std::string& destination_path,
                                   const std::vector<VersionLimit>& limits, EditDocument& copy);

  std::vector<std::uint8_t> encode() const;
  static std::optional<EditDocument> decode(const std::vector<std::uint8_t>& bytes);

  // 不增加修订号。预览副本不会被这次写入带走。
  ResultCode write_snapshot(FileSystem& files, const std::string& path) const;

  std::string image_id() const;
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
  std::optional<std::string> committed_parameter(const std::string& capability_id,
                                                 const std::string& parameter_id) const;

  ResultCode set_preview(const std::string& capability_id, const std::string& parameter_id,
                         const std::string& display_value);
  std::optional<std::string> preview(const std::string& capability_id,
                                     const std::string& parameter_id) const;
  ResultCode commit_preview(std::uint64_t expected_revision, const std::string& capability_id,
                            const std::string& parameter_id, const std::vector<VersionLimit>& limits,
                            FileSystem& files, const std::string& path);

 private:
  struct Record {
    std::uint32_t type = 0;
    std::vector<std::uint8_t> payload;
  };

  static EditDocument make_recovered_empty();
  std::vector<std::uint8_t> encode_body() const;
  void set_revision(std::uint64_t value);
  void clear_recovered_flag();

  std::vector<Record> records_;
  std::map<std::pair<std::string, std::string>, std::string> preview_;
  std::vector<VersionLimit> limits_;
};

}  // namespace pixcake
