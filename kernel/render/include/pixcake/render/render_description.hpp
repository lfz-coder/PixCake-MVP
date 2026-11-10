#pragma once

#include "pixcake/capability/capability_registry.hpp"
#include "pixcake/editdoc/edit_document.hpp"
#include "pixcake/support/result_code.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace pixcake {
class LayerDocument;
}

namespace pixcake::render {

inline constexpr std::uint32_t kRenderDescriptionVersion = 1;

// A render value is a validated display value plus the stable channel name
// from ParameterMode. It contains no backend object, texture, shader or Qt
// type, so the same description can be consumed by preview and export.
struct RenderValue {
  std::string parameter_id;
  std::string channel;
  std::string display_value;
  double numeric_value = 0.0;
};

struct RenderOperation {
  std::string capability_id;
  std::string stage;
  std::string program;
  std::uint32_t order = 0;
  std::vector<RenderValue> values;
  std::vector<std::string> mask_ids;
  std::string blend_mode = "normal";
  bool enabled = true;
  std::string disabled_reason;
};

struct RenderDiagnostic {
  std::string capability_id;
  std::string reason;
  bool blocking = false;
};

struct RenderLayer {
  std::string id;
  std::string parent_id;
  std::uint32_t order = 0;
  std::string blend_mode = "normal";
  std::uint32_t opacity = 100;
  bool hidden = false;
  bool group = false;
  bool mask_enabled = false;
  std::uint32_t mask_density = 100;
  std::uint32_t mask_feather = 0;
  bool enabled = true;
  std::string disabled_reason;
  // A generated layer keeps its verified RGBA result outside the edit
  // document.  The render process receives this metadata and loads the file
  // only after validating its dimensions and checksum.
  std::string pixel_file;
  std::string pixel_checksum;
  std::uint32_t pixel_width = 0;
  std::uint32_t pixel_height = 0;
};

struct RenderSelection {
  bool present = false;
  bool inverted = false;
  std::uint32_t feather = 0;
};

struct RenderDescription {
  std::uint32_t schema_version = kRenderDescriptionVersion;
  std::string image_id;
  std::string editing_instance_id;
  std::uint64_t document_revision = 0;
  std::vector<RenderOperation> operations;
  std::vector<RenderLayer> layers;
  RenderSelection selection;
  std::vector<RenderDiagnostic> diagnostics;
};

struct CompileResult {
  ResultCode code = ResultCode::Ok;
  RenderDescription description;
};

// Read-only compilation. The document is never changed, no file is opened,
// and no GPU or backend object is created. In-memory preview values override
// committed values; unknown and too-new fragments are reported and skipped
// while remaining in the source document.
CompileResult compile(const EditDocument& document);
CompileResult compile(const EditDocument& document, const CapabilityRegistry& registry);
CompileResult compile(const EditDocument& document, const LayerDocument& layers,
                      const CapabilityRegistry& registry = CapabilityRegistry::phase_one());

}  // namespace pixcake::render
