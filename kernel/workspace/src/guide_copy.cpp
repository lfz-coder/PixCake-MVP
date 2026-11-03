#include "pixcake/workspace/guide_copy.hpp"

namespace pixcake {

const char* account_input_kind() { return "手机号"; }
const char* home_search_looks_for() { return "项目名称或关键词"; }
const char* project_list_shows() { return "全部项目"; }
int deleted_item_keep_days() { return 30; }

std::vector<CloudMarkedProject> home_project_kinds() {
  return {CloudMarkedProject{"本台设备创建的项目", false},
          CloudMarkedProject{"已上传至云空间的项目", true},
          CloudMarkedProject{"从云空间下载到本台设备的项目", true}};
}

std::vector<std::string> local_project_kinds() {
  return {"本台设备创建的项目", "从云空间下载到本台设备的项目"};
}

std::vector<std::string> cloud_space_kinds() {
  return {"个人空间", "管理员创建的团队空间"};
}

const char* support_action() { return "在线联系像素蛋糕官方售后技术获取支持"; }
const char* message_center_shows() { return "查看软件内消息"; }
const char* transfer_list_shows() { return "查看上传及下载任务"; }
const char* export_list_shows() { return "查看导出任务"; }

const char* refine_export_target() { return "当前选中的图片"; }
const char* refine_canvas_shows() { return "精修后的图片"; }
const char* compare_button_corner() { return "右下角"; }
const char* full_effect_on_image() { return "效果图"; }
const char* full_effect_off_image() { return "原图"; }
int thumbnail_mark_limit() { return 6; }

std::vector<PlacedMark> thumbnail_marks() {
  return {PlacedMark{"上方", "RAW图"}, PlacedMark{"上方", "已导出"}, PlacedMark{"上方", "已编辑"},
          PlacedMark{"下方", "旗标"}, PlacedMark{"下方", "星标"}, PlacedMark{"下方", "色标"}};
}

const char* color_label_paints() { return "缩略图框背景色"; }

std::vector<std::string> view_card_labels() {
  return {"AI精选标签", "旗标", "星标", "颜色标"};
}

const char* view_card_close_label() { return "X"; }
const char* view_card_reflow() { return "动态补位"; }
const char* single_view_form() { return "大图"; }
const char* filter_view_layout() { return "同屏对比多张图片"; }
const char* filter_view_selection() { return "与缩略图库实时同步选中状态"; }
const char* creative_canvas_shows() { return "当前选中的图片及对应图层的效果"; }
bool creative_canvas_drop_imports_layer() { return true; }

}  // namespace pixcake
