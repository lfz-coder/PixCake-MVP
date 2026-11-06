#pragma once

#include "pixcake/support/result_code.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace pixcake::cull {

enum class Scene {
  General,
  Portrait,
  IdPhoto,
  Event,
  Wedding,
  Family,
  Child,
  School,
  Pregnancy,
  Boudoir,
};

enum class Sensitivity {
  Weak,
  Standard,
  Strong,
};

enum class MarkTarget {
  Star,
  Color,
  Flag,
};

struct CullOptions {
  Scene scene = Scene::General;
  Sensitivity sensitivity = Sensitivity::Standard;
  MarkTarget mark_target = MarkTarget::Star;
  std::optional<std::size_t> selection_limit;
  bool preserve_existing_labels = false;
};

struct ExistingLabels {
  bool has_star = false;
  bool has_color = false;
  bool has_flag = false;
};

struct CullDecision {
  std::string image_id;
  bool selected = false;
  std::string duplicate_group;
  ExistingLabels existing;
};

struct MarkAction {
  std::string image_id;
  MarkTarget target = MarkTarget::Star;
  bool applied = false;
  bool skipped_existing = false;
};

struct CullResult {
  CullOptions options;
  std::vector<CullDecision> decisions;
  std::vector<MarkAction> actions;
};

const char* scene_name(Scene scene);
const char* sensitivity_name(Sensitivity sensitivity);
const char* mark_target_name(MarkTarget target);

class CullSession {
 public:
  static std::optional<CullSession> begin(CullOptions options);

  ResultCode record_decision(CullDecision decision);
  ResultCode finish(CullResult& out);
  ResultCode cancel();

  bool active() const;
  std::vector<CullDecision> decisions() const;

 private:
  explicit CullSession(CullOptions options);

  CullOptions options_;
  std::vector<CullDecision> decisions_;
  bool active_ = true;
};

}  // namespace pixcake::cull
