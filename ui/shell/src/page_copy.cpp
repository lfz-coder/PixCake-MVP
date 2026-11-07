#include "pixcake/ui/shell/page_copy.hpp"

#include "pixcake/ui/shell/shared_words.hpp"
#include "pixcake/workspace/guide_copy.hpp"

#include <iterator>
#include <map>
#include <set>

namespace pixcake::ui::shell {
namespace {

using Texts = std::map<std::string, std::string, std::less<>>;
using Lists = std::map<std::string, std::vector<CopyItem>, std::less<>>;

// 多处出现的词只写一次。
constexpr char kSearch[] = "搜索";
constexpr char kFilter[] = "筛选";
constexpr char kSort[] = "排序";
constexpr char kImport[] = "导入新图片";
constexpr char kExport[] = "导出";
constexpr char kHelp[] = "帮助";
constexpr char kLibrary[] = "图库";
constexpr char kRefine[] = "精修";
constexpr char kCreative[] = "创意";
constexpr char kSide[] = "侧边栏";
constexpr char kThumbs[] = "缩略图区";
constexpr char kCanvas[] = "画布";
constexpr char kGlobal[] = "全局功能区";
constexpr char kSingle[] = "单视图";
constexpr char kCull[] = "去挑图";
constexpr const char* kPreset = kWordPreset;
constexpr char kAssistant[] = "像素助手";
constexpr char kToolbox[] = "AI工具箱";
constexpr char kAiTools[] = "AI工具";
constexpr char kHistory[] = "历史记录";
constexpr char kCapture[] = "联机拍摄";
constexpr char kEmpty[] = "暂无内容";
constexpr char kNoPhoto[] = "暂无已加载照片";
constexpr char kLiquify[] = "液化";
constexpr char kClone[] = "仿制图章";
constexpr char kZoom[] = "显示比例调整";
constexpr char kHand[] = "抓手";

CopyItem item(std::string id, std::string text, std::string guide = {}, std::string accessible = {},
              std::string description = {}) {
  return CopyItem{std::move(id), std::move(text), std::move(guide), std::move(accessible), std::move(description)};
}

Texts build_texts() {
  Texts t;
  t["app.title"] = "像素蛋糕";
  t["page.login"] = "登录页面";
  t["page.home"] = "主页面";
  t["page.library"] = kLibrary;
  t["page.refine"] = kRefine;
  t["page.creative"] = kCreative;
  t["common.empty"] = kEmpty;
  t["common.no_photo"] = kNoPhoto;
  t["icon.pending"] = "待补充官方图标";
  t["icon.pending_suffix"] = "待补充";
  t["icon.suffix"] = "图标";
  t["icon.pending_named"] = "图标待补充";
  t["widget.histogram"] = "直方图";
  t["widget.canvas"] = "照片预览";
  t["refine.preview.loading"] = "正在生成预览";
  t["refine.profile.pending"] = "这个配置文件的资源还没有接入";
  t["refine.preview.failed"] = "预览失败：";

  t["region.login-account"] = "账号输入框";
  t["region.login-code"] = "验证码输入框";
  t["region.login-methods"] = "登录方式切换";
  t["region.login-help"] = kHelp;
  t["login.account.placeholder"] = "输入手机号";
  t["login.code.placeholder"] = "输入短信中的验证码";
  t["login.submit"] = "登录";
  t["login.agreement"] = "已阅读并同意像素蛋糕《用户使用协议》《隐私政策》";
  t["login.or"] = "or";
  t["login.merchant.button"] = "商家账号";

  t["region.home-nav"] = "导航栏";
  t["region.home-import"] = "导入照片创建新项目";
  t["region.home-search"] = kSearch;
  t["region.home-global"] = kGlobal;
  t["region.home-common"] = "常用功能";
  t["region.home-quick"] = "快捷功能";
  t["region.home-projects"] = "项目列表";
  t["home.more_versions"] = "了解更多版本功能";
  t["home.search.name"] = "项目搜索";
  t["home.search.placeholder"] = "搜索所有项目";
  t["home.import.name"] = "照片导入";
  t["home.import.plus"] = "＋";
  t["home.import.hint"] = "拖动或点击导入图片";
  t["home.import.folder"] = "导入文件夹";
  t["home.import.dialog"] = "选择要导入的照片";
  t["home.import.filter"] = "图片 (*.jpg *.jpeg *.png *.tif *.tiff *.bmp)";
  t["library.import"] = "＋ 导入照片";
  t["library.open.hint"] = "单击选中，双击查看大图";
  t["home.quick.empty"] = "暂无快捷功能";
  t["home.projects.empty"] = "暂无项目";

  t["region.library-nav"] = "项目内导航";
  t["region.library-status"] = "显示状态";
  t["region.library-effect"] = "全效果模式";
  t["region.library-thumbs"] = kThumbs;
  t["region.library-delete"] = "批量删除";
  t["region.library-side"] = kSide;
  t["region.library-tools"] = "导入新图片/筛选/排序";
  t["region.library-views"] = "视图切换";
  t["library.status.accessible"] = "显示状态：显示RAW格式、显示修图状态、显示导出状态";
  t["library.selected"] = "已选 %1 张（共 %2 张）";
  t["library.empty"] = "暂无照片";
  t["library.empty.hint"] = "导入图片后将在此显示";
  t["library.span"] = "网格每行张数，范围 1 到 7，默认位置未确认";
  t["thumbs.add.name"] = "添加图片";
  t["thumbs.selected"] = "已选 %1 张";
  t["thumbs.empty"] = "暂无缩略图";

  t["region.refine-top"] = "返回主页面、搜索和像素助手";
  t["region.refine-switch"] = "图库/精修/创意页面切换";
  t["region.refine-global"] = kGlobal;
  t["region.refine-toolbar"] = "工具栏";
  t["region.refine-canvas"] = kCanvas;
  t["region.refine-thumbs"] = kThumbs;
  t["region.refine-modules"] = "功能区";
  t["refine.back.button"] = "返回";
  t["refine.preview"] = "预览区域\n暂无已加载照片";
  t["refine.rgb.name"] = "RGB读数，暂无已加载照片";
  t["refine.lab.name"] = "Lab读数，暂无已加载照片";
  t["refine.clothing.name"] = "衣服调整";
  t["refine.history.empty.name"] = "暂无已加载照片。关闭软件后再次打开时，先前步骤合并为上一次保存。当前不列出步骤。";

  t["region.creative-tools"] = "手动工具栏";
  t["region.creative-canvas"] = kCanvas;
  t["region.creative-thumbs"] = kThumbs;
  t["region.creative-toolbox"] = kToolbox;
  t["region.creative-side"] = kSide;
  t["region.creative-layers"] = "图层面板";
  t["creative.empty"] = "暂无图片或图层";
  t["creative.empty.hint"] = "将照片拖入画布后开始创作";

  // 导出弹窗与导出列表（需求第 19 节；字样与默认值见
  // docs/superpowers/evidence/2026-11-06-export-dialog-observation.md）。
  t["export.heading"] = "选择一个预设应用到 %1 张图片并导出";
  t["export.count.refined"] = "精修效果图(%1)";
  t["export.count.free"] = "免费效果图(%1)";
  t["export.count.original"] = "原图(%1)";
  t["export.estimate.note"] = "导出精修效果/免费效果为预估数，最终以导出明细为准";
  // 本机账本的张数：同一张图扣过就不再扣，原图和免费效果图不扣。
  t["export.sheets"] = "本次预计扣除 %1 张，剩余 %2 张";
  // 导出列表的计费一栏（test 版导出明细：精修效果图、免费功能、实际计费）。
  // 筛选与排序（需求 §6.2、§6.3）。
  t["filter.title"] = "筛选与排序";
  t["filter.rating"] = "星级";
  t["filter.rating.from"] = "大于等于";
  t["filter.rating.to"] = "小于等于";
  t["filter.flag"] = "按旗标";
  t["filter.color"] = "按色标";
  t["filter.none.flag"] = "无旗标";
  t["filter.none.color"] = "无色标";
  t["filter.edit"] = "修图状态";
  t["filter.not.edited"] = "未编辑";
  t["filter.export"] = "导出状态";
  t["filter.not.exported"] = "未导出";
  t["filter.format"] = "格式";
  t["filter.sort"] = "排序";
  t["filter.sort.import"] = "导入顺序";
  t["filter.sort.capture"] = "拍摄时间";
  t["filter.sort.edit"] = "编辑时间";
  t["filter.sort.rating"] = "星级";
  t["filter.sort.type"] = "文件类型";
  t["filter.sort.size"] = "文件大小";
  t["filter.sort.name"] = "文件名";
  t["filter.sort.descending"] = "降序";
  t["filter.reset"] = "重置";
  t["filter.shown"] = "显示 %1 张（共 %2 张）";
  t["library.filtered_empty"] = "没有符合条件的图片";
  // 标记（需求 §6.1）。
  t["mark.star"] = "★%1";
  t["mark.menu.rating"] = "设置星级";
  t["mark.menu.flag"] = "设置旗标";
  t["mark.menu.color"] = "设置色标";
  t["mark.none"] = "无";
  t["mark.remove"] = "移除";
  // 删除与回收站（需求 §7）。官方回收站按项目卡片显示“还剩：N天”，右键“还原”“彻底删除”（2026-11-07 观察）；
  // 这里是一个弹窗，图片和副本也列出来。
  t["trash.title"] = "回收站";
  t["trash.count"] = "共%1个项目";
  t["trash.empty"] = "回收站是空的";
  t["trash.left"] = "还剩：%1天";
  t["trash.restore"] = "还原";
  t["trash.erase"] = "彻底删除";
  t["trash.cancel"] = "取消";
  t["trash.col.name"] = "名称";
  t["trash.col.kind"] = "类型";
  t["trash.col.project"] = "所在项目";
  t["trash.col.left"] = "保留时间";
  t["trash.name.copy"] = "%1 · 副本 %2";
  t["trash.kind.project"] = "项目";
  t["trash.kind.photo"] = "图片";
  t["trash.kind.copy"] = "虚拟副本";
  t["trash.confirm.title"] = "确认删除";
  t["trash.confirm.ok"] = "删除";
  t["trash.confirm.photo"] = "确认删除该图片？删除后进入回收站，保留 30 天。";
  t["trash.confirm.photos"] = "确认删除所选的 %1 张图片？删除后进入回收站，保留 30 天。";
  t["trash.confirm.project"] = "确认删除项目“%1”？删除后进入回收站，保留 30 天。";
  t["trash.erase.confirm"] = "彻底删除项目“%1”后不能恢复（磁盘上的原图不会被删除）。确认彻底删除？";
  t["library.delete.menu"] = "删除";
  t["home.project.delete"] = "删除项目";
  // 虚拟副本（需求 §7）。
  t["library.copy.create"] = "创建虚拟副本";
  t["library.copy.batch"] = "批量创建虚拟副本";
  t["library.copy.tag"] = "副本 %1";
  t["export.charge.pending"] = "待记账";
  t["export.charge.charged"] = "扣 1 张";
  t["export.charge.repeat"] = "不扣：这张图已扣过";
  t["export.charge.free"] = "不扣：免费效果图";
  t["export.charge.original"] = "不扣：原图";
  t["export.charge.copy"] = "不扣：无水印图片不另计";
  // 个人中心：剩余张数来自本机账本（没有服务器，不跨设备同步）。
  t["account.sheets"] = "剩余张数：%1 张";
  t["account.sheets.note"] = "本机记账，不跨设备同步";
  t["account.why"] = "需要账号服务，还没有接入";
  t["export.preset"] = kWordPreset;
  t["export.preset.placeholder"] = "请选择一个预设";
  t["export.to"] = "导出至";
  t["export.choose"] = "选择文件夹";
  t["export.folder.suffix"] = "_导出";
  t["export.upload"] = "上传选片交付";
  t["export.advanced"] = "高级设置";
  t["export.location"] = "导出位置";
  t["export.location.default"] = "设为默认导出路径";
  t["export.location.project"] = "新建项目文件夹存储";
  t["export.location.keep"] = "保留导入时的完整路径";
  t["export.format"] = "图片格式";
  t["export.format.default"] = "设为默认格式";
  t["export.quality.group"] = "品质（jpg）";
  t["export.size_limit"] = "文件大小限制";
  t["export.quality"] = "品质";
  t["export.quality.small"] = "小文件";
  t["export.quality.large"] = "大文件";
  t["export.color"] = "色彩空间";
  t["export.srgb"] = "转换为sRGB";
  t["export.overlay"] = "保留叠加层";
  t["export.watermark"] = "水印";
  t["export.cancel"] = "取消";
  t["export.submit"] = kExport;
  t["export.queued"] = "已加入导出列表";
  t["export.refused"] = "以下照片没有加入导出列表";
  t["export.queue.unavailable"] = "导出队列不可用，请稍后重试";
  t["export.why.preset"] = "预设还没有接入导出";
  t["export.why.upload"] = "上传选片交付需要云端服务，还没有接入";
  t["export.wm.sync"] = "水印同步";
  t["export.wm.material"] = "水印";
  t["export.wm.upload"] = "点击上传素材";
  t["export.wm.upload.hint"] = "支持JPG/PNG格式，单张不超10MB";
  t["export.wm.replace"] = "替换";
  t["export.wm.rotate"] = kWordRotate;
  t["export.wm.ccw"] = "逆时针旋转";
  t["export.wm.cw"] = "顺时针旋转";
  t["export.wm.anchor"] = "定位";
  t["export.wm.size"] = "大小";
  t["export.wm.fit"] = "合适";
  t["export.wm.opacity"] = "不透明度";
  t["export.wm.x"] = "水平位置";
  t["export.wm.y"] = "垂直位置";
  t["export.wm.plain"] = "同时导出无水印图片";
  // 问号说明原文（test 版实测）。
  t["export.wm.plain.tip"] = "开启该功能后，导出时将在指定导出路径下创建两个子文件夹，分别包含带水印图片和无水印图片";
  t["export.wm.choose"] = "选择水印素材";
  t["export.wm.need_material"] = "请先上传水印素材，或关闭水印";
  t["export.wm.not_saved"] = "水印素材没有保存：";
  t["export.why.overlay"] = "保留叠加层的作用还没有核实，暂不可用";
  t["export.why.print"] = "排版印刷还没有接入";
  t["export.why.keep_path"] = "保留导入时的完整路径还没有接入";
  t["export.list.empty"] = "暂无导出任务";
  t["export.list.retry"] = "重试";
  t["export.list.cancel"] = "取消任务";
  t["export.list.open"] = "打开所在文件夹";
  return t;
}

std::string page_mark(const std::vector<CopyItem>& marks, std::string_view id) {
  for (const auto& mark : marks) {
    if (mark.id == id) return mark.text;
  }
  return {};
}

Lists build_lists() {
  Lists lists;
  lists["login-code"] = {item("get-code", "获取验证码")};
  lists["login-methods"] = {item("sms", "手机验证码登录"), item("wechat", "微信登录"), item("merchant", "商家账号登录")};
  lists["login-help"] = {item("support", "联系售后"), item("shortcuts", "查看快捷键")};
  // 主页侧栏的“AI 工具箱”带空格（2026-11-04 视觉验收读到的字样），手册写作“AI工具箱”。
  lists["home-nav"] = {item("home", "主页"),          item("toolbox", "AI 工具箱", kToolbox), item("local", "本地项目"),
                       item("cloud", "云空间"),       item("workflow", "工作流"),           item("delivery", "选片交付"),
                       item("trash", "回收站")};
  lists["global"] = {item("support", "技术支持"), item("account", "个人中心"), item("messages", "消息中心"),
                     item("transfers", "传输列表"), item("export-list", "导出列表")};
  lists["home-toolbar"] = {item("membership", "会员续费")};
  for (const auto& entry : lists["global"]) lists["home-toolbar"].push_back(entry);
  lists["library-toolbar"] = lists["home-toolbar"];
  lists["library-toolbar"].push_back(item("export", kExport));
  lists["home-cards"] = {item("capture", kCapture), item("toolbox", kToolbox), item("assistant", kAssistant)};
  lists["page-switch"] = {item("library", kLibrary), item("refine", kRefine), item("creative", kCreative)};
  lists["library-nav"] = {item("home", "首页"), item("search", kSearch)};
  for (const auto& entry : lists["page-switch"]) lists["library-nav"].push_back(entry);
  lists["refine-global"] = lists["global"];
  lists["refine-global"].push_back(item("export", kExport));
  lists["library-status"] = {item("show-raw", "显示RAW格式"), item("show-edit", "显示修图状态"),
                             item("show-export", "显示导出状态")};
  // 缩略图标识的字样与位置在 kernel/workspace 的手册文案里（guide_copy），这里只配编号。
  const auto marks = thumbnail_marks();
  const char* mark_ids[] = {"raw", "exported", "edited", "flag", "star", "color"};
  for (std::size_t index = 0; index < marks.size() && index < std::size(mark_ids); ++index) {
    lists["library-thumbs"].push_back(item(mark_ids[index], marks[index].label));
  }
  const std::string kFlag = page_mark(lists["library-thumbs"], "flag");
  const std::string kStar = page_mark(lists["library-thumbs"], "star");
  const std::string kColorTag = page_mark(lists["library-thumbs"], "color");
  lists["library-side"] = {item("cull", "AI挑图"), item("preset", kPreset), item("meta", "元数据")};
  lists["library-tools"] = {item("import", kImport), item("filter", kFilter), item("sort", kSort)};
  lists["library-views"] = {item("grid", "网格视图"), item("single", kSingle), item("filter-view", "筛选视图")};
  lists["refine-top"] = {item("back", "返回主页面"), item("search", kSearch), item("assistant", kAssistant)};
  lists["refine-tools"] = {item("zoom", kZoom),         item("hand", kHand),      item("fringe", "祛边"),
                           item("guides", "参考线"),    item("liquify", kLiquify), item("spot", "污点修复"),
                           item("patch", "修补"),       item("clone", kClone),     item("remove", "智能消除"),
                           item("point", "AI点修"),     item("reset", "重置效果"), item("preset", kPreset)};
  lists["refine-canvas"] = {item("compare", "查看对比")};
  lists["refine-thumbs"] = {item("add", "+"),        item("filter", kFilter),   item("sort", kSort),
                            item("cull", kCull),     item("star", kStar),       item("flag", kFlag),
                            item("color", kColorTag), item("single", kSingle), item("compare-view", "对比视图"),
                            item("reference", "参考视图")};
  // 精修功能区模块；说明是无障碍说明。“调色”的说明是色彩面板名，由 ui/panels 提供（见 ShellDisplay 之外的壳层）。
  lists["refine-modules"] = {item("ai-color", "AI色彩"),
                             item("color", "调色"),
                             item("local", "局部", {}, {}, "局部调色"),
                             item("portrait", "人像", {}, {}, "人像美化"),
                             item("image", "图像", {}, {}, "图像美化"),
                             item("clothing", "衣物", {}, {}, "衣物美化"),
                             item("pet", "宠物"),
                             item("post", "后调色", {}, {}, "后期调色"),
                             item("crop", "裁剪", {}, {}, "裁剪、旋转"),
                             item("history", "历史", {}, {}, kHistory),
                             item("capture", "联机", {}, {}, kCapture)};
  lists["refine-local"] = {};
  for (const char* label :
       {"所有人", "联动调整", "创建蒙版", "蒙版列表", "全部人物/整个人", "全部皮肤", "面部", "面部皮肤", "脖子",
        "身体皮肤", "眉毛", "眼白", "眼瞳", "嘴唇", "牙齿", "口腔", "耳朵", "头发", "全部衣服", "上衣", "内搭",
        "连体衣（裙）", "下装", "头纱/帽子", "手套", "鞋子", "背包", "首饰", "其他物品", "所有宠物", "全部背景",
        "天空", "草地", "地面", "礁石", "水面", "沙滩", "山", "建筑物", "智能选区", "自由选区", "快速选区",
        "画笔", "线性渐变", "径向渐变", "颜色范围", "亮度范围"}) {
    lists["refine-local"].push_back(item("local-" + std::to_string(lists["refine-local"].size() + 1), label));
  }
  const std::string clothing_prefix = "衣物美化-衣服调整-";
  for (const char* label : {"上衣祛褶皱", "上衣祛褶皱-祛粗褶皱", "上衣祛褶皱-祛细褶皱", "下装祛褶皱", "下装祛褶皱-祛粗褶皱",
                            "下装祛褶皱-祛细褶皱", "衣服祛汗渍", "衣服祛瑕疵", "衣服祛褶皱", "衣服祛褶皱-祛粗褶皱",
                            "衣服祛褶皱-祛细褶皱", "衣服边缘平整", "道具保护"}) {
    auto& clothing = lists["refine-clothing"];
    clothing.push_back(item("clothing-" + std::to_string(clothing.size() + 1), label, {}, clothing_prefix + label));
  }
  lists["refine-crop"] = {};
  for (const char* label : {"裁剪", "原始尺寸", "原照设置", "固定比例", "长*宽*分辨率", "自由尺寸", "证件照尺寸",
                            "我的裁剪预设", "AI智能裁剪", "网格线", "分格数", "几何中心十字线", kWordRotate, "逆时针",
                            "顺时针", "水平翻转", "垂直翻转", "角度滑杆", "AI水平矫正"}) {
    auto& crop = lists["refine-crop"];
    const std::string id = std::string(label) == "角度滑杆" ? "angle" : "crop-" + std::to_string(crop.size() + 1);
    crop.push_back(item(id, label));
  }
  lists["refine-history"] = {item("title", kHistory), item("empty", kNoPhoto)};
  lists["creative-tools"] = {item("zoom", kZoom),
                             item("screen", "更改屏幕模式"),
                             item("hand", kHand),
                             item("move", "移动图层"),
                             item("scale", "缩放工具"),
                             item("liquify", kLiquify),
                             item("spot", "污点修复画笔"),
                             item("patch", "修补工具"),
                             item("clone", kClone),
                             item("marquee", "矩形/椭圆选区", "矩形选区工具&椭圆选区工具"),
                             item("lasso", "套索/选区画笔", "套索工具&选区画笔工具"),
                             item("crop", "裁切")};
  lists["creative-top"] = {item("ai", kAiTools), item("history", "历史"), item("batch", "批量"), item("help", kHelp),
                           item("export", kExport)};
  lists["creative-side"] = {item("ai", kAiTools),         item("history", kHistory), item("batch", "批量设计"),
                            item("help", kHelp),          item("guide", "新手引导"), item("feedback", "反馈")};
  lists["account-actions"] = {item("sheets", "查看剩余张数"), item("packages", "查看已购买套餐"),
                              item("version", "当前软件版本"), item("upgrade", "升级软件"), item("sign-out", "退出登录")};
  // 名字照抄 PixCake 9.3.0 Jarvis 页面图；不要改名、翻译或删减。
  lists["refine-portrait"] = {
      item("portrait-1", "AI祛纹身"),
      item("portrait-2", "下巴-平整"),
      item("portrait-3", "下巴-提亮程度"),
      item("portrait-4", "下巴-程度"),
      item("portrait-5", "全脸丰盈总"),
      item("portrait-6", "全身美型-AI塑形"),
      item("portrait-7", "全身美型-AI液化背景修复"),
      item("portrait-8", "全身美型-AI瘦手臂-右"),
      item("portrait-9", "全身美型-AI瘦手臂-左"),
      item("portrait-10", "全身美型-上身长度"),
      item("portrait-11", "全身美型-丰胸"),
      item("portrait-12", "全身美型-增高"),
      item("portrait-13", "全身美型-天鹅颈-右"),
      item("portrait-14", "全身美型-天鹅颈-左"),
      item("portrait-15", "全身美型-小头"),
      item("portrait-16", "全身美型-瘦手臂"),
      item("portrait-17", "全身美型-瘦脖子-右"),
      item("portrait-18", "全身美型-瘦脖子-左"),
      item("portrait-19", "全身美型-瘦腰"),
      item("portrait-20", "全身美型-瘦腿-大腿"),
      item("portrait-21", "全身美型-瘦腿-小腿"),
      item("portrait-22", "全身美型-瘦身"),
      item("portrait-23", "全身美型-直角肩-右"),
      item("portrait-24", "全身美型-直角肩-左"),
      item("portrait-25", "全身美型-美臀"),
      item("portrait-26", "全身美型-肩膀宽度-右"),
      item("portrait-27", "全身美型-肩膀宽度-左"),
      item("portrait-28", "全身美型-胸位提升"),
      item("portrait-29", "全身美型-脖子长度"),
      item("portrait-30", "全身美型-腰部长度"),
      item("portrait-31", "全身美型-身型流畅"),
      item("portrait-32", "全身美型-长腿"),
      item("portrait-33", "全身美型-长腿-大腿"),
      item("portrait-34", "全身美型-长腿-小腿"),
      item("portrait-35", "嘴角-程度"),
      item("portrait-36", "头发调整-发缝填充"),
      item("portrait-37", "头发调整-发色统一"),
      item("portrait-38", "头发调整-发色选择"),
      item("portrait-39", "头发调整-发色选择-程度"),
      item("portrait-40", "头发调整-发量调整-右"),
      item("portrait-41", "头发调整-发量调整-左"),
      item("portrait-42", "头发调整-头发光泽"),
      item("portrait-43", "头发调整-头发柔顺"),
      item("portrait-44", "头发调整-头发柔顺-发丝调整"),
      item("portrait-45", "头发调整-祛白发"),
      item("portrait-46", "头发调整-祛碎发"),
      item("portrait-47", "头发调整-祛碎发-人物区域"),
      item("portrait-48", "头发调整-祛碎发-背景区域"),
      item("portrait-49", "头发调整-颅顶调整"),
      item("portrait-50", "头发调整-颅顶调整（头顶）"),
      item("portrait-51", "妆容减淡-修容总"),
      item("portrait-52", "妆容减淡-修容阴影"),
      item("portrait-53", "妆容减淡-修容高光"),
      item("portrait-54", "妆容减淡-全脸开关"),
      item("portrait-55", "妆容减淡-全脸祛妆"),
      item("portrait-56", "妆容减淡-卧蚕"),
      item("portrait-57", "妆容减淡-口红"),
      item("portrait-58", "妆容减淡-开关"),
      item("portrait-59", "妆容减淡-眉毛"),
      item("portrait-60", "妆容减淡-眼妆总"),
      item("portrait-61", "妆容减淡-眼影"),
      item("portrait-62", "妆容减淡-眼线"),
      item("portrait-63", "妆容减淡-睫毛"),
      item("portrait-64", "妆容减淡-美瞳"),
      item("portrait-65", "妆容减淡-腮红"),
      item("portrait-66", "妆容减淡-面部装饰"),
      item("portrait-67", "妆容调整-主题妆容"),
      item("portrait-68", "妆容调整-主题妆容-素材路径"),
      item("portrait-69", "妆容调整-修容"),
      item("portrait-70", "妆容调整-修容-素材路径"),
      item("portrait-71", "妆容调整-卧蚕"),
      item("portrait-72", "妆容调整-卧蚕-素材路径"),
      item("portrait-73", "妆容调整-卧蚕上下-右"),
      item("portrait-74", "妆容调整-卧蚕上下-左"),
      item("portrait-75", "妆容调整-卧蚕大小-右"),
      item("portrait-76", "妆容调整-卧蚕大小-左"),
      item("portrait-77", "妆容调整-卧蚕左右-右"),
      item("portrait-78", "妆容调整-卧蚕左右-左"),
      item("portrait-79", "妆容调整-口红"),
      item("portrait-80", "妆容调整-口红-素材路径"),
      item("portrait-81", "妆容调整-口红-颜色"),
      item("portrait-82", "妆容调整-唇妆增强"),
      item("portrait-83", "妆容调整-唇妆增强-明度"),
      item("portrait-84", "妆容调整-唇妆增强-立体度"),
      item("portrait-85", "妆容调整-唇妆增强-饱和度"),
      item("portrait-86", "妆容调整-眉毛-右"),
      item("portrait-87", "妆容调整-眉毛-左"),
      item("portrait-88", "妆容调整-眉毛-素材路径"),
      item("portrait-89", "妆容调整-眉毛-颜色"),
      item("portrait-90", "妆容调整-眉毛增强-右"),
      item("portrait-91", "妆容调整-眉毛增强-左"),
      item("portrait-92", "妆容调整-眼妆"),
      item("portrait-93", "妆容调整-眼妆增强-右"),
      item("portrait-94", "妆容调整-眼妆增强-左"),
      item("portrait-95", "妆容调整-眼妆增强-明度-右"),
      item("portrait-96", "妆容调整-眼妆增强-明度-左"),
      item("portrait-97", "妆容调整-眼妆增强-立体度-右"),
      item("portrait-98", "妆容调整-眼妆增强-立体度-左"),
      item("portrait-99", "妆容调整-眼妆增强-饱和度-右"),
      item("portrait-100", "妆容调整-眼妆增强-饱和度-左"),
      item("portrait-101", "妆容调整-眼睛-素材路径"),
      item("portrait-102", "妆容调整-眼线-右"),
      item("portrait-103", "妆容调整-眼线-左"),
      item("portrait-104", "妆容调整-眼线-素材路径"),
      item("portrait-105", "妆容调整-眼线-颜色"),
      item("portrait-106", "妆容调整-睫毛-右上"),
      item("portrait-107", "妆容调整-睫毛-右下"),
      item("portrait-108", "妆容调整-睫毛-左上"),
      item("portrait-109", "妆容调整-睫毛-左下"),
      item("portrait-110", "妆容调整-睫毛-素材路径"),
      item("portrait-111", "妆容调整-睫毛-颜色"),
      item("portrait-112", "妆容调整-美瞳"),
      item("portrait-113", "妆容调整-美瞳-素材路径"),
      item("portrait-114", "妆容调整-脸部装饰-素材路径"),
      item("portrait-115", "妆容调整-腮红"),
      item("portrait-116", "妆容调整-腮红-素材路径"),
      item("portrait-117", "妆容调整-腮红-颜色"),
      item("portrait-118", "妆容调整-阴影立体"),
      item("portrait-119", "妆容调整-雀斑妆程度"),
      item("portrait-120", "妆容调整-高光立体"),
      item("portrait-121", "手部中性灰"),
      item("portrait-122", "手部美化-祛青筋"),
      item("portrait-123", "智能调色"),
      item("portrait-124", "牙齿美化-牙齿修复"),
      item("portrait-125", "牙齿美化-牙齿排齐"),
      item("portrait-126", "牙齿美化-牙齿祛瑕疵"),
      item("portrait-127", "牙齿美化-牙齿美白"),
      item("portrait-128", "牙齿美化-牙齿美白-明度"),
      item("portrait-129", "牙齿美化-牙齿美白-祛黄"),
      item("portrait-130", "牙齿美化-边缘补齐"),
      item("portrait-131", "皮肤调整-中性灰平整"),
      item("portrait-132", "皮肤调整-中性灰立体"),
      item("portrait-133", "皮肤调整-五官"),
      item("portrait-134", "皮肤调整-光泽度"),
      item("portrait-135", "皮肤调整-水润磨皮（脸部）"),
      item("portrait-136", "皮肤调整-水润磨皮（身体）"),
      item("portrait-137", "皮肤调整-皮肤明度"),
      item("portrait-138", "皮肤调整-皮肤质感"),
      item("portrait-139", "皮肤调整-皮肤质感-清晰度"),
      item("portrait-140", "皮肤调整-皮肤颜色-AI多人肤色统一"),
      item("portrait-141", "皮肤调整-皮肤颜色-AI肤色统一（脸部）"),
      item("portrait-142", "皮肤调整-皮肤颜色-AI肤色统一（身体）"),
      item("portrait-143", "皮肤调整-皮肤颜色-皮肤红润"),
      item("portrait-144", "皮肤调整-皮肤颜色-肤色统一（身体）"),
      item("portrait-145", "皮肤调整-皮肤颜色-肤色美白"),
      item("portrait-146", "皮肤调整-皮肤颜色-肤色透亮"),
      item("portrait-147", "皮肤调整-纹理度"),
      item("portrait-148", "皮肤调整-肤色统一调整吸管"),
      item("portrait-149", "皮肤调整-肤色统一调整吸管x坐标"),
      item("portrait-150", "皮肤调整-肤色统一调整吸管颜色"),
      item("portrait-151", "皮肤调整-肤色色温"),
      item("portrait-152", "皮肤调整-肤色色调"),
      item("portrait-153", "皮肤调整-肤色选择"),
      item("portrait-154", "皮肤调整-肤色选择-程度"),
      item("portrait-155", "皮肤调整-质感磨皮"),
      item("portrait-156", "皮肤调整-轮廓"),
      item("portrait-157", "皮肤调整-锁骨增强"),
      item("portrait-158", "皮肤调整-高低频磨皮（脸部）"),
      item("portrait-159", "皮肤调整-高低频磨皮（脸部）-低频平整（脸部）"),
      item("portrait-160", "皮肤调整-高低频磨皮（脸部）-高频增强（脸部）"),
      item("portrait-161", "皮肤调整-高低频磨皮（身体）"),
      item("portrait-162", "皮肤调整-高低频磨皮（身体）-低频平整（身体）"),
      item("portrait-163", "皮肤调整-高低频磨皮（身体）-高频增强（身体）"),
      item("portrait-164", "眉弓-程度"),
      item("portrait-165", "眼头调整开关"),
      item("portrait-166", "眼睛增强-亮眼-右"),
      item("portrait-167", "眼睛增强-亮眼-左"),
      item("portrait-168", "眼睛增强-亮眼-眼白-右"),
      item("portrait-169", "眼睛增强-亮眼-眼白-左"),
      item("portrait-170", "眼睛增强-亮眼-眼睛高光-右"),
      item("portrait-171", "眼睛增强-亮眼-眼睛高光-左"),
      item("portrait-172", "眼睛增强-亮眼-眼瞳-右"),
      item("portrait-173", "眼睛增强-亮眼-眼瞳-左"),
      item("portrait-174", "眼睛增强-双眼皮"),
      item("portrait-175", "眼睛增强-右眼垂直移动"),
      item("portrait-176", "眼睛增强-右眼水平移动"),
      item("portrait-177", "眼睛增强-右眼缩放"),
      item("portrait-178", "眼睛增强-大小对称"),
      item("portrait-179", "眼睛增强-左眼垂直移动"),
      item("portrait-180", "眼睛增强-左眼水平移动"),
      item("portrait-181", "眼睛增强-左眼缩放"),
      item("portrait-182", "眼睛增强-水平对称"),
      item("portrait-183", "眼睛增强-眼白净化"),
      item("portrait-184", "眼睛增强-眼睛对称"),
      item("portrait-185", "眼睛增强-眼神光-垂直位置-右"),
      item("portrait-186", "眼睛增强-眼神光-垂直位置-左"),
      item("portrait-187", "眼睛增强-眼神光-旋转-右"),
      item("portrait-188", "眼睛增强-眼神光-旋转-左"),
      item("portrait-189", "眼睛增强-眼神光-水平位置-右"),
      item("portrait-190", "眼睛增强-眼神光-水平位置-左"),
      item("portrait-191", "眼睛增强-眼神光-水平翻转-右"),
      item("portrait-192", "眼睛增强-眼神光-水平翻转-左"),
      item("portrait-193", "眼睛增强-眼神光-程度-右"),
      item("portrait-194", "眼睛增强-眼神光-程度-左"),
      item("portrait-195", "眼睛增强-眼神光-素材路径"),
      item("portrait-196", "眼睛增强-眼镜祛反光"),
      item("portrait-197", "眼睛增强-祛双眼皮贴-右"),
      item("portrait-198", "眼睛增强-祛双眼皮贴-左"),
      item("portrait-199", "眼睛增强-祛红眼"),
      item("portrait-200", "眼睛增强-祛红血丝"),
      item("portrait-201", "眼睛增强-程度-右"),
      item("portrait-202", "眼睛增强-程度-左"),
      item("portrait-203", "眼睛增强-自动识别/手动选择（参考眼）"),
      item("portrait-204", "眼睛增强-虹膜自动直视开关"),
      item("portrait-205", "眼睛增强-虹膜自动矫正开关"),
      item("portrait-206", "眼睛增强-长度-右"),
      item("portrait-207", "眼睛增强-长度-左"),
      item("portrait-208", "眼睛增强-长度-眼头-右"),
      item("portrait-209", "眼睛增强-长度-眼头-左"),
      item("portrait-210", "眼睛增强-长度-眼尾-右"),
      item("portrait-211", "眼睛增强-长度-眼尾-左"),
      item("portrait-212", "眼睛增强-高度-右"),
      item("portrait-213", "眼睛增强-高度-左"),
      item("portrait-214", "眼睛增强-高度-眼头-右"),
      item("portrait-215", "眼睛增强-高度-眼头-左"),
      item("portrait-216", "眼睛增强-高度-眼尾-右"),
      item("portrait-217", "眼睛增强-高度-眼尾-左"),
      item("portrait-218", "眼窝-右"),
      item("portrait-219", "眼窝-左"),
      item("portrait-220", "祛除瑕疵-下颌线增强"),
      item("portrait-221", "祛除瑕疵-卧蚕保留"),
      item("portrait-222", "祛除瑕疵-唇纹修整"),
      item("portrait-223", "祛除瑕疵-妆容保护"),
      item("portrait-224", "祛除瑕疵-祛副乳"),
      item("portrait-225", "祛除瑕疵-祛双下巴"),
      item("portrait-226", "祛除瑕疵-祛双下巴-祛除程度"),
      item("portrait-227", "祛除瑕疵-祛双下巴-阴影程度"),
      item("portrait-228", "祛除瑕疵-祛嘴周纹"),
      item("portrait-229", "祛除瑕疵-祛妊娠纹"),
      item("portrait-230", "祛除瑕疵-祛妊娠纹-祛妊娠线"),
      item("portrait-231", "祛除瑕疵-祛川字纹"),
      item("portrait-232", "祛除瑕疵-祛抬头纹"),
      item("portrait-233", "祛除瑕疵-祛斑祛痘"),
      item("portrait-234", "祛除瑕疵-祛斑祛痘-祛斑"),
      item("portrait-235", "祛除瑕疵-祛斑祛痘-祛痘"),
      item("portrait-236", "祛除瑕疵-祛斑祛痘-质感保留"),
      item("portrait-237", "祛除瑕疵-祛木偶纹"),
      item("portrait-238", "祛除瑕疵-祛汗珠（脸部）"),
      item("portrait-239", "祛除瑕疵-祛油光（脸部）"),
      item("portrait-240", "祛除瑕疵-祛法令纹-右"),
      item("portrait-241", "祛除瑕疵-祛法令纹-左"),
      item("portrait-242", "祛除瑕疵-祛生长纹"),
      item("portrait-243", "祛除瑕疵-祛痣"),
      item("portrait-244", "祛除瑕疵-祛眼周纹"),
      item("portrait-245", "祛除瑕疵-祛眼袋"),
      item("portrait-246", "祛除瑕疵-祛胡须"),
      item("portrait-247", "祛除瑕疵-祛胡须-祛胡渣"),
      item("portrait-248", "祛除瑕疵-祛胡须-祛胡青"),
      item("portrait-249", "祛除瑕疵-祛脸颊纹"),
      item("portrait-250", "祛除瑕疵-祛颈纹"),
      item("portrait-251", "祛除瑕疵-祛黑眼圈"),
      item("portrait-252", "祛除瑕疵-祛鼻孔瑕疵"),
      item("portrait-253", "祛除瑕疵-祛鼻背纹"),
      item("portrait-254", "祛除瑕疵-胡型保护"),
      item("portrait-255", "祛除瑕疵-身体祛瑕疵"),
      item("portrait-256", "祛除瑕疵-身体祛瑕疵（婴幼儿）"),
      item("portrait-257", "脚部美化-祛青筋"),
      item("portrait-258", "表情管理-微笑嘴角"),
      item("portrait-259", "表情管理-笑容管理"),
      item("portrait-260", "表情管理-笑容管理"),
      item("portrait-261", "表情管理-笑容管理"),
      item("portrait-262", "面中-平整程度"),
      item("portrait-263", "面中-提亮程度"),
      item("portrait-264", "面中-程度"),
      item("portrait-265", "面部丰盈"),
      item("portrait-266", "面部重塑-3D塑颜-上下"),
      item("portrait-267", "面部重塑-3D塑颜-倾斜"),
      item("portrait-268", "面部重塑-3D塑颜-左右"),
      item("portrait-269", "面部重塑-上半身对称"),
      item("portrait-270", "面部重塑-五官对称"),
      item("portrait-271", "面部重塑-全脸提拉总"),
      item("portrait-272", "面部重塑-嘴巴-M唇"),
      item("portrait-273", "面部重塑-嘴巴-上下"),
      item("portrait-274", "面部重塑-嘴巴-丰上唇"),
      item("portrait-275", "面部重塑-嘴巴-丰下唇"),
      item("portrait-276", "面部重塑-嘴巴-倾斜"),
      item("portrait-277", "面部重塑-嘴巴-嘴宽"),
      item("portrait-278", "面部重塑-嘴巴-大小"),
      item("portrait-279", "面部重塑-嘴巴-左右"),
      item("portrait-280", "面部重塑-嘴巴-露龈调整-右"),
      item("portrait-281", "面部重塑-嘴巴-露龈调整-左"),
      item("portrait-282", "面部重塑-对称液化"),
      item("portrait-283", "面部重塑-眉毛-位置"),
      item("portrait-284", "面部重塑-眉毛-位置-右"),
      item("portrait-285", "面部重塑-眉毛-位置-左"),
      item("portrait-286", "面部重塑-眉毛-倾斜"),
      item("portrait-287", "面部重塑-眉毛-倾斜-右"),
      item("portrait-288", "面部重塑-眉毛-倾斜-左"),
      item("portrait-289", "面部重塑-眉毛-形状"),
      item("portrait-290", "面部重塑-眉毛-形状-右"),
      item("portrait-291", "面部重塑-眉毛-形状-左"),
      item("portrait-292", "面部重塑-眉毛-眉头长短-右"),
      item("portrait-293", "面部重塑-眉毛-眉头长短-左"),
      item("portrait-294", "面部重塑-眉毛-眉尾长短-右"),
      item("portrait-295", "面部重塑-眉毛-眉尾长短-左"),
      item("portrait-296", "面部重塑-眉毛-眉距"),
      item("portrait-297", "面部重塑-眉毛-眉距-右"),
      item("portrait-298", "面部重塑-眉毛-眉距-左"),
      item("portrait-299", "面部重塑-眉毛-粗细"),
      item("portrait-300", "面部重塑-眉毛-粗细-右"),
      item("portrait-301", "面部重塑-眉毛-粗细-左"),
      item("portrait-302", "面部重塑-眼睛-位置"),
      item("portrait-303", "面部重塑-眼睛-位置-右"),
      item("portrait-304", "面部重塑-眼睛-位置-左"),
      item("portrait-305", "面部重塑-眼睛-倾斜"),
      item("portrait-306", "面部重塑-眼睛-倾斜-右"),
      item("portrait-307", "面部重塑-眼睛-倾斜-左"),
      item("portrait-308", "面部重塑-眼睛-内眼角-右"),
      item("portrait-309", "面部重塑-眼睛-内眼角-左"),
      item("portrait-310", "面部重塑-眼睛-外眼角-右"),
      item("portrait-311", "面部重塑-眼睛-外眼角-左"),
      item("portrait-312", "面部重塑-眼睛-整体大小-右"),
      item("portrait-313", "面部重塑-眼睛-整体大小-左"),
      item("portrait-314", "面部重塑-眼睛-眼宽"),
      item("portrait-315", "面部重塑-眼睛-眼宽-右"),
      item("portrait-316", "面部重塑-眼睛-眼宽-左"),
      item("portrait-317", "面部重塑-眼睛-眼睑下至-右"),
      item("portrait-318", "面部重塑-眼睛-眼睑下至-左"),
      item("portrait-319", "面部重塑-眼睛-眼瞳大小"),
      item("portrait-320", "面部重塑-眼睛-眼瞳大小-右"),
      item("portrait-321", "面部重塑-眼睛-眼瞳大小-左"),
      item("portrait-322", "面部重塑-眼睛-眼距"),
      item("portrait-323", "面部重塑-眼睛-眼距-右"),
      item("portrait-324", "面部重塑-眼睛-眼距-左"),
      item("portrait-325", "面部重塑-眼睛-眼高"),
      item("portrait-326", "面部重塑-眼睛-眼高-右"),
      item("portrait-327", "面部重塑-眼睛-眼高-左"),
      item("portrait-328", "面部重塑-脸型-AI面部流畅"),
      item("portrait-329", "面部重塑-脸型-V脸-右"),
      item("portrait-330", "面部重塑-脸型-V脸-左"),
      item("portrait-331", "面部重塑-脸型-下巴长短"),
      item("portrait-332", "面部重塑-脸型-下庭"),
      item("portrait-333", "面部重塑-脸型-下颌"),
      item("portrait-334", "面部重塑-脸型-下颌-右"),
      item("portrait-335", "面部重塑-脸型-下颌-左"),
      item("portrait-336", "面部重塑-脸型-中庭"),
      item("portrait-337", "面部重塑-脸型-人中"),
      item("portrait-338", "面部重塑-脸型-发际线"),
      item("portrait-339", "面部重塑-脸型-太阳穴"),
      item("portrait-340", "面部重塑-脸型-太阳穴-右"),
      item("portrait-341", "面部重塑-脸型-太阳穴-左"),
      item("portrait-342", "面部重塑-脸型-小脸"),
      item("portrait-343", "面部重塑-脸型-尖下巴-右"),
      item("portrait-344", "面部重塑-脸型-尖下巴-左"),
      item("portrait-345", "面部重塑-脸型-收下巴"),
      item("portrait-346", "面部重塑-脸型-瘦脸"),
      item("portrait-347", "面部重塑-脸型-瘦脸-右"),
      item("portrait-348", "面部重塑-脸型-瘦脸-左"),
      item("portrait-349", "面部重塑-脸型-短脸"),
      item("portrait-350", "面部重塑-脸型-脸宽"),
      item("portrait-351", "面部重塑-脸型-脸宽-右"),
      item("portrait-352", "面部重塑-脸型-脸宽-左"),
      item("portrait-353", "面部重塑-脸型-颌面矫正"),
      item("portrait-354", "面部重塑-脸型-颌面矫正-光影矫正"),
      item("portrait-355", "面部重塑-脸型-颌面矫正-轮廓塑形"),
      item("portrait-356", "面部重塑-脸型-额头宽度-右"),
      item("portrait-357", "面部重塑-脸型-额头宽度-左"),
      item("portrait-358", "面部重塑-脸型-额头高度-右"),
      item("portrait-359", "面部重塑-脸型-额头高度-左"),
      item("portrait-360", "面部重塑-脸型-颧骨"),
      item("portrait-361", "面部重塑-脸型-颧骨-右"),
      item("portrait-362", "面部重塑-脸型-颧骨-左"),
      item("portrait-363", "面部重塑-脸型预设"),
      item("portrait-364", "面部重塑-脸型预设-程度"),
      item("portrait-365", "面部重塑-面部提拉-口周-平整程度"),
      item("portrait-366", "面部重塑-面部提拉-口周-液化程度"),
      item("portrait-367", "面部重塑-面部提拉-口周-程度"),
      item("portrait-368", "面部重塑-面部提拉-眼周-平整程度"),
      item("portrait-369", "面部重塑-面部提拉-眼周-液化程度"),
      item("portrait-370", "面部重塑-面部提拉-眼周-程度"),
      item("portrait-371", "面部重塑-面部提拉-轮廓-程度"),
      item("portrait-372", "面部重塑-面部提拉-面中-平整程度"),
      item("portrait-373", "面部重塑-面部提拉-面中-液化程度"),
      item("portrait-374", "面部重塑-面部提拉-面中-程度"),
      item("portrait-375", "面部重塑-鼻子-大小"),
      item("portrait-376", "面部重塑-鼻子-左右"),
      item("portrait-377", "面部重塑-鼻子-鼻尖"),
      item("portrait-378", "面部重塑-鼻子-鼻梁"),
      item("portrait-379", "面部重塑-鼻子-鼻翼"),
      item("portrait-380", "面部重塑-鼻子-鼻高"),
      item("portrait-381", "面颊-程度"),
      item("portrait-382", "额头-平整程度"),
      item("portrait-383", "额头-提亮程度"),
      item("portrait-384", "额头-程度"),
      item("portrait-385", "鼻基底-平整程度"),
      item("portrait-386", "鼻基底-提亮程度"),
      item("portrait-387", "鼻基底-程度"),
  };
  // 名字照抄 PixCake 9.3.0 Jarvis 页面图；不要改名、翻译或删减。
  lists["refine-image"] = {
      item("image-1", "图像美化-AI保留阴影-阴影混合模式"),
      item("image-2", "图像美化-AI保留阴影-阴影混合模式(增加阴影)"),
      item("image-3", "图像美化-去黑白边开关"),
      item("image-4", "图像美化-去黑白边强度"),
      item("image-5", "图像美化-换天空-人景调整-人物色彩（偏向天空）"),
      item("image-6", "图像美化-换天空-人景调整-景物色彩（偏向天空）"),
      item("image-7", "图像美化-换天空-地平线位置"),
      item("image-8", "图像美化-换天空-天空素材路径"),
      item("image-9", "图像美化-换天空-天空调整-不透明度"),
      item("image-10", "图像美化-换天空-天空调整-亮度"),
      item("image-11", "图像美化-换天空-天空调整-垂直位置"),
      item("image-12", "图像美化-换天空-天空调整-垂直翻转"),
      item("image-13", "图像美化-换天空-天空调整-旋转档位"),
      item("image-14", "图像美化-换天空-天空调整-旋转角度"),
      item("image-15", "图像美化-换天空-天空调整-水平位置"),
      item("image-16", "图像美化-换天空-天空调整-水平翻转"),
      item("image-17", "图像美化-换天空-天空调整-渐变"),
      item("image-18", "图像美化-换天空-天空调整-色温"),
      item("image-19", "图像美化-换天空-天空调整-色调"),
      item("image-20", "图像美化-换天空-天空调整-虚化"),
      item("image-21", "图像美化-换天空-天空调整-边缘过渡"),
      item("image-22", "图像美化-换天空-天空调整-饱和度"),
      item("image-23", "图像美化-换天空-水面调整-景物反射"),
      item("image-24", "图像美化-换天空-水面调整-景物水效虚化"),
      item("image-25", "图像美化-换背景绿幕祛反光"),
      item("image-26", "图像美化-换背景阴影右锚点垂直位移"),
      item("image-27", "图像美化-换背景阴影右锚点水平位移"),
      item("image-28", "图像美化-换背景阴影左锚点垂直位移"),
      item("image-29", "图像美化-换背景阴影左锚点水平位移"),
      item("image-30", "图像美化-纯色背景祛色彩断层"),
      item("image-31", "图像美化-背景调整-AI保留阴影"),
      item("image-32", "图像美化-背景调整-纯色背景光影均匀"),
      item("image-33", "图像美化-背景调整-纯色背景光影均匀-明度"),
      item("image-34", "图像美化-背景调整-纯色背景光影均匀-程度"),
      item("image-35", "图像美化-背景调整-纯色背景祛瑕疵"),
      item("image-36", "图像美化-背景调整-纯色背景祛瑕疵-祛瑕疵程度"),
      item("image-37", "图像美化-背景调整-纯色背景祛瑕疵-祛瑕疵程度-地面"),
      item("image-38", "图像美化-背景调整-纯色背景祛瑕疵-祛瑕疵程度-背景"),
      item("image-39", "图像美化-背景调整-纯色背景祛穿帮"),
      item("image-40", "图像美化-背景调整-背景增强"),
      item("image-41", "图像美化-背景调整-背景布祛褶皱-祛粗褶皱"),
      item("image-42", "图像美化-背景调整-背景布祛褶皱-祛细褶皱"),
      item("image-43", "图像美化-背景调整-背景布祛褶皱-程度"),
      item("image-44", "图像美化-证件照不透明度"),
      item("image-45", "图像美化-证件照垂直位置"),
      item("image-46", "图像美化-证件照填充模式"),
      item("image-47", "图像美化-证件照大小"),
      item("image-48", "图像美化-证件照换背景"),
      item("image-49", "图像美化-证件照换背景-不透明度"),
      item("image-50", "图像美化-证件照换背景-增加阴影"),
      item("image-51", "图像美化-证件照换背景-抠像模式"),
      item("image-52", "图像美化-证件照换背景-抠图保留范围"),
      item("image-53", "图像美化-证件照换背景-推荐-AI保留阴影-程度"),
      item("image-54", "图像美化-证件照换背景-渐变"),
      item("image-55", "图像美化-证件照换背景-素材路径"),
      item("image-56", "图像美化-证件照换背景-背景混合模式"),
      item("image-57", "图像美化-证件照换背景-自定义颜色"),
      item("image-58", "图像美化-证件照换背景-虚化范围"),
      item("image-59", "图像美化-证件照换背景-阴影3D垂直位移"),
      item("image-60", "图像美化-证件照换背景-阴影3D水平位移"),
      item("image-61", "图像美化-证件照换背景-阴影垂直位移"),
      item("image-62", "图像美化-证件照换背景-阴影水平位移"),
      item("image-63", "图像美化-证件照换背景-阴影虚化"),
      item("image-64", "图像美化-证件照水平位置"),
  };
  // 名字照抄 PixCake 9.3.0 Jarvis 页面图；不要改名、翻译或删减。
  lists["refine-pet"] = {
      item("pet-1", "宠物-眼睛增强-眼神光-垂直位置-右"),
      item("pet-2", "宠物-眼睛增强-眼神光-垂直位置-左"),
      item("pet-3", "宠物-眼睛增强-眼神光-大小-右"),
      item("pet-4", "宠物-眼睛增强-眼神光-大小-左"),
      item("pet-5", "宠物-眼睛增强-眼神光-旋转-右"),
      item("pet-6", "宠物-眼睛增强-眼神光-旋转-左"),
      item("pet-7", "宠物-眼睛增强-眼神光-水平位置-右"),
      item("pet-8", "宠物-眼睛增强-眼神光-水平位置-左"),
      item("pet-9", "宠物-眼睛增强-眼神光-水平翻转-右"),
      item("pet-10", "宠物-眼睛增强-眼神光-水平翻转-左"),
      item("pet-11", "宠物-眼睛增强-眼神光-程度-右"),
      item("pet-12", "宠物-眼睛增强-眼神光-程度-左"),
      item("pet-13", "宠物-眼睛增强-眼神光-素材路径"),
      item("pet-14", "宠物美化-眼睛-亮眼-右"),
      item("pet-15", "宠物美化-眼睛-亮眼-左"),
      item("pet-16", "宠物美化-眼睛-虹膜增强-右"),
      item("pet-17", "宠物美化-眼睛-虹膜增强-左"),
      item("pet-18", "宠物美化-身型-祛碎毛"),
  };
  lists["export-tabs"] = {item("quick", "快速导出"), item("custom", "自定设置"), item("print", "排版印刷")};
  lists["export-destinations"] = {item("folder", "指定文件夹"), item("original", "原始照片所在的文件夹")};
  // 原始格式、JPG、TIFF8位、TIFF16位；编号与 kernel/exportq 设置快照里的格式名一致。
  lists["export-formats"] = {item("original", "原始格式"), item("jpg", "JPG"), item("tiff", "TIFF8位"),
                             item("tiff16", "TIFF16位")};
  lists["export-quality-levels"] = {item("low", "低"), item("medium", "中"), item("high", "高"), item("best", "最佳")};
  // 水印页签：编号与 kernel/exportq 的 WatermarkShape 顺序一致（横、竖、方）。
  lists["export-wm-shapes"] = {item("landscape", "横图"), item("portrait", "竖图"), item("square", "方图")};
  // 九宫格，按行从左上到右下（WatermarkPlacement::anchor 0..8）。
  lists["export-wm-anchors"] = {item("0", "左上"), item("1", "上中"), item("2", "右上"), item("3", "左中"),
                                item("4", "居中"), item("5", "右中"), item("6", "左下"), item("7", "下中"),
                                item("8", "右下")};
  lists["export-list-columns"] = {item("file", "文件名"), item("status", "状态"), item("reason", "失败原因"),
                                  item("charge", "计费"), item("destination", "导出位置")};
  return lists;
}

const Texts& texts() {
  static const Texts value = build_texts();
  return value;
}

const Lists& lists() {
  static const Lists value = build_lists();
  return value;
}

std::set<std::string>& missing() {
  static std::set<std::string> value;
  return value;
}

}  // namespace

const std::string& page_text(std::string_view key) {
  static const std::string empty;
  const auto found = texts().find(key);
  if (found != texts().end()) return found->second;
  missing().insert(std::string(key));
  return empty;
}

const std::vector<CopyItem>& page_list(std::string_view key) {
  static const std::vector<CopyItem> empty;
  const auto found = lists().find(key);
  if (found != lists().end()) return found->second;
  missing().insert("list:" + std::string(key));
  return empty;
}

const CopyItem& page_item(std::string_view list, std::string_view id) {
  static const CopyItem empty;
  for (const auto& entry : page_list(list)) {
    if (entry.id == id) return entry;
  }
  missing().insert("item:" + std::string(list) + "/" + std::string(id));
  return empty;
}

std::vector<std::string> missing_page_copy() { return {missing().begin(), missing().end()}; }

}  // namespace pixcake::ui::shell
