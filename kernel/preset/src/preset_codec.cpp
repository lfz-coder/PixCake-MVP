#include "pixcake/preset/preset_codec.hpp"

#include <cstddef>
#include <string>

namespace pixcake {
namespace {

constexpr char kMagic[] = "PXPRESETS1";
constexpr std::size_t kMagicSize = sizeof(kMagic) - 1;
constexpr std::uint32_t kMaxCount = 100000;

void put_u32(std::vector<std::uint8_t>& out, std::uint32_t value) {
  for (int shift = 0; shift < 32; shift += 8) out.push_back(static_cast<std::uint8_t>(value >> shift));
}

void put_bytes(std::vector<std::uint8_t>& out, const std::uint8_t* data, std::size_t size) {
  put_u32(out, static_cast<std::uint32_t>(size));
  out.insert(out.end(), data, data + size);
}

void put_string(std::vector<std::uint8_t>& out, const std::string& text) {
  put_bytes(out, reinterpret_cast<const std::uint8_t*>(text.data()), text.size());
}

class Reader {
 public:
  explicit Reader(const std::vector<std::uint8_t>& bytes) : bytes_(bytes) {}
  bool u32(std::uint32_t& value) {
    if (bytes_.size() - at_ < 4) return false;
    value = 0;
    for (int shift = 0; shift < 32; shift += 8) value |= static_cast<std::uint32_t>(bytes_[at_++]) << shift;
    return true;
  }
  bool bytes(std::vector<std::uint8_t>& out) {
    std::uint32_t size = 0;
    if (!u32(size) || bytes_.size() - at_ < size) return false;
    out.assign(bytes_.begin() + static_cast<std::ptrdiff_t>(at_), bytes_.begin() + static_cast<std::ptrdiff_t>(at_ + size));
    at_ += size;
    return true;
  }
  bool string(std::string& out) {
    std::vector<std::uint8_t> raw;
    if (!bytes(raw)) return false;
    out.assign(raw.begin(), raw.end());
    return true;
  }
  bool at_end() const { return at_ == bytes_.size(); }
  bool skip(std::size_t size) {
    if (bytes_.size() - at_ < size) return false;
    at_ += size;
    return true;
  }

 private:
  const std::vector<std::uint8_t>& bytes_;
  std::size_t at_ = 0;
};

}  // namespace

std::vector<std::uint8_t> encode_presets(const std::vector<Preset>& presets) {
  std::vector<std::uint8_t> out(kMagic, kMagic + kMagicSize);
  put_u32(out, static_cast<std::uint32_t>(presets.size()));
  for (const auto& preset : presets) {
    put_string(out, preset.id);
    put_string(out, preset.name);
    put_string(out, preset.category);
    put_string(out, preset.type);
    put_u32(out, static_cast<std::uint32_t>(preset.modules.size()));
    for (const auto& module : preset.modules) put_string(out, module);
    put_u32(out, static_cast<std::uint32_t>(preset.fragments.size()));
    for (const auto& fragment : preset.fragments) {
      put_string(out, fragment.capability_id);
      put_u32(out, fragment.version);
      put_bytes(out, fragment.inner.data(), fragment.inner.size());
    }
  }
  return out;
}

std::optional<std::vector<Preset>> decode_presets(const std::vector<std::uint8_t>& bytes) {
  if (bytes.size() < kMagicSize || std::string(bytes.begin(), bytes.begin() + kMagicSize) != kMagic) return std::nullopt;
  Reader in(bytes);
  if (!in.skip(kMagicSize)) return std::nullopt;
  std::uint32_t count = 0;
  if (!in.u32(count) || count > kMaxCount) return std::nullopt;
  std::vector<Preset> presets;
  for (std::uint32_t i = 0; i < count; ++i) {
    Preset preset;
    std::uint32_t modules = 0;
    std::uint32_t fragments = 0;
    if (!in.string(preset.id) || !in.string(preset.name) || !in.string(preset.category) || !in.string(preset.type) ||
        !in.u32(modules) || modules > kMaxCount) {
      return std::nullopt;
    }
    for (std::uint32_t m = 0; m < modules; ++m) {
      std::string module;
      if (!in.string(module)) return std::nullopt;
      preset.modules.push_back(std::move(module));
    }
    if (!in.u32(fragments) || fragments > kMaxCount) return std::nullopt;
    for (std::uint32_t f = 0; f < fragments; ++f) {
      PresetFragment fragment;
      if (!in.string(fragment.capability_id) || !in.u32(fragment.version) || !in.bytes(fragment.inner)) return std::nullopt;
      preset.fragments.push_back(std::move(fragment));
    }
    presets.push_back(std::move(preset));
  }
  if (!in.at_end()) return std::nullopt;
  return presets;
}

}  // namespace pixcake
