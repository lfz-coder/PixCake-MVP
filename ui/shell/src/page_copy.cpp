#include "pixcake/ui/shell/page_copy.hpp"

#include "pixcake/ui/shell/shared_words.hpp"
#include "pixcake/workspace/guide_copy.hpp"

#include <iterator>
#include <map>
#include <set>
#include <utility>

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
  t["widget.close"] = "关闭";
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
  // 精修右侧：图标栏选模块，面板一次只显示一个模块。色彩调节页顶部是直方图卡片：下面一行平时是相机参数
  // （非 RAW 没有，显示“ISO -”“- mm”“F/-”“-秒”和格式），鼠标在画面上时换成指针下像素的 R、G、B。
  t["refine.rail"] = "功能模块";
  t["refine.module.pending"] = "该模块暂未接入";
  t["refine.master-color"] = "大师级色彩";
  t["refine.master-image"] = "大师级美化";
  t["refine.ai-calibrate"] = "AI 校准";
  t["refine.color-unify"] = "多图色彩统一";
  t["refine.wb.preset"] = kWordWhiteBalance;
  t["refine.wb.custom"] = "自定义";
  t["refine.exif.iso"] = "ISO %1";
  t["refine.exif.focal"] = "%1 mm";
  t["refine.exif.aperture"] = "F/%1";
  t["refine.exif.shutter"] = "%1秒";
  t["refine.exif.none"] = "-";
  t["refine.readout.name"] = "指针下像素的 RGB 读数";
  t["refine.section.toggle"] = "展开或收起%1";
  t["refine.section.reset"] = "还原本组";
  t["refine.side.left"] = "左";
  t["refine.side.right"] = "右";
  t["refine.side.link"] = "左右联动";
  t["refine.card.none"] = "无";
  t["refine.portrait.scan"] = "人脸识别";
  t["refine.history.empty.name"] = "暂无已加载照片。关闭软件后再次打开时，先前步骤合并为上一次保存。当前不列出步骤。";
  t["guides.create-horizontal"] = "创建水平参考线";
  t["guides.create-vertical"] = "创建垂直参考线";
  t["guides.hide"] = "隐藏参考线";
  t["guides.show"] = "显示参考线";
  t["guides.remove"] = "删除当前参考线";
  t["guides.remove-all"] = "删除全部参考线";

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
  // 预设（需求 §9）：本机“我的”预设；官方的“推荐”预设没有素材，不做。
  t["preset.title"] = "我的预设";
  t["preset.empty"] = "还没有预设：调好一张图，点“保存新预设”";
  t["preset.save"] = "保存新预设";
  t["preset.apply"] = "套用";
  t["preset.rename"] = "重命名";
  t["preset.delete"] = "删除";
  t["preset.rename.title"] = "重命名预设";
  t["preset.rename.label"] = "名称（不超过 20 字）";
  t["preset.tooltip"] = "分类：%1　类型：%2";
  t["preset.overwrite.confirm"] = "预设“%1”会替换这张图已有的色彩调整，确认套用？";
  t["preset.delete.confirm"] = "确认删除预设“%1”？";
  t["preset.save.title"] = "保存新预设";
  t["preset.save.name"] = "名称（不超过 20 字）";
  t["preset.save.category"] = "分类";
  t["preset.save.category.default"] = "默认";
  t["preset.save.type"] = "类型";
  t["preset.save.type.auto"] = "自动判断";
  t["preset.save.overwrite"] = "覆盖旧预设";
  t["preset.save.overwrite.none"] = "不覆盖";
  t["preset.save.adjusted"] = "已调整";
  t["preset.save.all"] = "全选";
  t["preset.save.cleared"] = "全部取消";
  t["preset.save.nothing"] = "这张图还没有可以存进预设的调整";
  t["preset.save.failed"] = "没有保存：%1";
  t["history.preset"] = "预设：%1";
  // 复制、粘贴效果和批量同步（需求 §18）。
  t["sync.title"] = "同步到选中图片";
  t["sync.hint"] = "把当前图片的效果同步到选中的其他图片。可以只勾选二级功能；不能同步的项目已禁用。";
  t["sync.blocked"] = "%1（%2）";
  t["sync.targets"] = "已选 %1 张";
  t["sync.all"] = "全选";
  t["sync.none"] = "全部取消";
  t["sync.save"] = "保存设置";
  t["sync.none.checked"] = "还没有勾选要同步的功能";
  t["sync.failed"] = "有 %1 张没有同步成功";
  t["effect.copy"] = "复制效果";
  t["effect.paste"] = "粘贴效果";
  t["history.paste"] = "粘贴效果";
  // 局部调色的蒙版列表（官方的“蒙版列表”：每个蒙版有自己的子项和一组滑杆）。
  t["local.mask.name"] = "自定义-%1";
  t["local.mask.new"] = "创建蒙版";
  t["local.common"] = "常用蒙版";
  t["local.manage"] = "管理";
  t["local.linked"] = "联动调整";
  t["local.mask.list"] = "蒙版列表";
  t["local.mask.create-new"] = "创建新蒙版";
  t["local.mask.edit"] = "编辑蒙版";
  t["local.mask.whole"] = "全图";
  t["local.part.pending"] = "这个部位还没有分割模型，暂不可用";
  t["local.mask.remove"] = "删除蒙版";
  t["local.draw"] = "在画布上拖动";
  t["local.item.add"] = "添加子项";
  t["local.item.remove"] = "删除子项";
  t["local.item.mode.add"] = "添加";
  t["local.item.mode.subtract"] = "减去";
  t["local.item.text"] = "%1 · %2";
  t["local.item.invert"] = "反相";
  t["local.brush.tool.paint"] = kWordBrush;
  t["local.brush.tool.erase"] = "橡皮擦";
  t["local.brush.size"] = "大小";
  t["local.brush.flow"] = "流畅度";
  t["local.brush.density"] = "密度";
  t["history.local.strokes"] = "画笔笔触";
  t["local.item.text.invert"] = " · 反相";  // 接在子项行文字后面
  t["history.local.invert"] = "子项反相";
  t["history.local.remove"] = "删除蒙版";
  t["local.mask.rename"] = "重命名蒙版";
  // 历史（需求 §8.3）。
  t["history.step"] = "%1 %2";
  t["history.post"] = "后期调色·%1";
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
  t["trash.restore"] = kWordRestore;
  t["trash.erase"] = "彻底删除";
  t["trash.cancel"] = "取消";
  t["trash.clear"] = "清空回收站";
  t["trash.clear.confirm"] = "清空回收站将彻底删除其中的 %1 个项目，不能恢复（磁盘上的原图不会被删除）。图片和虚拟副本随所在项目一起保留到期。确认清空？";
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
  t["export.wm.opacity"] = kWordOpacity;
  t["export.wm.x"] = kWordHorizontalPosition;
  t["export.wm.y"] = kWordVerticalPosition;
  t["export.wm.plain"] = "同时导出无水印图片";
  // 问号说明原文（test 版实测）。
  t["export.wm.plain.tip"] = "开启该功能后，导出时将在指定导出路径下创建两个子文件夹，分别包含带水印图片和无水印图片";
  t["export.wm.choose"] = "选择水印素材";
  t["export.wm.need_material"] = "请先上传水印素材，或关闭水印";
  t["export.wm.not_saved"] = "水印素材没有保存：";
  t["export.why.overlay"] = "保留叠加层的作用还没有核实，暂不可用";
  t["export.layout.group"] = "照片排版";
  t["export.layout.id"] = "证件照排版";
  t["export.layout.plain"] = "普通照片排版";
  t["export.layout.landscape"] = "横向";
  t["export.layout.portrait"] = "竖向";
  t["export.layout.paper"] = "相纸";
  t["export.layout.dpi"] = "相纸分辨率（像素/英寸）";
  t["export.layout.rows"] = "行数";
  t["export.layout.cols"] = "列数";
  t["export.layout.gap"] = "间隔（像素）";
  t["export.layout.unit"] = "标尺单位";
  t["export.layout.unit.mm"] = "毫米";
  t["export.layout.unit.cm"] = "厘米";
  t["export.layout.unit.in"] = "英寸";
  t["export.layout.margin_top"] = "上边距";
  t["export.layout.margin_bottom"] = "下边距";
  t["export.layout.margin_left"] = "左边距";
  t["export.layout.margin_right"] = "右边距";
  t["export.layout.note"] = "排版把每张成品排到一张相纸上；边距是裁切窗口，照片的大小和位置不变。";
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
  // 精修右侧图标栏的模块（顺序同 PixCake 9.3.0：AI色彩、调色、局部 | 人像、图像、衣物、后调色、裁剪、历史、联机）；
  // 说明是无障碍说明，也是点开后面板的标题。“调色”的说明是色彩面板名“色彩调节”，由 ui/panels 提供。
  // 9.3.0 的图标栏里没有“宠物”（页面图里有），这里不列。
  lists["refine-modules"] = {item("ai-color", kWordAiColor),
                             item("color", "调色"),
                             item("local", "局部", {}, {}, "局部调色"),
                             item("portrait", "人像", {}, {}, "人像美化"),
                             item("image", "图像", {}, {}, "图像美化"),
                             item("clothing", "衣物", {}, {}, "衣物美化"),
                             item("post", "后调色", {}, {}, "后期调色"),
                             item("crop", "裁剪", {}, {}, "裁剪、旋转"),
                             item("history", "历史", {}, {}, kHistory),
                             item("capture", "联机", {}, {}, kCapture)};
  // 局部调色的蒙版区（PixCake 9.3.0，2026-11-08 截图）：主体、宠物、背景、自定义四个页签。主体页签的“常用蒙版”是
  // 语义部位（名字来自 kernel/mask 的部位表），这里只列另外三个页签；编号就是作用部位的参数值，没有分割的部位编号
  // 不在参数里，按钮禁用。
  lists["refine-local-tabs"] = {item("subject", "主体"), item("pet", "宠物"), item("background", "背景"),
                                item("custom", "自定义")};
  lists["refine-local-pet"] = {item("pet.all", "所有宠物")};
  lists["refine-local-background"] = {};
  for (const auto& [id, label] : {std::pair<const char*, const char*>{"background.all", "全部背景"}, {"background.sky", "天空"},
                                  {"background.grass", "草地"}, {"background.ground", "地面"}, {"background.rock", "礁石"},
                                  {"background.water", "水面"}, {"background.beach", "沙滩"}, {"background.mountain", "山"},
                                  {"background.building", "建筑物"}}) {
    lists["refine-local-background"].push_back(item(id, label));
  }
  lists["refine-local-custom"] = {item("smart", "智能选区"),        item("free", "自由选区"),
                                  item("quick", "快速选区"),        item("brush", kWordBrush),
                                  item("linear", kWordLinearGradient), item("radial", kWordRadialGradient),
                                  item("color_range", kWordColorRange), item("luminance_range", kWordLuminanceRange)};
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
  // 相纸（宽×高 mm，横向写法；竖向时对调）；编号是 "宽x高"。
  lists["export-papers"] = {item("89x127", "5寸 89×127mm"),   item("152x102", "6寸 152×102mm"),
                            item("178x127", "7寸 178×127mm"),  item("203x152", "8寸 203×152mm"),
                            item("254x203", "10寸 254×203mm"), item("254x305", "12寸 254×305mm"),
                            item("297x210", "A4 297×210mm"),   item("148x105", "A6 148×105mm"),
                            item("210x148", "A5 210×148mm")};
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

const std::vector<ShortcutEntry>& shortcut_table() {
  // 需求 §23.1 里本程序已有功能对应的键；还没有的功能（放大缩小、抓手、查看对比、蒙版、手动工具、曲线…）不在表里。
  // 官方“上一张/下一张”的 ↑/↓ 会和滑杆的方向键调值冲突，没有做。
  static const std::vector<ShortcutEntry> table = {
      {"go-home", "Ctrl+Shift+0", "转到首页", "global"},
      {"go-library", "Ctrl+Shift+1", "转到图库", "global"},
      {"go-refine", "Ctrl+Shift+2", "转到精修", "global"},
      {"create-project", "Ctrl+N", "创建项目", "home"},
      {"home-delete", "Delete", "删除项目", "home"},
      {"export-settings", "Shift+E", "导出设置", "project"},
      {"export-list", "Ctrl+S", "导出列表", "project"},
      {"select-all", "Ctrl+A", "图片全选", "library"},
      {"deselect", "Ctrl+D", "取消多选", "library"},
      {"library-delete", "Delete", "删除图片", "library"},
      {"grid-view", "Ctrl+G", "网格视图", "library"},
      {"single-view", "Ctrl+T", "单视图", "library"},
      {"enter-refine", "D", "进入精修页", "library"},
      {"copy-effect", "Ctrl+C", "复制效果", "project"},
      {"paste-effect", "Ctrl+V", "粘贴效果", "project"},
      {"sync-effect", "Ctrl+Shift+S", "同步到选中图片", "project"},
      {"virtual-copy", "Ctrl+'", "创建副本", "project"},
      {"undo", "Ctrl+Z", "单图撤销", "refine"},
      {"redo", "Ctrl+Shift+Z", "单图重做", "refine"},
      {"guide-horizontal", "Ctrl+R", "创建水平参考线", "refine"},
      {"guide-vertical", "Shift+R", "创建垂直参考线", "refine"},
      {"guide-toggle", "Alt+R", "隐藏或显示参考线", "refine"},
      {"preset-list", "Shift+S", "预设列表", "refine"},
      {"module-color", "E", "色彩", "refine"},
      {"module-local", "Ctrl+W", "局部调色", "refine"},
      {"module-portrait", "R", "人像", "refine"},
      {"module-image", "T", "图像", "refine"},
      {"module-clothing", "Y", "衣物", "refine"},
      {"module-post", "Ctrl+Shift+E", "后期调色", "refine"},
      {"module-crop", "C", "裁剪", "refine"},
      {"module-history", "Shift+V", "历史", "refine"},
      // 局部调色里的画笔（需求 §23.1）：K 选画笔，其余调当前画笔的设置，只在画笔设置显示时起作用。官方切到擦除是
      // Shift+E，和“导出设置”同键（官方按上下文分流），这里没有做；X 已是旗标排除。
      {"brush-tool", "K", kWordBrush, "refine"},
      {"brush-size-down", "[", std::string(kWordBrush) + "大小减小", "refine"},
      {"brush-size-up", "]", std::string(kWordBrush) + "大小增大", "refine"},
      {"brush-feather-down", "Shift+[", std::string(kWordBrush) + "羽化减小", "refine"},
      {"brush-feather-up", "Shift+]", std::string(kWordBrush) + "羽化增大", "refine"},
      {"brush-flow-down", ",", std::string(kWordBrush) + "流畅度减小", "refine"},
      {"brush-flow-up", ".", std::string(kWordBrush) + "流畅度增大", "refine"},
      {"brush-density-down", "Shift+,", std::string(kWordBrush) + "密度减小", "refine"},
      {"brush-density-up", "Shift+.", std::string(kWordBrush) + "密度增大", "refine"},
      {"rating-0", "0", "星级取消", "project"},
      {"rating-1", "1", "星级 1", "project"},
      {"rating-2", "2", "星级 2", "project"},
      {"rating-3", "3", "星级 3", "project"},
      {"rating-4", "4", "星级 4", "project"},
      {"rating-5", "5", "星级 5", "project"},
      {"color-red", "6", "色标红", "project"},
      {"color-yellow", "7", "色标黄", "project"},
      {"color-green", "8", "色标绿", "project"},
      {"color-blue", "9", "色标蓝", "project"},
      {"flag-pick", "P", "旗标选取", "project"},
      {"flag-exclude", "X", "旗标排除", "project"},
      {"flag-clear", "U", "旗标取消", "project"},
  };
  return table;
}

const std::string& shortcut_key(std::string_view id) {
  static const std::string empty;
  for (const auto& entry : shortcut_table()) {
    if (entry.id == id) return entry.key;
  }
  missing().insert("shortcut:" + std::string(id));
  return empty;
}

std::vector<std::string> missing_page_copy() { return {missing().begin(), missing().end()}; }

}  // namespace pixcake::ui::shell
