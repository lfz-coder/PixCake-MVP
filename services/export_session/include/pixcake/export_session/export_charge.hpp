#pragma once

#include "pixcake/support/result_code.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace pixcake::export_session {

// 一次导出算哪一类（test 版导出弹窗的“精修效果图 / 免费效果图 / 原图”，
// docs/superpowers/evidence/2026-11-07-export-credit-observation.md）：
// 没有任何调整是原图；只用了免费功能是免费效果；用到任何精修功能是精修效果，只有它扣张数。
enum class ExportEffect { Original, Free, Refined };

// 按参数快照判断：编译出的每个操作只要有一个值离开注册的默认值，就算“用到”了这个能力。
// 免费：色彩调节（color.global，面板上的分组都带 Free）、细节、裁剪旋转、手动工具、
// 后期调色的全图调整（不带蒙版）；蒙版和图层参数跟着别的能力走，自己不算。其余（AI 色彩、
// 人像、图像、衣物、宠物、局部调色、带蒙版的后期调色）都算精修。快照读不了时按精修算。
ExportEffect classify_export(const std::vector<std::uint8_t>& document_snapshot);

// 计费用的“同一张图片”标识：按画面内容取，不按文件字节（test 版重新压缩同一画面不再扣，
// 翻转后再扣）。形如 "pic-<宽>x<高>-<64 位十六进制>"：存储尺寸加上 64 像素缩略图的
// 16×16 差值哈希。虚拟副本共用原图，标识自然相同。
ResultCode picture_fingerprint(const std::string& path, std::string& out, std::string& diagnostic);

// 两个标识是否算同一张图：尺寸相同、哈希相差不超过 kSamePictureBits 位
// （重新压缩只让少数位翻转；翻转、换图会让大半位不同）。
constexpr int kSamePictureBits = 12;
bool same_picture(const std::string& a, const std::string& b);

}  // namespace pixcake::export_session
