
#include "pixcake/preset/preset_library.hpp"
#include "support/fake_file_system.hpp"

#include <cstdio>
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

const char* kPath = "docs/img.pxedit";

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

void test_c44_name_counts_characters_not_bytes() {
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
  mixed.fragments.push_back(pixcake::PresetFragment{"color.global", 1, {1}});
  EXPECT(library.save(mixed, false) == pixcake::ResultCode::StateConflict);

  auto pet = portrait_named("pet", han(1));
  pet.type = pixcake::kPresetCombo;
  pet.modules = {"人像", "色彩"};
  pet.fragments.push_back(pixcake::PresetFragment{"pet", 1, {1}});
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
  document.store_fragment("color.global", 1, {1});
  document.store_fragment("color.local", 1, {2});
  document.store_fragment("portrait.blemish", 1, {3});
  document.store_fragment("color.match", 1, {4});
  pixcake::test::FakeFileSystem files;
  EXPECT(document.write_snapshot(files, kPath) == pixcake::ResultCode::Ok);

  pixcake::Preset color;
  color.id = "color";
  color.name = han(1);
  color.type = pixcake::kPresetColor;
  color.fragments.push_back(pixcake::PresetFragment{"color.global", 1, {9}});

  pixcake::PresetLibrary library;
  const auto warning = library.check_apply(document, color);
  EXPECT(warning.code == pixcake::ResultCode::Ok);
  EXPECT(warning.overwrite_color);

  pixcake::Preset ai;
  ai.id = "ai";
  ai.name = han(1);
  ai.type = pixcake::kPresetAiColor;
  ai.fragments.push_back(pixcake::PresetFragment{"color.style", 1, {8}});
  const auto ai_check = library.check_apply(document, ai);
  EXPECT(ai_check.code == pixcake::ResultCode::Ok);
  EXPECT(!ai_check.overwrite_color);

  EXPECT(library.apply(document, color, 0, files, kPath) == pixcake::ResultCode::Ok);
  EXPECT(document.revision() == 1);
  EXPECT(document.fragment("color.global")->inner == std::vector<std::uint8_t>({9}));
  EXPECT(!document.fragment("color.local").has_value());
  EXPECT(document.fragment("portrait.blemish")->inner == std::vector<std::uint8_t>({3}));
  EXPECT(document.fragment("color.match")->inner == std::vector<std::uint8_t>({4}));
  EXPECT(document.preset_refs().empty());

  EXPECT(library.apply(document, ai, 1, files, kPath) == pixcake::ResultCode::Ok);
  EXPECT(document.fragment("color.global")->inner == std::vector<std::uint8_t>({9}));
  EXPECT(document.fragment("color.style")->inner == std::vector<std::uint8_t>({8}));

  const auto saved = files.files[kPath];
  EXPECT(library.apply(document, color, 0, files, kPath) == pixcake::ResultCode::StateConflict);
  EXPECT(document.revision() == 2);
  EXPECT(files.files[kPath] == saved);

  files.write_error = pixcake::ResultCode::DiskFull;
  EXPECT(library.apply(document, color, 2, files, kPath) == pixcake::ResultCode::DiskFull);
  EXPECT(document.revision() == 2);
  EXPECT(files.files[kPath] == saved);
}

}  // namespace

int main() {
  test_c44_name_counts_characters_not_bytes();
  test_c43_type_and_module_must_match();
  test_overwrite_and_import_do_not_change_old_rows();
  test_c43_color_replaces_and_ai_color_stacks();
  if (g_failures != 0) {
    std::fprintf(stderr, "%d assertion(s) failed\n", g_failures);
    return 1;
  }
  return 0;
}
