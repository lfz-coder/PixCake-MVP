#pragma once

#include <string>
#include <vector>

namespace pixcake {

struct CloudMarkedProject {
  std::string label;
  bool cloud_badge = false;
};

struct PlacedMark {
  std::string place;
  std::string label;
};

const char* account_input_kind();
const char* home_search_looks_for();
std::vector<CloudMarkedProject> home_project_kinds();
std::vector<std::string> local_project_kinds();
std::vector<std::string> cloud_space_kinds();
int deleted_item_keep_days();
const char* project_list_shows();
const char* support_action();
const char* message_center_shows();
const char* transfer_list_shows();
const char* export_list_shows();
const char* refine_export_target();
const char* refine_canvas_shows();
const char* compare_button_corner();
const char* full_effect_on_image();
const char* full_effect_off_image();
int thumbnail_mark_limit();
std::vector<PlacedMark> thumbnail_marks();
const char* color_label_paints();
std::vector<std::string> view_card_labels();
const char* view_card_close_label();
const char* view_card_reflow();
const char* single_view_form();
const char* filter_view_layout();
const char* filter_view_selection();
const char* creative_canvas_shows();
bool creative_canvas_drop_imports_layer();

}  // namespace pixcake
