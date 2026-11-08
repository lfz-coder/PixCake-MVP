#pragma once

#include "pixcake/preset/preset_library.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace pixcake {

// 预设库存盘的字节格式（本机“我的”预设）：魔数加版本，之后是预设列表，字符串和字节串都带长度。
// 解码失败（魔数不对、长度越界、多出尾巴）返回空，调用方当作没有这份文件，不写一半。
std::vector<std::uint8_t> encode_presets(const std::vector<Preset>& presets);
std::optional<std::vector<Preset>> decode_presets(const std::vector<std::uint8_t>& bytes);

}  // namespace pixcake
