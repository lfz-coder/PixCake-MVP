#pragma once

#include "pixcake/generate/generation_task.hpp"
#include "pixcake/persist/file_system.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace pixcake::generation_session {

inline constexpr char kGenerationHistoryFailedStatus[] = "失败";

// The history is deliberately provider-neutral.  It keeps enough of the task
// and verified result metadata to show a durable task list after a restart;
// provider JSON and credentials never enter this file.
struct GenerationHistoryResult {
  std::uint32_t ordinal = 0;
  std::string result_id;
  std::string pixel_file;
  std::string checksum;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::vector<std::uint8_t> continuation_parameters;
};

struct GenerationHistoryEntry {
  std::string task_id;
  std::string image_id;
  std::uint64_t source_revision = 0;
  std::string tool_id;
  std::string result_shape;
  std::string status;
  std::string idempotency_key;
  std::vector<std::uint8_t> parameter_payload;
  std::vector<std::string> mask_ids;
  std::vector<std::string> reference_image_ids;
  std::vector<GenerationHistoryResult> results;
};

class GenerationHistoryStore {
 public:
  static constexpr std::size_t kMaxEntries = 128;

  static ResultCode open(const std::string& path, std::unique_ptr<GenerationHistoryStore>& out,
                         std::string& diagnostic);

  const std::vector<GenerationHistoryEntry>& entries() const { return entries_; }
  const GenerationHistoryEntry* find(const std::string& task_id) const;

  ResultCode record_task(const generate::GenerationTask& task, std::string status);
  ResultCode update_status(const std::string& task_id, std::string status,
                           const std::vector<GenerationHistoryResult>& results = {});

 private:
  explicit GenerationHistoryStore(std::string path) : path_(std::move(path)) {}

  ResultCode load(std::string& diagnostic);
  ResultCode persist();
  static std::vector<std::uint8_t> encode(const std::vector<GenerationHistoryEntry>& entries);
  static bool decode(const std::vector<std::uint8_t>& bytes, std::vector<GenerationHistoryEntry>& out);

  std::string path_;
  PosixFileSystem files_;
  std::vector<GenerationHistoryEntry> entries_;
};

}  // namespace pixcake::generation_session
