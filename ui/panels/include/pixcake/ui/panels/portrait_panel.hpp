#pragma once

#include "pixcake/ui/panels/slider_spec.hpp"

#include <vector>

namespace pixcake::ui::panels {

// 精修“人像美化”面板的行（PixCake 9.3.0，2026-11-08 用户截图）：十一个可折叠分组，组内的小标题、滑杆、开关、
// 左右联动滑杆、卡片、色块、AI 按钮和页签按截图的顺序。人像效果还没有实现，行都不带参数绑定。
std::vector<PanelRow> refine_portrait_rows();
// 面板顶部的人物属性（女、男、儿童、长辈女、长辈男、单人、所有人）。
std::vector<std::string> refine_portrait_people();

}  // namespace pixcake::ui::panels
