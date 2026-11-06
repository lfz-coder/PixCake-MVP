#include "pixcake/recipe/recipe.hpp"

#include <algorithm>
#include <cstddef>
#include <utility>

namespace pixcake::recipe {
namespace {

bool valid_identifier(const std::string& value) {
  return !value.empty() && value.find('\0') == std::string::npos;
}

bool has_image(const std::vector<std::string>& image_ids, const std::string& image_id) {
  return std::find(image_ids.begin(), image_ids.end(), image_id) != image_ids.end();
}

}  // namespace

Recipe::Recipe(std::string recipe_id, std::string name)
    : recipe_id_(std::move(recipe_id)), name_(std::move(name)) {}

std::optional<Recipe> Recipe::create(std::string recipe_id, std::string name) {
  if (!valid_identifier(recipe_id) || !valid_identifier(name)) {
    return std::nullopt;
  }
  return Recipe(std::move(recipe_id), std::move(name));
}

ResultCode Recipe::append_node(RecipeNode node) {
  if (!valid_identifier(node.node_id) || !valid_identifier(node.tool_id)) {
    return ResultCode::StateConflict;
  }
  const auto duplicate = std::find_if(nodes_.begin(), nodes_.end(), [&node](const RecipeNode& existing) {
    return existing.node_id == node.node_id;
  });
  if (duplicate != nodes_.end()) {
    return ResultCode::StateConflict;
  }
  nodes_.push_back(std::move(node));
  return ResultCode::Ok;
}

ResultCode Recipe::remove_node(const std::string& node_id) {
  const auto found = std::find_if(nodes_.begin(), nodes_.end(), [&node_id](const RecipeNode& node) {
    return node.node_id == node_id;
  });
  if (found == nodes_.end()) {
    return ResultCode::StateConflict;
  }
  nodes_.erase(found);
  return ResultCode::Ok;
}

ResultCode Recipe::move_node(const std::string& node_id, std::size_t destination_index) {
  if (destination_index >= nodes_.size()) {
    return ResultCode::StateConflict;
  }
  const auto found = std::find_if(nodes_.begin(), nodes_.end(), [&node_id](const RecipeNode& node) {
    return node.node_id == node_id;
  });
  if (found == nodes_.end()) {
    return ResultCode::StateConflict;
  }
  RecipeNode moved = std::move(*found);
  nodes_.erase(found);
  nodes_.insert(nodes_.begin() + static_cast<std::ptrdiff_t>(destination_index), std::move(moved));
  return ResultCode::Ok;
}

std::string Recipe::recipe_id() const { return recipe_id_; }

std::string Recipe::name() const { return name_; }

std::vector<RecipeNode> Recipe::nodes() const { return nodes_; }

bool RecipeRun::active() const { return state_ == State::Running; }

std::string RecipeRun::recipe_id() const { return recipe_id_; }

bool RecipeRun::occupies(const std::string& image_id) const {
  return active() && has_image(image_ids_, image_id);
}

std::vector<std::string> RecipeRun::image_ids() const { return image_ids_; }

ResultCode RecipeRuntime::begin(const Recipe& recipe, const std::vector<std::string>& image_ids,
                                RecipeRun& out) {
  if (recipe.nodes_.empty() || image_ids.empty() || out.active()) {
    return ResultCode::StateConflict;
  }
  std::vector<std::string> unique;
  for (const auto& image_id : image_ids) {
    if (!valid_identifier(image_id) || has_image(unique, image_id) || occupies(image_id)) {
      return ResultCode::StateConflict;
    }
    unique.push_back(image_id);
  }
  out.recipe_id_ = recipe.recipe_id_;
  out.image_ids_ = unique;
  out.state_ = RecipeRun::State::Running;
  occupied_images_.insert(occupied_images_.end(), unique.begin(), unique.end());
  return ResultCode::Ok;
}

ResultCode RecipeRuntime::complete(RecipeRun& run) {
  if (!run.active()) {
    return ResultCode::StateConflict;
  }
  for (const auto& image_id : run.image_ids_) {
    const auto found = std::find(occupied_images_.begin(), occupied_images_.end(), image_id);
    if (found == occupied_images_.end()) {
      return ResultCode::StateConflict;
    }
  }
  for (const auto& image_id : run.image_ids_) {
    const auto found = std::find(occupied_images_.begin(), occupied_images_.end(), image_id);
    occupied_images_.erase(found);
  }
  run.image_ids_.clear();
  run.state_ = RecipeRun::State::Completed;
  return ResultCode::Ok;
}

ResultCode RecipeRuntime::cancel(RecipeRun& run) {
  if (!run.active()) {
    return ResultCode::StateConflict;
  }
  for (const auto& image_id : run.image_ids_) {
    const auto found = std::find(occupied_images_.begin(), occupied_images_.end(), image_id);
    if (found == occupied_images_.end()) {
      return ResultCode::StateConflict;
    }
  }
  for (const auto& image_id : run.image_ids_) {
    const auto found = std::find(occupied_images_.begin(), occupied_images_.end(), image_id);
    occupied_images_.erase(found);
  }
  run.image_ids_.clear();
  run.state_ = RecipeRun::State::Cancelled;
  return ResultCode::Ok;
}

bool RecipeRuntime::occupies(const std::string& image_id) const {
  return has_image(occupied_images_, image_id);
}

}  // namespace pixcake::recipe
