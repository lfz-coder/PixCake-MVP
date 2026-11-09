#include "pixcake/ui/panels/portrait_panel.hpp"

#include "pixcake/ui/shell/shared_words.hpp"

#include <string_view>

namespace pixcake::ui::panels {
namespace {

using K = PanelRowKind;

// 修饰位。
constexpr unsigned kHelp = 1u << 0;    // 名字后的问号
constexpr unsigned kExpand = 1u << 1;  // 数值右边的 ▸
constexpr unsigned kChild = 1u << 2;   // 上一个可展开行或 Group 的子项
constexpr unsigned kCenter = 1u << 3;  // 双向滑杆（截图里滑块在中间）
constexpr unsigned kReset = 1u << 4;   // Section 标题栏右侧的“还原本组”

constexpr char kNew[] = "New";
constexpr char kBeta[] = "Beta";

struct Def {
  K kind;
  std::string_view label;
  unsigned flags = 0;
  std::string_view badge = {};
  std::string_view options = {};  // 用 | 分隔；档位滑杆是档数
};

// 人像美化面板（PixCake 9.3.0，2026-11-08 用户截图，所有分组展开的长图）。顺序、控件种类、角标、问号照截图。
// 可展开行的子项：高低频磨皮（脸部）是截图里展开看到的；其余写了子项的，子项名来自 Jarvis 页面图的参数全名
// （“<父项>-<子项>”），没有展开核对过；没写子项的 ▸ 在界面上禁用。卡片的图片没有素材，只画名字。
constexpr Def kPortrait[] = {
    {K::Section, "祛除瑕疵", kReset, kNew},
    {K::Subheading, "面部祛瑕疵"},
    {K::Slider, "祛斑祛痘", kExpand},
    {K::Toggle, "祛痣"},
    {K::Slider, "祛油光（脸部）"},
    {K::Slider, "祛汗珠（脸部）", 0, kNew},
    {K::Slider, "祛黑眼圈", kHelp},
    {K::Slider, "祛眼袋", kExpand},
    {K::Slider, "祛鼻孔瑕疵", 0, kNew},
    {K::Slider, "唇纹修整"},
    {K::Slider, "祛双下巴", kHelp | kExpand},
    {K::Slider, "下颌线增强", 0, kNew},
    {K::Slider, "祛胡须", kHelp | kExpand},
    {K::Slider, "祛颈纹"},
    {K::Toggle, "胡型保护", kHelp},
    {K::Toggle, "妆容保护", kHelp},
    {K::Separator, ""},
    {K::Subheading, "面部祛纹"},
    {K::Slider, "祛抬头纹"},
    {K::Slider, "祛川字纹"},
    {K::Slider, "祛眼周纹"},
    {K::Slider, "祛鼻背纹"},
    {K::PairSlider, "祛法令纹"},
    {K::Slider, "祛脸颊纹"},
    {K::Slider, "祛木偶纹"},
    {K::Slider, "祛嘴周纹"},
    {K::Separator, ""},
    {K::Subheading, "身体祛瑕疵"},
    {K::Slider, "身体祛瑕疵"},
    {K::Slider, "祛副乳"},
    {K::Slider, "祛妊娠纹", kExpand},
    {K::Toggle, "AI祛纹身", kHelp, kBeta},

    {K::Section, "皮肤调整", kReset, kNew},
    {K::Slider, "中性灰平整"},
    {K::Slider, "中性灰立体", kExpand},
    {K::Slider, "质感磨皮", kHelp},
    {K::Slider, "高低频磨皮（脸部）", kExpand},
    {K::Slider, "高频增强（脸部）", kChild | kCenter},
    {K::Slider, "低频平整（脸部）", kChild | kCenter},
    {K::Slider, "高低频磨皮（身体）", kExpand},
    {K::Slider, "高频增强（身体）", kChild | kCenter},
    {K::Slider, "低频平整（身体）", kChild | kCenter},
    {K::Slider, "水润磨皮（脸部）"},
    {K::Slider, "水润磨皮（身体）"},
    {K::Slider, "锁骨增强", kHelp, kBeta},
    {K::Separator, ""},
    {K::Subheading, "面部丰盈", kHelp, kNew},
    {K::Slider, "全脸丰盈", 0, {}, "5"},
    {K::Title, ui::shell::kWordPartAdjust},
    {K::Cards, "面部丰盈-部位", 0, {}, "额头|面中|面颊|鼻基底|眼窝|眉弓|下巴|嘴角"},
    {K::Separator, ""},
    {K::Subheading, "皮肤质感"},
    {K::Cards, "皮肤质感-选择", 0, {}, "无|哑光|缎面|水光"},
    {K::Slider, "光泽度", kCenter},
    {K::Slider, "纹理度", kCenter},
    {K::Slider, ui::shell::kWordClarity},
    {K::Separator, ""},
    {K::Subheading, "皮肤颜色"},
    {K::Slider, "肤色透亮"},
    {K::Slider, "肤色美白"},
    {K::Slider, "皮肤红润"},
    {K::Slider, "AI多人肤色统一", kHelp},
    {K::Tool, "AI全身肤色统一", kHelp, kNew},
    {K::Slider, "AI肤色统一（脸部）", kHelp},
    {K::Slider, "AI肤色统一（身体）"},
    {K::Slider, "肤色统一（身体）", kHelp},
    {K::Subheading, "肤色选择"},
    // 色块颜色取自截图像素（显示器色彩配置下的近似值）；第一个是“无”。
    {K::Swatches, "肤色选择-色块", 0, {}, "|#ffffff|#f1d8da|#e8f6fc|#f5e2cb|#b27e75|#b67e64|#73534c|#734438"},
    {K::Slider, "肤色色温", kCenter, {}, "track:skin-temperature"},
    {K::Slider, "肤色色调", kCenter, {}, "track:tint"},
    {K::AiButton, "AI补光"},
    {K::AiButton, "柔化阴影"},
    {K::AiButton, "AI面部重绘"},

    {K::Section, "面部重塑", 0, kNew},
    {K::Subheading, "脸型预设"},
    {K::Cards, "脸型预设-选择", 0, {}, "无|经典|幼态|英气|少年|圆脸适用|长脸适用|方脸适用"},
    // 页签只看到“脸型”一页的内容，其余页签禁用。
    {K::Tabs, "面部重塑-页签", 0, {}, "脸型|3D塑颜|眉毛|眼睛|鼻子|嘴巴"},
    {K::PairSlider, "瘦脸"},
    {K::PairSlider, "太阳穴", kCenter},
    {K::PairSlider, "颧骨", kCenter},
    {K::PairSlider, "下颌", kCenter},
    {K::Separator, ""},
    {K::Slider, "AI面部流畅"},
    {K::Slider, "颌面矫正", kHelp | kExpand, kNew},
    {K::Slider, "小脸", kHelp | kCenter},
    {K::PairSlider, "V脸", kCenter},
    {K::Slider, "短脸", kCenter},
    {K::PairSlider, "脸宽", kCenter},
    {K::Slider, ui::shell::kWordHairline, kCenter},
    {K::PairSlider, "额头宽度", kCenter},
    {K::PairSlider, "额头高度", kCenter},
    {K::Slider, "人中", kCenter},
    {K::Slider, "中庭", kCenter},
    {K::Slider, "下庭", kCenter},
    {K::Slider, "下巴长短", kCenter},
    {K::PairSlider, "尖下巴", kCenter},
    {K::Slider, "收下巴", kCenter},
    {K::Checkbox, "使用旧版"},
    {K::Toggle, "对称液化（适用证件照）", kHelp},

    {K::Section, "表情管理", kReset, kNew},
    {K::AiButton, "AI完美表情"},
    {K::Subheading, "笑容管理", kHelp},
    {K::Cards, "笑容管理-选择", 0, {}, "无|露齿笑|抿嘴笑|闭合嘴巴"},
    {K::Slider, "微笑嘴角", kHelp},

    {K::Section, "牙齿美化"},
    {K::Slider, "牙齿祛瑕疵", kExpand},
    {K::Slider, "牙齿美白", kExpand},
    {K::Slider, "明度", kChild},
    {K::Slider, "祛黄", kChild},
    {K::Slider, "牙齿排齐", kHelp, {}, "6"},
    {K::PairSlider, "露龈调整"},
    {K::Toggle, "牙齿修复", 0, kBeta},

    {K::Section, "眼睛增强", kReset, kNew},
    {K::Toggle, "AI眼球调整", kHelp, kNew},
    {K::Toggle, "眼睛对称"},
    {K::PairSlider, "亮眼", kExpand},
    {K::PairSlider, "眼白", kChild},
    {K::PairSlider, "眼睛高光", kChild},
    {K::PairSlider, "眼瞳", kChild},
    {K::Slider, "祛红血丝"},
    {K::Slider, "眼白净化"},
    {K::Toggle, "祛红眼", kHelp},
    {K::PairSlider, "祛双眼皮贴"},
    {K::Slider, "眼镜祛反光"},
    {K::Subheading, "双眼皮", kHelp},
    {K::Cards, "双眼皮-选择", 0, {}, "无|开扇|平行|新月|平扇|欧式|单眼皮"},
    {K::Separator, ""},
    {K::Subheading, "眼神光"},
    {K::Tabs, "眼神光-页签", 0, {}, "通用|棚内"},
    {K::Cards, "眼神光-选择", 0, {}, "无|01|02|03|04|05|06"},

    {K::Section, "妆容调整", 0, kNew},
    {K::Slider, "高光立体"},
    {K::Slider, "阴影立体"},
    {K::PairSlider, "眉毛增强", kCenter},
    {K::PairSlider, "眼妆增强", kCenter | kExpand},
    {K::PairSlider, "明度", kChild | kCenter},
    {K::PairSlider, "立体度", kChild | kCenter},
    {K::PairSlider, ui::shell::kWordSaturation, kChild | kCenter},
    {K::Slider, "唇妆增强", kCenter | kExpand},
    {K::Slider, "明度", kChild | kCenter},
    {K::Slider, "立体度", kChild | kCenter},
    {K::Slider, ui::shell::kWordSaturation, kChild | kCenter},
    {K::Separator, ""},
    {K::Subheading, "妆容减淡", 0, kNew},
    {K::Toggle, "全脸减淡"},
    {K::Title, ui::shell::kWordPartAdjust},
    {K::Cards, "妆容减淡-部位", 0, {}, "口红|修容|眉毛|眼妆|腮红|面部装饰"},
    // 页签只看到“主题妆容”一页的内容，其余页签禁用。
    {K::Tabs, "妆容调整-页签", 0, {}, "主题妆容|眉毛|眼妆|眼线|睫毛|美瞳|卧蚕|腮红|口红|脸部装饰|修容"},
    {K::Cards, "主题妆容-选择", 0, {},
     "无|伪素颜|质感|甜美|日系|复古|欧美|锦鲤妆|千金妆|雀斑妆|阳光|韩式|清冷|初恋|轻泰|英气|中式|率真|少女|童真|新生|优雅|绅士|知性|精英"},

    {K::Section, "头发调整", kReset},
    {K::Slider, "发缝填充"},
    {K::Slider, "颅顶调整", kCenter},
    {K::Slider, "颅顶调整（头顶）", kCenter, kNew},
    {K::PairSlider, "发量调整", kCenter},
    {K::Slider, ui::shell::kWordHairline, kCenter},
    {K::Slider, "祛碎发", kExpand},
    {K::Slider, "人物区域", kChild},
    {K::Slider, "背景区域", kChild},
    {K::Slider, "头发柔顺", kExpand},
    {K::Slider, "发丝调整", kChild},
    {K::Slider, "头发光泽", 0, kBeta},
    {K::Slider, "祛白发", 0, kBeta},
    {K::Slider, "发色统一", kHelp | kCenter},
    {K::Subheading, "发色选择"},
    {K::Cards, "发色选择-选择", 0, {},
     "无|蜜茶棕|栗棕|落日橘|冷茶棕|苦亚麻|自然黑|玫瑰紫|蓝黑|鸢尾蓝紫|树莓粉棕|枫叶红|苔绿|薄藤粉"},

    {K::Section, "手部美化", 0, kNew},
    {K::Slider, "手部中性灰"},
    {K::Slider, ui::shell::kWordRemoveVeins},
    {K::Slider, "手部美型", 0, kNew},

    {K::Section, "脚部美化", kReset, kNew},
    {K::Slider, ui::shell::kWordRemoveVeins, 0, kNew},

    {K::Section, "全身美型", kHelp, kNew},
    {K::Slider, "AI塑形", kCenter | kExpand},
    {K::Slider, "瘦身", kCenter},
    {K::Slider, "增高"},
    {K::Slider, "小头", kHelp | kCenter},
    {K::Slider, "脖子长度", kCenter},
    {K::PairSlider, "瘦脖子", kCenter},
    {K::PairSlider, "天鹅颈"},
    {K::PairSlider, "直角肩"},
    {K::PairSlider, "肩膀宽度", kCenter},
    {K::Slider, "瘦手臂", kCenter},
    {K::Slider, "丰胸", kCenter},
    {K::Slider, "胸位提升", 0, kNew},
    {K::Slider, "瘦腰"},
    {K::Slider, "腰部长度", kHelp | kCenter},
    {K::Slider, "上身长度", kHelp | kCenter},
    {K::Slider, "美臀"},
    {K::Group, "瘦腿"},
    {K::Slider, "大腿", kChild | kCenter},
    {K::Slider, "小腿", kChild | kCenter},
    {K::Slider, "长腿", kExpand},
    {K::Slider, "大腿", kChild},
    {K::Slider, "小腿", kChild},
    {K::AiButton, "AI瘦肚子"},
};

std::vector<std::string> split(std::string_view text) {
  std::vector<std::string> parts;
  std::size_t start = 0;
  while (start <= text.size()) {
    const std::size_t bar = text.find('|', start);
    const std::size_t end = bar == std::string_view::npos ? text.size() : bar;
    parts.emplace_back(text.substr(start, end - start));
    if (bar == std::string_view::npos) break;
    start = bar + 1;
  }
  return parts;
}

}  // namespace

std::vector<PanelRow> refine_portrait_rows() {
  std::vector<PanelRow> rows;
  for (const auto& def : kPortrait) {
    PanelRow row;
    row.kind = def.kind;
    row.label = std::string(def.label);
    row.badge = std::string(def.badge);
    row.help = (def.flags & kHelp) != 0;
    row.reset = (def.flags & kReset) != 0;
    row.expandable = (def.flags & kExpand) != 0;
    row.child = (def.flags & kChild) != 0;
    row.centered = (def.flags & kCenter) != 0;
    if (def.kind == K::Cards || def.kind == K::Swatches || def.kind == K::Tabs) {
      row.options = split(def.options);
    } else if (def.options.rfind("track:", 0) == 0) {
      row.track = std::string(def.options.substr(6));
    } else if (!def.options.empty()) {
      row.ticks = std::stoi(std::string(def.options));
    }
    rows.push_back(std::move(row));
  }
  return rows;
}

std::vector<std::string> refine_portrait_people() { return {"女", "男", "儿童", "长辈女", "长辈男", "单人", "所有人"}; }

}  // namespace pixcake::ui::panels
