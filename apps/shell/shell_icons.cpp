#include "shell_icons.hpp"

#include <map>
#include <set>
#include <string>

namespace pixcake::shell {
namespace {

using Table = std::map<std::string, std::string, std::less<>>;

const Table& tools() {
  static const Table table = {
      {"zoom", "zoom-display"}, {"hand", "hand"},          {"fringe", "edge-remove"},   {"guides", "guides"},
      {"liquify", "liquify"},   {"spot", "spot-heal"},     {"patch", "patch"},          {"clone", "clone-stamp"},
      {"remove", "smart-remove"}, {"point", "ai-spot"},    {"reset", "reset"},          {"preset", "preset"},
      {"screen", "screen-mode"}, {"move", "move-layer"},   {"scale", "zoom-tool"},      {"marquee", "marquee"},
      {"lasso", "lasso"},       {"crop", "crop"}};
  return table;
}

const Table& global() {
  static const Table table = {
      {"search", "workspace-global/search.svg"},       {"membership", "workspace-global/membership.svg"},
      {"support", "workspace-global/support.svg"},     {"account", "workspace-global/account.svg"},
      {"messages", "workspace-global/messages.svg"},   {"transfers", "workspace-global/transfer.svg"},
      {"export-list", "workspace-global/export-list.svg"}, {"export", "workspace-global/export.svg"},
      {"assistant", "workspace-global/assistant.svg"}, {"ai", "workspace-global/ai-tools.svg"},
      {"history", "workspace-global/history.svg"},     {"batch", "workspace-global/batch.svg"},
      {"help", "help.svg"},                            {"merchant", "merchant.svg"},
      {"back", "workspace-global/home.svg"},           {"home", "workspace-global/home.svg"},
      {"filter", "workspace-global/filter.svg"},       {"sort", "workspace-global/sort.svg"},
      {"star", "workspace-global/star.svg"},           {"flag", "workspace-global/flag.svg"},
      {"color", "workspace-global/color-tag.svg"},     {"grid", "workspace-global/view-grid.svg"},
      {"single", "workspace-global/view-single.svg"},  {"filter-view", "workspace-global/view-filter.svg"},
      {"compare-view", "workspace-global/view-compare.svg"}, {"reference", "workspace-global/view-reference.svg"},
      {"library", "workspace-global/tab-library.svg"}, {"refine", "workspace-global/tab-refine.svg"},
      {"creative", "workspace-global/tab-creative.svg"}};
  return table;
}

const Table& home_nav() {
  static const Table table = {{"home", "home-nav/home.svg"},           {"toolbox", "home-nav/ai-toolbox.svg"},
                              {"local", "home-nav/local-projects.svg"}, {"cloud", "home-nav/cloud.svg"},
                              {"workflow", "home-nav/workflow.svg"},   {"delivery", "home-nav/delivery.svg"},
                              {"trash", "home-nav/trash.svg"}};
  return table;
}

const Table& home_cards() {
  static const Table table = {{"capture", "home-capture.svg"},
                              {"toolbox", "home-ai-toolbox.svg"},
                              {"assistant", "home-assistant.svg"}};
  return table;
}

QString lookup(const Table& table, std::string_view id, const char* prefix = "", const char* suffix = "") {
  const auto found = table.find(id);
  if (found == table.end()) return {};
  return QString::fromLatin1(prefix) + QString::fromStdString(found->second) + QString::fromLatin1(suffix);
}

}  // namespace

QString iconPath(std::string_view list, std::string_view id) {
  if (list == "refine-tools" || list == "creative-tools") return lookup(tools(), id, "workspace-tools/", ".svg");
  if (list == "home-nav") return lookup(home_nav(), id);
  if (list == "home-cards") return lookup(home_cards(), id);
  // 创意侧边栏只有“AI工具”和“帮助”两项有图标。
  if (list == "creative-side" && id != "ai" && id != "help") return {};
  return lookup(global(), id);
}

}  // namespace pixcake::shell
