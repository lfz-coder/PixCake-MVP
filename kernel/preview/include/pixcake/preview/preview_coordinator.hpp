#pragma once

#include "pixcake/capability/capability_registry.hpp"
#include "pixcake/editdoc/edit_document.hpp"
#include "pixcake/render/render_description.hpp"
#include "pixcake/support/fast_export_latch.hpp"
#include "pixcake/support/result_code.hpp"

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>

namespace pixcake::preview {

enum class PreviewMode {
  FullEffect,
  ColorOnly,
};

struct PreviewRequest {
  std::string image_id;
  std::string editing_instance_id;
  std::uint64_t base_revision = 0;
  std::string capability_id;
  std::string parameter_id;
  std::string display_value;
  PreviewMode mode = PreviewMode::FullEffect;
  std::uint32_t max_edge = 4000;
};

struct PreviewSnapshot {
  std::uint64_t sequence = 0;
  PreviewRequest request;
  render::RenderDescription description;
};

struct PreviewTakeResult {
  ResultCode code = ResultCode::Ok;
  std::optional<PreviewSnapshot> snapshot;
};

// Owns only the latest in-memory preview request. It never writes a document
// snapshot or a file and does not create a render/GPU object.
class PreviewCoordinator {
 public:
  ResultCode submit(EditDocument& document, const PreviewRequest& request,
                    const FastExportLatch* latch = nullptr);
  PreviewTakeResult take_latest(const EditDocument& document,
                                const CapabilityRegistry& registry = CapabilityRegistry::phase_one());
  void clear();
  std::uint64_t latest_sequence() const;

 private:
  mutable std::mutex mutex_;
  std::optional<PreviewSnapshot> pending_;
  std::uint64_t next_sequence_ = 1;
};

}  // namespace pixcake::preview
