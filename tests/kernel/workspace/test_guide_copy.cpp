#include "pixcake/workspace/guide_copy.hpp"

#include <cstdio>
#include <string>
#include <vector>

namespace {

int g_failures = 0;

void expect(bool cond, const char* text, int line) {
  if (!cond) {
    std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, line, text);
    ++g_failures;
  }
}

#define EXPECT(cond) expect(static_cast<bool>(cond), #cond, __LINE__)

}  // namespace

int main() {
  EXPECT(std::string(pixcake::account_input_kind()) == "手机号");
  EXPECT(std::string(pixcake::home_search_looks_for()) == "项目名称或关键词");
  EXPECT(std::string(pixcake::project_list_shows()) == "全部项目");
  EXPECT(pixcake::deleted_item_keep_days() == 30);

  const std::vector<pixcake::CloudMarkedProject> home = pixcake::home_project_kinds();
  EXPECT(home.size() == 3);
  EXPECT(home[0].label == "本台设备创建的项目");
  EXPECT(!home[0].cloud_badge);
  EXPECT(home[1].label == "已上传至云空间的项目");
  EXPECT(home[1].cloud_badge);
  EXPECT(home[2].label == "从云空间下载到本台设备的项目");
  EXPECT(home[2].cloud_badge);

  const std::vector<std::string> local = pixcake::local_project_kinds();
  EXPECT(local.size() == 2);
  EXPECT(local[0] == "本台设备创建的项目");
  EXPECT(local[1] == "从云空间下载到本台设备的项目");

  const std::vector<std::string> cloud = pixcake::cloud_space_kinds();
  EXPECT(cloud.size() == 2);
  EXPECT(cloud[0] == "个人空间");
  EXPECT(cloud[1] == "管理员创建的团队空间");

  EXPECT(std::string(pixcake::support_action()) == "在线联系像素蛋糕官方售后技术获取支持");
  EXPECT(std::string(pixcake::message_center_shows()) == "查看软件内消息");
  EXPECT(std::string(pixcake::transfer_list_shows()) == "查看上传及下载任务");
  EXPECT(std::string(pixcake::export_list_shows()) == "查看导出任务");

  // 主页导航栏 7 项、常用功能为空：显示模型的核对移到 tests/ui/test_shell_display.cpp。

  EXPECT(std::string(pixcake::refine_export_target()) == "当前选中的图片");
  EXPECT(std::string(pixcake::refine_canvas_shows()) == "精修后的图片");
  EXPECT(std::string(pixcake::compare_button_corner()) == "右下角");
  EXPECT(std::string(pixcake::full_effect_on_image()) == "效果图");
  EXPECT(std::string(pixcake::full_effect_off_image()) == "原图");
  EXPECT(pixcake::thumbnail_mark_limit() == 6);

  const std::vector<pixcake::PlacedMark> marks = pixcake::thumbnail_marks();
  EXPECT(marks.size() == 6);
  EXPECT(marks[0].place == "上方" && marks[0].label == "RAW图");
  EXPECT(marks[1].place == "上方" && marks[1].label == "已导出");
  EXPECT(marks[2].place == "上方" && marks[2].label == "已编辑");
  EXPECT(marks[3].place == "下方" && marks[3].label == "旗标");
  EXPECT(marks[4].place == "下方" && marks[4].label == "星标");
  EXPECT(marks[5].place == "下方" && marks[5].label == "色标");
  EXPECT(std::string(pixcake::color_label_paints()) == "缩略图框背景色");

  const std::vector<std::string> cards = pixcake::view_card_labels();
  EXPECT(cards.size() == 4);
  EXPECT(cards[0] == "AI精选标签");
  EXPECT(cards[1] == "旗标");
  EXPECT(cards[2] == "星标");
  EXPECT(cards[3] == "颜色标");
  EXPECT(std::string(pixcake::view_card_close_label()) == "X");
  EXPECT(std::string(pixcake::view_card_reflow()) == "动态补位");
  EXPECT(std::string(pixcake::single_view_form()) == "大图");
  EXPECT(std::string(pixcake::filter_view_layout()) == "同屏对比多张图片");
  EXPECT(std::string(pixcake::filter_view_selection()) == "与缩略图库实时同步选中状态");

  EXPECT(std::string(pixcake::creative_canvas_shows()) == "当前选中的图片及对应图层的效果");
  EXPECT(pixcake::creative_canvas_drop_imports_layer());

  if (g_failures != 0) {
    std::fprintf(stderr, "%d failure(s)\n", g_failures);
    return 1;
  }
  return 0;
}
