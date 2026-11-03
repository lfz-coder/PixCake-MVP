#include "pixcake/capability/capability_sync.hpp"

namespace pixcake {
namespace {

constexpr char kRecomputeReason[] = "同步时按每张图重新计算，不是把数值原样抄过去";

struct Row {
  const char* id;
  const char* module_label;
  const char* label;
  const char* capability_id;
  bool enabled;
  const char* blocked_reason;
};

const Row kRows[] = {
    {"color.match", "AI 色彩", "AI 追色", "color.match", false, kRecomputeReason},
    {"color.match.local", "AI 色彩", "局部追色", "", false, "局部追色不能批量同步"},
    {"color.style", "AI 色彩", "AI 色彩风格", "color.style", false, kRecomputeReason},
    {"color.global", "色彩", "全图色彩工具", "color.global", true, ""},
    {"color.local", "色彩", "局部调色", "color.local", true, ""},
    {"color.post", "色彩", "后期调色", "color.post", true, ""},
    {"detail", "色彩", "细节", "detail", true, ""},
    {"portrait.blemish", "人像", "祛除瑕疵", "portrait.blemish", true, ""},
    {"portrait.skin", "人像", "皮肤调整", "portrait.skin", true, ""},
    {"portrait.reshape", "人像", "面部重塑", "portrait.reshape", true, ""},
    {"portrait.feature", "人像", "表情、牙齿、眼睛、妆容、头发", "portrait.feature", true, ""},
    {"portrait.body", "人像", "手、脚、全身美型", "portrait.body", true, ""},
    {"image.background", "图像", "背景调整", "image.background", true, ""},
    {"image.idphoto", "图像", "证件照换背景", "image.idphoto", true, ""},
    {"image.sky", "图像", "换天空", "image.sky", true, ""},
    {"clothes", "衣物", "衣物美化", "clothes", true, ""},
    {"pet", "宠物", "宠物美化", "pet", true, ""},
    {"crop", "裁剪", "裁剪旋转", "crop", true, ""},
    {"manual.edge", "手动", "祛边", "manual.edge", true, ""},
    {"manual.liquify", "手动", "液化", "manual.liquify", true, ""},
    {"manual.heal", "手动", "污点修复、修补、仿制", "manual.heal", true, ""},
};

}  // namespace

std::vector<CapabilitySyncRow> capability_sync_rows() {
  std::vector<CapabilitySyncRow> rows;
  rows.reserve(sizeof(kRows) / sizeof(kRows[0]));
  for (const auto& row : kRows) {
    CapabilitySyncRow item;
    item.id = row.id;
    item.module_label = row.module_label;
    item.label = row.label;
    item.capability_id = row.capability_id;
    item.enabled = row.enabled;
    item.blocked_reason = row.blocked_reason;
    rows.push_back(std::move(item));
  }
  return rows;
}

}  // namespace pixcake
