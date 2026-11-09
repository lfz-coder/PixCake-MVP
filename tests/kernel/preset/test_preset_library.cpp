
#include "pixcake/preset/preset_codec.hpp"
#include "pixcake/preset/preset_library.hpp"
#include "pixcake/capability/fragment_codec.hpp"
#include "support/fake_file_system.hpp"

#include <cstdio>
#include <optional>
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

// 能力片段的合法编码，里面只有一条测试用的未知记录（类型 77）带着标记字节。
// 预设、同步、粘贴经能力模块校验片段，裸字节不再被接受。
std::vector<std::uint8_t> marked(std::uint8_t n) {
  return pixcake::encode_fragment_records({{77, {n}}});
}

const char* kPath = "docs/img.pxedit";

class Latch final : public pixcake::FastExportLatch {
 public:
  bool on = false;
  bool enabled() const override { return on; }
};

std::string han(std::size_t count) {
  std::string out;
  for (std::size_t i = 0; i < count; ++i) {
    out.append("\xE4\xB8\x80");
  }
  return out;
}

pixcake::Preset portrait_named(std::string id, std::string name) {
  pixcake::Preset preset;
  preset.id = std::move(id);
  preset.name = std::move(name);
  preset.type = pixcake::kPresetPortrait;
  return preset;
}

void test_c44_b04_b20_b21_name_counts_characters_not_bytes() {
  pixcake::PresetLibrary library;
  EXPECT(library.save(portrait_named("p20", han(20)), false) == pixcake::ResultCode::Ok);
  EXPECT(library.save(portrait_named("p7", han(7)), false) == pixcake::ResultCode::Ok);
  EXPECT(han(7).size() == 21);
  EXPECT(library.presets().size() == 2);

  EXPECT(library.save(portrait_named("p21", han(21)), false) == pixcake::ResultCode::StateConflict);
  EXPECT(library.save(portrait_named("ascii", std::string(21, 'a')), false) ==
         pixcake::ResultCode::StateConflict);
  EXPECT(library.save(portrait_named("empty", ""), false) == pixcake::ResultCode::StateConflict);
  EXPECT(library.save(portrait_named("blank", " \t\n"), false) == pixcake::ResultCode::StateConflict);
  EXPECT(library.save(portrait_named("fullwidth-blank", "\xE3\x80\x80"), false) ==
         pixcake::ResultCode::StateConflict);
  EXPECT(library.save(portrait_named("badutf", "\xFF"), false) == pixcake::ResultCode::StateConflict);
  EXPECT(library.save(portrait_named("", han(1)), false) == pixcake::ResultCode::StateConflict);
  EXPECT(library.presets().size() == 2);
  EXPECT(library.presets()[0].name == han(20));
}

void test_c43_type_and_module_must_match() {
  pixcake::PresetLibrary library;
  auto wrong = portrait_named("filter", han(1));
  wrong.type = "滤镜";
  EXPECT(library.save(wrong, false) == pixcake::ResultCode::StateConflict);

  auto mixed = portrait_named("mixed", han(1));
  mixed.fragments.push_back(pixcake::PresetFragment{"color.global", 1, marked(1)});
  EXPECT(library.save(mixed, false) == pixcake::ResultCode::StateConflict);

  auto pet = portrait_named("pet", han(1));
  pet.type = pixcake::kPresetCombo;
  pet.modules = {"人像", "色彩"};
  pet.fragments.push_back(pixcake::PresetFragment{"pet", 1, marked(1)});
  EXPECT(library.save(pet, false) == pixcake::ResultCode::StateConflict);

  auto one = portrait_named("one", han(1));
  one.type = pixcake::kPresetCombo;
  one.modules = {"人像"};
  EXPECT(library.save(one, false) == pixcake::ResultCode::StateConflict);

  auto combo = portrait_named("combo", han(1));
  combo.type = pixcake::kPresetCombo;
  combo.modules = {"人像", "色彩"};
  EXPECT(library.save(combo, false) == pixcake::ResultCode::Ok);
  EXPECT(library.presets().size() == 1);
}

void test_overwrite_and_import_do_not_change_old_rows() {
  pixcake::PresetLibrary library;
  EXPECT(library.save(portrait_named("p1", han(1)), false) == pixcake::ResultCode::Ok);
  EXPECT(library.save(portrait_named("p1", han(2)), false) == pixcake::ResultCode::StateConflict);
  EXPECT(library.presets()[0].name == han(1));
  EXPECT(library.save(portrait_named("p1", han(2)), true) == pixcake::ResultCode::Ok);
  EXPECT(library.presets().size() == 1);
  EXPECT(library.presets()[0].name == han(2));
  EXPECT(library.save(portrait_named("missing", han(1)), true) == pixcake::ResultCode::StateConflict);
  EXPECT(library.presets().size() == 1);

  EXPECT(library.import_preset(portrait_named("p2", han(1)), true, false) ==
         pixcake::ResultCode::StateConflict);
  EXPECT(library.import_preset(portrait_named("p3", han(1)), false, true) ==
         pixcake::ResultCode::StateConflict);
  EXPECT(library.presets().size() == 1);
  EXPECT(library.import_preset(portrait_named("p2", han(1)), false, false) == pixcake::ResultCode::Ok);
  EXPECT(library.presets().size() == 2);
}

void test_c43_color_replaces_and_ai_color_stacks() {
  auto document = *pixcake::EditDocument::create("img-1");
  document.store_fragment("color.global", 1, marked(1));
  document.store_fragment("color.local", 1, marked(2));
  document.store_fragment("portrait.blemish", 1, marked(3));
  document.store_fragment("color.match", 1, marked(4));
  pixcake::test::FakeFileSystem files;
  EXPECT(document.write_snapshot(files, kPath) == pixcake::ResultCode::Ok);

  pixcake::Preset color;
  color.id = "color";
  color.name = han(1);
  color.type = pixcake::kPresetColor;
  color.fragments.push_back(pixcake::PresetFragment{"color.global", 1, marked(9)});

  pixcake::PresetLibrary library;
  Latch latch;
  const auto warning = library.check_apply(document, color);
  EXPECT(warning.code == pixcake::ResultCode::Ok);
  EXPECT(warning.overwrite_color);

  pixcake::Preset ai;
  ai.id = "ai";
  ai.name = han(1);
  ai.type = pixcake::kPresetAiColor;
  ai.fragments.push_back(pixcake::PresetFragment{"color.style", 1, marked(8)});
  const auto ai_check = library.check_apply(document, ai);
  EXPECT(ai_check.code == pixcake::ResultCode::Ok);
  EXPECT(!ai_check.overwrite_color);

  latch.on = true;
  EXPECT(library.apply(document, color, 0, files, kPath, latch) == pixcake::ResultCode::FastExportBusy);
  latch.on = false;
  EXPECT(library.apply(document, color, 0, files, kPath, latch) == pixcake::ResultCode::Ok);
  EXPECT(document.revision() == 1);
  EXPECT(document.fragment("color.global")->inner == marked(9));
  EXPECT(!document.fragment("color.local").has_value());
  EXPECT(document.fragment("portrait.blemish")->inner == marked(3));
  EXPECT(document.fragment("color.match")->inner == marked(4));
  EXPECT(document.preset_refs().empty());

  EXPECT(library.apply(document, ai, 1, files, kPath, latch) == pixcake::ResultCode::Ok);
  EXPECT(document.fragment("color.global")->inner == marked(9));
  EXPECT(document.fragment("color.style")->inner == marked(8));

  const auto saved = files.files[kPath];
  EXPECT(library.apply(document, color, 0, files, kPath, latch) == pixcake::ResultCode::StateConflict);
  EXPECT(document.revision() == 2);
  EXPECT(files.files[kPath] == saved);

  files.write_error = pixcake::ResultCode::DiskFull;
  EXPECT(library.apply(document, color, 2, files, kPath, latch) == pixcake::ResultCode::DiskFull);
  EXPECT(document.revision() == 2);
  EXPECT(files.files[kPath] == saved);
}

}  // namespace

// C-31：预设经能力模块写入。取值不符合参数模式时整次拒绝、文档不变；目标片段比本能力新时保留。
void test_c31_preset_apply_validates_through_capability_modules() {
  auto document = *pixcake::EditDocument::create("img-1");
  document.store_fragment("color.global", 1, marked(1));
  document.store_fragment("color.local", 3, marked(2));
  pixcake::test::FakeFileSystem files;
  EXPECT(document.write_snapshot(files, kPath) == pixcake::ResultCode::Ok);
  pixcake::PresetLibrary library;
  Latch latch;

  pixcake::Preset bad;
  bad.id = "bad";
  bad.name = han(1);
  bad.type = pixcake::kPresetColor;
  bad.fragments.push_back(pixcake::PresetFragment{
      "color.global", 1,
      pixcake::encode_fragment_records({{pixcake::kFragmentParameterRecord,
                                         pixcake::encode_parameter_record("vibrance", "500")}})});
  const auto saved = files.files[kPath];
  EXPECT(library.apply(document, bad, 0, files, kPath, latch) == pixcake::ResultCode::StateConflict);
  EXPECT(document.revision() == 0 && document.fragment("color.global")->inner == marked(1));
  EXPECT(files.files[kPath] == saved);

  pixcake::Preset good = bad;
  good.fragments[0].inner = pixcake::encode_fragment_records(
      {{pixcake::kFragmentParameterRecord, pixcake::encode_parameter_record("vibrance", "10")}});
  EXPECT(library.apply(document, good, 0, files, kPath, latch) == pixcake::ResultCode::Ok);
  EXPECT(document.committed_parameter("color.global", "vibrance") == std::optional<std::string>("10"));
  // color.local 的片段来自更新的版本：替换整个色彩模块时也不删它。
  EXPECT(document.fragment("color.local") && document.fragment("color.local")->version == 3);
}

// 管理和存盘：重命名守 20 字、删除、按已用能力推断类型、字节格式来回一致、坏字节被拒。
void test_manage_infer_and_codec() {
  pixcake::PresetLibrary library;
  pixcake::Preset first = portrait_named("a", "first");
  pixcake::Preset second = portrait_named("b", "second");
  first.category = "wedding";
  first.fragments.push_back({"portrait.skin", 1, marked(1)});
  EXPECT(library.save(first, false) == pixcake::ResultCode::Ok);
  EXPECT(library.save(second, false) == pixcake::ResultCode::Ok);
  EXPECT(library.rename("a", "renamed") == pixcake::ResultCode::Ok && library.find("a")->name == "renamed");
  EXPECT(library.rename("a", han(21)) == pixcake::ResultCode::StateConflict && library.find("a")->name == "renamed");
  EXPECT(library.rename("zz", "x") == pixcake::ResultCode::StateConflict);
  const auto bytes = pixcake::encode_presets(library.presets());
  const auto decoded = pixcake::decode_presets(bytes);
  EXPECT(decoded && decoded->size() == 2 && (*decoded)[0].id == "a" && (*decoded)[0].name == "renamed" &&
         (*decoded)[0].category == "wedding" && (*decoded)[0].fragments.size() == 1 &&
         (*decoded)[0].fragments[0].capability_id == "portrait.skin" && (*decoded)[0].fragments[0].inner == marked(1));
  auto cut = bytes;
  cut.pop_back();
  auto padded = bytes;
  padded.push_back(0);
  auto wrong = bytes;
  wrong[0] = 'X';
  EXPECT(!pixcake::decode_presets(cut) && !pixcake::decode_presets(padded) && !pixcake::decode_presets(wrong));
  EXPECT(!pixcake::decode_presets({}) && pixcake::decode_presets(pixcake::encode_presets({}))->empty());
  pixcake::PresetLibrary other;
  EXPECT(other.load(*decoded) == pixcake::ResultCode::Ok && other.presets().size() == 2);
  auto bad = *decoded;
  bad[1].id = bad[0].id;  // duplicate ids
  EXPECT(other.load(bad) == pixcake::ResultCode::StateConflict && other.presets().size() == 2);
  EXPECT(library.remove("a") == pixcake::ResultCode::Ok && library.find("a") == nullptr &&
         library.remove("a") == pixcake::ResultCode::StateConflict && library.presets().size() == 1);
  std::vector<std::string> modules;
  EXPECT(pixcake::PresetLibrary::infer_type({"color.global", "detail"}, modules) == pixcake::kPresetColor &&
         modules.empty());
  EXPECT(pixcake::PresetLibrary::infer_type({"color.global", "portrait.skin"}, modules) == pixcake::kPresetCombo &&
         modules.size() == 2);
  EXPECT(pixcake::PresetLibrary::infer_type({}, modules).empty() && modules.empty());
}

// 局部调色的多个蒙版随预设整体写入：带前缀的参数照常校验，不合法的前缀或取值整次拒绝、文档不变。
void test_local_masks_travel_with_a_preset() {
  auto document = *pixcake::EditDocument::create("img-masks");
  pixcake::test::FakeFileSystem files;
  EXPECT(document.write_snapshot(files, kPath) == pixcake::ResultCode::Ok);
  pixcake::PresetLibrary library;
  Latch latch;
  const auto fragment = [](const char* first_id, const char* first, const char* second_id, const char* second) {
    return pixcake::PresetFragment{
        "color.local", 2,
        pixcake::encode_fragment_records(
            {{pixcake::kFragmentParameterRecord, pixcake::encode_parameter_record(first_id, first)},
             {pixcake::kFragmentParameterRecord, pixcake::encode_parameter_record(second_id, second)}})};
  };
  pixcake::Preset preset;
  preset.id = "masks";
  preset.name = "two masks";
  preset.type = pixcake::kPresetColor;
  preset.fragments.push_back(fragment("m2:exposure", "1", "m3:region", "person.hair"));
  EXPECT(library.apply(document, preset, 0, files, kPath, latch) == pixcake::ResultCode::Ok);
  EXPECT(document.committed_parameter("color.local", "m2:exposure") == std::optional<std::string>("1"));
  EXPECT(document.committed_parameter("color.local", "m3:region") == std::optional<std::string>("person.hair"));
  EXPECT(document.fragment("color.local")->version == 2);
  // A parameter with an invalid prefix is an unknown field: kept in the fragment like any unknown parameter, never drawn.
  pixcake::Preset odd = preset;
  odd.fragments[0] = fragment("m99:exposure", "1", "m3:region", "person.hair");
  EXPECT(library.apply(document, odd, 1, files, kPath, latch) == pixcake::ResultCode::Ok);
  EXPECT(!document.committed_parameter("color.local", "m2:exposure") && document.committed_parameter("color.local", "m3:region"));
  // A value outside the range of a valid prefixed parameter is refused as a whole and nothing changes.
  const auto saved = files.files[kPath];
  pixcake::Preset bad_value = preset;
  bad_value.fragments[0] = fragment("m2:exposure", "9", "m3:region", "person.hair");
  EXPECT(library.apply(document, bad_value, 2, files, kPath, latch) == pixcake::ResultCode::StateConflict);
  EXPECT(document.revision() == 2 && files.files[kPath] == saved);
}

int main() {
  test_local_masks_travel_with_a_preset();
  test_manage_infer_and_codec();
  test_c31_preset_apply_validates_through_capability_modules();
  test_c44_b04_b20_b21_name_counts_characters_not_bytes();
  test_c43_type_and_module_must_match();
  test_overwrite_and_import_do_not_change_old_rows();
  test_c43_color_replaces_and_ai_color_stacks();
  if (g_failures != 0) {
    std::fprintf(stderr, "%d assertion(s) failed\n", g_failures);
    return 1;
  }
  return 0;
}
