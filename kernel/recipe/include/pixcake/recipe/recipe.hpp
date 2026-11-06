#pragma once

#include "pixcake/support/result_code.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace pixcake::recipe {

struct RecipeNode {
  std::string node_id;
  std::string tool_id;
  std::vector<std::uint8_t> parameters;
  bool editable = false;
};

class RecipeRun;

class Recipe {
 public:
  static std::optional<Recipe> create(std::string recipe_id, std::string name);

  ResultCode append_node(RecipeNode node);
  ResultCode remove_node(const std::string& node_id);
  ResultCode move_node(const std::string& node_id, std::size_t destination_index);

  std::string recipe_id() const;
  std::string name() const;
  std::vector<RecipeNode> nodes() const;

 private:
  Recipe(std::string recipe_id, std::string name);
  friend class RecipeRuntime;

  std::string recipe_id_;
  std::string name_;
  std::vector<RecipeNode> nodes_;
};

class RecipeRun {
 public:
  RecipeRun() = default;

  bool active() const;
  std::string recipe_id() const;
  bool occupies(const std::string& image_id) const;
  std::vector<std::string> image_ids() const;

 private:
  friend class Recipe;
  friend class RecipeRuntime;
  enum class State { Inactive, Running, Completed, Cancelled };

  std::string recipe_id_;
  std::vector<std::string> image_ids_;
  State state_ = State::Inactive;
};

// Owns the cross-recipe batch occupancy table. It does not execute tools or
// charge points; callers pass the run to ledger/generation services.
class RecipeRuntime {
 public:
  ResultCode begin(const Recipe& recipe, const std::vector<std::string>& image_ids, RecipeRun& out);
  ResultCode complete(RecipeRun& run);
  ResultCode cancel(RecipeRun& run);
  bool occupies(const std::string& image_id) const;

 private:
  std::vector<std::string> occupied_images_;
};

}  // namespace pixcake::recipe
