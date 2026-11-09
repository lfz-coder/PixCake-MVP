#include "pixcake/renderd/semantic_mask_provider.hpp"

#include "pixcake/mask/combined_mask.hpp"
#include "pixcake/mask/semantic_parts.hpp"

#include <algorithm>
#include <cstring>

namespace pixcake::renderd {
namespace {

// FNV-1a over the visible bytes of every row (row padding is ignored).
std::uint64_t content_hash(const RgbaInput& input) {
  std::uint64_t hash = 1469598103934665603ull;
  const std::size_t row = static_cast<std::size_t>(input.width) * 4;
  for (std::uint32_t y = 0; y < input.height; ++y) {
    const std::uint8_t* p = input.bytes.data() + static_cast<std::size_t>(y) * input.stride;
    for (std::size_t i = 0; i < row; ++i) {
      hash ^= p[i];
      hash *= 1099511628211ull;
    }
  }
  return hash;
}

const char* label_of(const std::string& part_id) {
  for (const auto& part : mask::semantic_parts()) {
    if (part_id == part.id) return part.label;
  }
  return "";
}

// Operations (by capability id) that reference a given mask id.
std::vector<std::string> users_of(const render::RenderDescription& description, const std::string& mask_id) {
  std::vector<std::string> users;
  for (const auto& operation : description.operations) {
    if (!operation.enabled) continue;
    bool uses = false;
    for (const auto& whole : operation.mask_ids) {
      for (const auto& leaf : mask::leaf_mask_ids(whole)) uses = uses || leaf == mask_id;
    }
    if (uses && std::find(users.begin(), users.end(), operation.capability_id) == users.end()) {
      users.push_back(operation.capability_id);
    }
  }
  return users;
}

void report(SemanticMaskBatch& out, const render::RenderDescription& description, const std::string& mask_id,
            const std::string& reason) {
  for (const auto& user : users_of(description, mask_id)) out.diagnostics.push_back({user, reason, false});
}

}  // namespace

SemanticMaskProvider::SemanticMaskProvider(std::shared_ptr<SemanticSegmenter> segmenter)
    : segmenter_(std::move(segmenter)) {}

std::vector<std::string> SemanticMaskProvider::referenced_semantic_ids(const render::RenderDescription& description) {
  std::vector<std::string> ids;
  for (const auto& operation : description.operations) {
    if (!operation.enabled) continue;
    for (const auto& whole : operation.mask_ids) {
      for (const auto& id : mask::leaf_mask_ids(whole)) {  // 组合编号里的语义子项也要提供
        if (mask::is_semantic_mask_id(id) && std::find(ids.begin(), ids.end(), id) == ids.end()) ids.push_back(id);
      }
    }
  }
  return ids;
}

void SemanticMaskProvider::provide(const std::string& image_id, const RgbaInput& input,
                                   const render::RenderDescription& description, SemanticMaskBatch& out) {
  out = {};
  const auto ids = referenced_semantic_ids(description);
  if (ids.empty()) return;

  if (!segmenter_) {
    out.segmentation_failed = true;
    for (const auto& id : ids) report(out, description, id, "语义分割未构建，语义蒙版不可用：" + id);
    return;
  }

  const std::uint64_t hash = content_hash(input);
  if (!cached_ || cached_->image_id != image_id || cached_->width != input.width || cached_->height != input.height ||
      cached_->content_hash != hash) {
    cached_.reset();
    SegmentationResult result;
    std::string diagnostic;
    ++runs_;
    const ResultCode code = segmenter_->segment(input, result, diagnostic);
    if (code != ResultCode::Ok) {
      out.segmentation_failed = true;
      for (const auto& id : ids) {
        report(out, description, id,
               std::string("语义分割失败（") + result_code_label(code) + "）：" + diagnostic + "；跳过 " + id);
      }
      return;
    }
    cached_ = Cached{image_id, input.width, input.height, hash, std::move(result)};
  }

  const SegmentationResult& result = cached_->result;
  for (const auto& id : ids) {
    std::string part_id;
    if (!mask::semantic_part_of_mask_id(id, part_id)) {
      report(out, description, id, "未知的语义部位：" + id);
      continue;
    }
    const auto capability = std::find_if(result.capabilities.begin(), result.capabilities.end(),
                                         [&](const auto& c) { return part_id == semantic_part_id(c.part); });
    if (capability == result.capabilities.end() || capability->status == SupportStatus::Unsupported) {
      const std::string why = capability == result.capabilities.end() ? "模型没有声明该部位" : capability->diagnostic;
      report(out, description, id, std::string(label_of(part_id)) + "不可用（" + part_id + "）：" + why);
      continue;
    }
    const auto mask = std::find_if(result.masks.begin(), result.masks.end(),
                                   [&](const auto& m) { return m.part == capability->part; });
    if (mask == result.masks.end()) {
      report(out, description, id, std::string(label_of(part_id)) + "的蒙版缺失（" + part_id + "）");
      continue;
    }
    gpu::RenderMask render_mask;
    render_mask.id = id;
    render_mask.width = mask->width;
    render_mask.height = mask->height;
    render_mask.alpha.resize(static_cast<std::size_t>(mask->width) * mask->height);
    for (std::uint32_t y = 0; y < mask->height; ++y) {
      std::memcpy(render_mask.alpha.data() + static_cast<std::size_t>(y) * mask->width,
                  mask->alpha.data() + static_cast<std::size_t>(y) * mask->stride, mask->width);
    }
    out.masks.push_back(std::move(render_mask));
  }
}

}  // namespace pixcake::renderd
