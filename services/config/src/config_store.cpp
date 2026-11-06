#include "pixcake/config/config_store.hpp"

#include <algorithm>
#include <initializer_list>
#include <utility>

namespace pixcake::config {
namespace {

bool valid_identifier(const std::string& value) {
  return !value.empty() && value.find('\0') == std::string::npos;
}

bool duplicate(const std::vector<std::string>& values, const std::string& value) {
  return std::find(values.begin(), values.end(), value) != values.end();
}

void append_acceleration(std::vector<AccelerationRule>& rules, AccelerationCharge charge,
                         std::int64_t points, std::initializer_list<const char*> names) {
  for (const char* name : names) {
    rules.push_back({name, charge, points});
  }
}

void append_third_party(std::vector<ThirdPartyToolRule>& rules, const char* package_name,
                        bool high_compute,
                        std::initializer_list<std::pair<const char*, std::int64_t>> entries) {
  for (const auto& entry : entries) {
    rules.push_back({package_name, entry.first, entry.second, high_compute});
  }
}

ConfigSnapshot build_default_snapshot() {
  ConfigSnapshot snapshot;
  snapshot.client_version = "9.3.0";
  snapshot.queue_policy = {false, true};

  append_acceleration(snapshot.official_acceleration, AccelerationCharge::Points, 5, {"AI 面部重绘极速"});
  append_acceleration(snapshot.official_acceleration, AccelerationCharge::Points, 10, {"AI 面部重绘标准"});
  append_acceleration(snapshot.official_acceleration, AccelerationCharge::LimitedFree, 0,
                     {"闭眼修复", "AI 加腹肌", "创成式填充", "祛体毛", "鞋子祛污", "裙子蓬松", "裙摆平整", "AI 换天空"});
  append_acceleration(snapshot.official_acceleration, AccelerationCharge::Points, 40, {"AI 瘦肚子", "AI 老照片修复"});
  append_acceleration(snapshot.official_acceleration, AccelerationCharge::Points, 3, {"证件照换衣服"});
  append_acceleration(snapshot.official_acceleration, AccelerationCharge::Points, 10,
                     {"AI 祛路人", "智能消除生成式", "智能道具修复"});
  append_acceleration(snapshot.official_acceleration, AccelerationCharge::PermanentFree, 0,
                     {"智能消除快速", "AI 超清", "发光", "镜头虚化"});
  append_acceleration(snapshot.official_acceleration, AccelerationCharge::Points, 20,
                     {"AI 补草地", "沙滩美化", "自然光", "追光追色"});
  append_acceleration(snapshot.official_acceleration, AccelerationCharge::Points, 80,
                     {"AI 衣服精修 1.0", "衣服精修 2.0 Pro 2K"});
  append_acceleration(snapshot.official_acceleration, AccelerationCharge::Points, 160, {"衣服精修 2.0 Pro 4K"});
  append_acceleration(snapshot.official_acceleration, AccelerationCharge::Points, 40, {"追场景"});
  append_acceleration(snapshot.official_acceleration, AccelerationCharge::Points, 30, {"AI 布景 1.0"});
  append_acceleration(snapshot.official_acceleration, AccelerationCharge::Points, 40, {"AI 布景 2.0 4K"});
  append_acceleration(snapshot.official_acceleration, AccelerationCharge::Points, 45, {"AI 布景 2.0 6K"});
  append_acceleration(snapshot.official_acceleration, AccelerationCharge::Points, 20, {"春日樱花"});
  append_acceleration(snapshot.official_acceleration, AccelerationCharge::Points, 30, {"夏日晴空", "秋天落叶", "冬日雪景"});
  append_acceleration(snapshot.official_acceleration, AccelerationCharge::Points, 15, {"AI 补光", "柔化阴影", "夕阳光"});
  append_acceleration(snapshot.official_acceleration, AccelerationCharge::Points, 50, {"AI 背景扩图"});

  snapshot.package_prices = {
      {"刘锐 AIGC", 399}, {"花生国风", 239}, {"幻象 Cos", 399}, {"光影大师", 299},
      {"电商美工", 99}, {"影像百宝箱", 65}, {"影像优化", 45}, {"AIGC 设计", 66},
      {"全能电商", 50}, {"婚纱摄影", 179}, {"萌宠拍档", 69}, {"专业生图", 49},
  };

  append_third_party(snapshot.third_party_tools, "刘锐 AIGC", false,
                    {{"背景提取", 35}, {"光影融合", 40}, {"重绘融合", 80}, {"元素迁移", 75},
                     {"自由光线", 50}, {"沙滩修复", 70}, {"草坪修复", 55}, {"建筑修复", 45},
                     {"旧照修复", 45}, {"扩图", 50}, {"全局融合", 55}, {"风格迁移", 100},
                     {"美体美型", 80}, {"面部中性灰", 95}, {"全景融合 2.0", 100}, {"元素迁移 2.0", 155},
                     {"高清放大", 15}, {"形体液化 2.0", 45}, {"草坪修复 2.0", 75}, {"形体液化 3.0", 40},
                     {"风格参考", 175}, {"完美景棚", 65}});
  append_third_party(snapshot.third_party_tools, "花生国风", false,
                    {{"斑驳光影", 50}, {"碧波海鸥", 50}, {"春日雏菊", 55}, {"春日梨花", 30},
                     {"春日桃花", 25}, {"春日樱花", 25}, {"电影光影", 50}, {"飞舞裙摆", 45},
                     {"氛围飘发", 45}, {"粉黛花海", 55}, {"古风逆光", 55}, {"光影增强", 50},
                     {"黑色长相思", 55}, {"红色长相思", 55}, {"锦鲤裙摆", 65}, {"轮廓光", 55},
                     {"日照金山", 60}, {"水面波光", 40}, {"水墨雨雾", 55}, {"武侠竹林", 40},
                     {"夕阳时刻", 55}, {"一键白烟", 30}, {"一键夜景", 30}, {"一键远山", 35},
                     {"一句话 P 图", 30}, {"油菜花海", 25}, {"中性灰磨皮", 15}});
  append_third_party(snapshot.third_party_tools, "幻象 Cos", false,
                    {{"2D 转 3D", 60}, {"场照清场", 45}, {"成男转绘", 60}, {"飞沙走石", 55},
                     {"氛围底光", 35}, {"氛围飘发", 35}, {"光影溶图", 30}, {"光影增强", 55},
                     {"黑色烟尘", 70}, {"厚涂发丝", 45}, {"厚涂眼睛", 35}, {"厚涂妆感", 65},
                     {"火花碎石", 60}, {"枪林弹雨", 40}, {"闪光灯轮廓光", 55}, {"手绘感长发", 55},
                     {"水面波光", 40}, {"头发美化", 30}, {"消除打底裤", 40}, {"消除灯架", 40},
                     {"消除梯子", 55}, {"一键白棚", 40}, {"一键场照", 50}, {"一键丰胸", 45},
                     {"一键雷电", 40}, {"一键逆光", 60}, {"一键夜景", 35}, {"一句话 P 图", 40},
                     {"fufu 玩偶", 35}, {"JK 樱花", 30}});
  append_third_party(snapshot.third_party_tools, "光影大师", false,
                    {{"一键造光", 65}, {"光影纠正", 15}, {"光色同步", 45}, {"发丝逆光", 55},
                     {"夕阳侧逆光", 35}, {"指令造光", 15}, {"晨光侧逆光", 40}, {"朦胧体积光", 50},
                     {"自然侧光", 50}, {"逆光补光去噪", 25}, {"斑驳光", 20}, {"艺术光", 35},
                     {"室内窗户光", 35}, {"束光", 35}, {"赛博影棚光", 20}, {"梦幻轮廓光", 45},
                     {"午后顺光", 75}, {"王家卫影调", 45}});
  append_third_party(snapshot.third_party_tools, "电商美工", false,
                    {{"产品精修", 25}, {"首饰钻戒精修", 30}, {"数码产品精修", 30}, {"化妆品精修", 30},
                     {"产品光影融合", 30}, {"光影融合-矫正透视", 30}, {"更换产品背景", 25}, {"商品多视角图", 25},
                     {"场景图转夜晚", 25}, {"修改模特发型", 25}, {"空背景加模特", 25}, {"模特多视角图", 25},
                     {"更换模特背景", 25}});
  append_third_party(snapshot.third_party_tools, "影像百宝箱", false,
                    {{"产品精修", 20}, {"电商产品光影融图", 20}, {"动漫转真人", 20}, {"黑白照片上色修复", 10},
                     {"人像精修", 20}, {"日式夏日清新胶片滤镜", 20}, {"一键乐高积木质感风格转绘", 20},
                     {"一键新海诚动漫风格转绘", 10}, {"一键针织毛线风格转绘", 10}, {"一键 citypop 复古风格转绘", 10}});
  append_third_party(snapshot.third_party_tools, "影像优化", false,
                    {{"复古胶片滤镜", 10}, {"光影迁移", 30}, {"光影重塑", 15}, {"去灰提亮", 10}, {"瘦身", 10}});
  append_third_party(snapshot.third_party_tools, "AIGC 设计", false,
                    {{"指定产品部分改色", 30}, {"3C 产品光效生成", 30}, {"全类目产品精修", 30}, {"产品背景合成打光", 25},
                     {"多种产品特效生成", 30}, {"模特服装迁移替换", 60}, {"纠正背景透视溶图", 35}, {"快速精细产品抠图", 10},
                     {"人像背景合成打光", 30}, {"提取服装印花图案", 30}, {"指定图片物品移除", 30}});
  append_third_party(snapshot.third_party_tools, "全能电商", false,
                    {{"产品渲染级精修", 45}, {"人像肤质精修", 15}, {"光影重塑", 25}, {"全能场景溶图", 15},
                     {"全能模型渲染", 25}, {"无缝扩图", 10}, {"珠宝首饰精修", 45}, {"真实背景替换", 10},
                     {"简约产品线稿", 30}, {"纠正透视溶图", 10}});
  append_third_party(snapshot.third_party_tools, "专业生图", true,
                    {{"一句话生图", 100}, {"姿势变换", 100}, {"表情变换", 100}, {"多图融合", 100}});
  append_third_party(snapshot.third_party_tools, "婚纱摄影", false,
                    {{"白天转夜景", 135}, {"百叶窗条纹光影", 125}, {"彩虹光影氛围", 110}, {"场景扩图", 40},
                     {"场景瑕疵修复", 60}, {"窗格光影氛围", 115}, {"丰胸", 40}, {"局部迁移", 55},
                     {"曝光修复", 60}, {"去除纹身", 45}, {"人物移除", 55}, {"生成前景", 80},
                     {"网图无损去水印", 45}, {"夕阳光影氛围", 130}, {"夜景极光光影氛围", 130}, {"参考图换背景", 110},
                     {"草坪修复", 70}, {"一键婚纱照", 110}, {"一键结婚登记照", 35}, {"一键形象照", 110}, {"一键证件照", 70}});
  append_third_party(snapshot.third_party_tools, "萌宠拍档", false,
                    {{"宠物插画", 40}, {"宠物场景置换", 40}, {"宠物换装", 40}, {"宠物祛泪痕", 40},
                     {"宠物生日照", 60}, {"宠物肖像", 60}, {"宠物写真", 65}, {"宠物油画", 40},
                     {"宠物证件照", 80}, {"多宠合影", 65}, {"人宠合影", 55}});
  return snapshot;
}

}  // namespace

const ConfigSnapshot& default_snapshot() {
  static const ConfigSnapshot snapshot = build_default_snapshot();
  return snapshot;
}

ResultCode validate(const ConfigSnapshot& snapshot) {
  if (!valid_identifier(snapshot.client_version)) {
    return ResultCode::StateConflict;
  }
  std::vector<std::string> names;
  for (const auto& rule : snapshot.official_acceleration) {
    if (!valid_identifier(rule.tool_name) || duplicate(names, rule.tool_name)) {
      return ResultCode::StateConflict;
    }
    names.push_back(rule.tool_name);
    if ((rule.charge == AccelerationCharge::Points && rule.points <= 0) ||
        (rule.charge != AccelerationCharge::Points && rule.points != 0)) {
      return ResultCode::StateConflict;
    }
  }
  names.clear();
  for (const auto& package : snapshot.package_prices) {
    if (!valid_identifier(package.package_name) || package.annual_price < 0 ||
        duplicate(names, package.package_name)) {
      return ResultCode::StateConflict;
    }
    names.push_back(package.package_name);
  }
  std::vector<std::string> tool_keys;
  for (const auto& tool : snapshot.third_party_tools) {
    const std::string key = tool.package_name + "\n" + tool.tool_name;
    if (!valid_identifier(tool.package_name) || !valid_identifier(tool.tool_name) || tool.points <= 0 ||
        duplicate(tool_keys, key)) {
      return ResultCode::StateConflict;
    }
    tool_keys.push_back(key);
  }
  return ResultCode::Ok;
}

std::optional<AccelerationRule> find_official_acceleration(const ConfigSnapshot& snapshot,
                                                            const std::string& tool_name) {
  const auto found = std::find_if(snapshot.official_acceleration.begin(), snapshot.official_acceleration.end(),
                                  [&tool_name](const AccelerationRule& rule) {
                                    return rule.tool_name == tool_name;
                                  });
  return found == snapshot.official_acceleration.end() ? std::nullopt : std::optional<AccelerationRule>(*found);
}

std::optional<ThirdPartyToolRule> find_third_party_tool(const ConfigSnapshot& snapshot,
                                                        const std::string& package_name,
                                                        const std::string& tool_name) {
  const auto found = std::find_if(snapshot.third_party_tools.begin(), snapshot.third_party_tools.end(),
                                  [&package_name, &tool_name](const ThirdPartyToolRule& rule) {
                                    return rule.package_name == package_name && rule.tool_name == tool_name;
                                  });
  return found == snapshot.third_party_tools.end() ? std::nullopt : std::optional<ThirdPartyToolRule>(*found);
}

ConfigCache::ConfigCache() : snapshot_(default_snapshot()) {}

ConfigLoadResult ConfigCache::load(std::optional<ConfigSnapshot> cached) {
  if (!cached.has_value()) {
    snapshot_ = default_snapshot();
    used_defaults_ = true;
    return {ResultCode::Ok, true};
  }
  if (validate(*cached) != ResultCode::Ok) {
    return {ResultCode::StateConflict, used_defaults_};
  }
  snapshot_ = std::move(*cached);
  used_defaults_ = false;
  return {ResultCode::Ok, false};
}

ResultCode ConfigCache::replace(ConfigSnapshot snapshot) {
  if (validate(snapshot) != ResultCode::Ok) {
    return ResultCode::StateConflict;
  }
  snapshot_ = std::move(snapshot);
  used_defaults_ = false;
  return ResultCode::Ok;
}

const ConfigSnapshot& ConfigCache::snapshot() const { return snapshot_; }

bool ConfigCache::used_defaults() const { return used_defaults_; }

}  // namespace pixcake::config
