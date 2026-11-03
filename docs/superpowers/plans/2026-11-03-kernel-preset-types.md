# 预设四种类型 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在内存里保存四种预设，按类型替换或叠加到已提交的参数文档上，并在覆盖已有色彩调整前给出警告事实。

**Architecture:** `kernel/preset` 只认识架构第 7.3 节的能力标识和模块。它决定删哪些能力、写入哪些片段。`kernel/editdoc` 负责把这一组替换写成下一次修订，并用现有的五步替换写参数文档。预设自己的清单不落盘：边界清单只规定了精修文档的字节布局，没有规定预设文件怎么排。

**Tech Stack:** C++17、CMake、CTest、系统没有新增依赖。测试是独立可执行文件，不引入第三方测试框架。

## Global Constraints

- 预设只有 `色彩`、`AI 色彩`、`人像`、`组合` 四种。同类型后者替换前者，不同类型叠加。组合按它列出的模块分别替换（需求第 9 节、C-43）。
- 套用含色彩模块的预设时，若文档里已经有色彩模块的片段，先给出「将覆盖色彩」这个事实。壳层负责显示。内核给出事实，不弹窗（需求第 9 节、架构第 6.9 节）。
- 名称按用户可见字符计数，不用 UTF-8 字节数。正好 20 个允许，21 个拒绝，已有预设不改。空名称或只有空白拒绝（C-44、B-04、B-20、B-21）。空白只算 U+0009、U+000A、U+000D、U+0020。文档没有把全角空格列进空白。
- 不能解码成 UTF-8 的名称没有「用户可见字符」个数，拒绝这一次，已有预设不改。
- 能力标识到模块的对应以架构第 7.3 节的表为准。`detail` 属于色彩。`pet` 和 `manual.*` 不在四种预设的包含范围里，带这些片段的预设拒绝保存。不在表里的标识拒绝保存，不猜它属于哪一类。
- 组合的模块只能从 `人像`、`色彩`、`AI 色彩`、`图像`、`衣物`、`裁剪` 里选，并且至少两个。片段的模块必须落在这次列出的模块里。
- 更高版本、来源已删除：导入失败，清单保持调用前的内容（C-44）。这两件事由调用方用两个布尔值传入。口令的字节格式文档没写，这一步不解析口令。
- 预设编号为空则拒绝。信封上的预设引用是编号，空编号不能当作引用。编号已存在且没有选择覆盖：拒绝，旧预设不改。选择覆盖但编号不存在：拒绝。覆盖时校验失败：旧预设不改（需求第 9 节「覆盖旧预设」）。
- 组合的模块列表有重复项时拒绝。单类型预设的 `modules` 必须是空的，模块由类型决定。
- 套用时修订号必须等于文档当前修订号。写盘失败时内存修订号和正式文件都保持调用前（B-73、B-111）。
- 不认识的非片段记录原样保留。预设引用列表这一步不改：引用没有类型，文档没写替换时要删掉哪一条。
- `kernel/` 不包含 Qt 头，不包含 `sqlite3.h`。不调用 `system`。

本计划明确不做：口令编码、`.xmp`、分类拖拽、按名字搜索、快捷键、命令入口、筛选、历史接线、账本、导出队列、Qt 壳、`kernel-cli`。预设清单不写文件。T-12、T-16、T-17、T-20、T-21 保持未实现。

---

### Task 1: 一次替换一组片段并写成下一修订

**Files:**
- Modify: `kernel/editdoc/include/pixcake/editdoc/edit_document.hpp`
- Modify: `kernel/editdoc/src/edit_document.cpp`
- Modify: `tests/kernel/editdoc/test_edit_document.cpp`

**Interfaces:**
- Consumes: `EditDocument::store_fragment`、`revision`、`encode`、`atomic_replace`
- Produces: `EditDocument::commit_fragment_replacement(expected_revision, remove_capability_ids, replacements, files, path)`

- [x] **Step 1: Write the failing test**

在 `tests/kernel/editdoc/test_edit_document.cpp` 增加下面这个测试，并从 `main` 调用。

```cpp
void test_fragment_replacement_keeps_other_records() {
  auto document = *pixcake::EditDocument::create("img-1");
  document.store_fragment("color.global", 1, {1, 2});
  document.store_fragment("color.local", 1, {3});
  document.store_fragment("portrait.blemish", 1, {4});
  document.append_raw_record(40, {9, 9});
  pixcake::test::FakeFileSystem files;
  EXPECT(document.write_snapshot(files, kPath) == pixcake::ResultCode::Ok);

  std::vector<pixcake::FragmentView> replacements(1);
  replacements[0].capability_id = "color.global";
  replacements[0].version = 1;
  replacements[0].inner = {7, 7};
  EXPECT(document.commit_fragment_replacement(0, {"color.global", "color.local"}, replacements, files,
                                              kPath) == pixcake::ResultCode::Ok);
  EXPECT(document.revision() == 1);
  EXPECT(document.fragment("color.global")->inner == std::vector<std::uint8_t>({7, 7}));
  EXPECT(!document.fragment("color.local").has_value());
  EXPECT(document.fragment("portrait.blemish")->inner == std::vector<std::uint8_t>({4}));
  EXPECT(document.payloads_of_type(40).size() == 1);

  const auto saved = files.files[kPath];
  EXPECT(document.commit_fragment_replacement(0, {"portrait.blemish"}, {}, files, kPath) ==
         pixcake::ResultCode::StateConflict);
  EXPECT(document.revision() == 1);
  EXPECT(files.files[kPath] == saved);

  files.write_error = pixcake::ResultCode::DiskFull;
  EXPECT(document.commit_fragment_replacement(1, {"portrait.blemish"}, {}, files, kPath) ==
         pixcake::ResultCode::DiskFull);
  EXPECT(document.revision() == 1);
  EXPECT(document.fragment("portrait.blemish").has_value());
  EXPECT(files.files[kPath] == saved);
}
```

- [x] **Step 2: Run test to verify it fails**

Run: `cmake --build build --target test_edit_document && ctest --test-dir build -R '^test_edit_document$' --output-on-failure`

Expected: 编译失败，因为还没有 `commit_fragment_replacement`。

- [x] **Step 3: Write minimal implementation**

在 `edit_document.hpp` 的 `store_fragment` 声明后面增加：

```cpp
  // 删掉列出的能力片段，再写入新片段。修订号加一。写盘失败时调用方看到的文档仍是上一份。
  ResultCode commit_fragment_replacement(std::uint64_t expected_revision,
                                         const std::vector<std::string>& remove_capability_ids,
                                         const std::vector<FragmentView>& replacements,
                                         FileSystem& files, const std::string& path);
```

在 `edit_document.cpp` 里实现。修订号不一致、或某个新片段的能力标识为空：返回状态冲突，不写文件。解析不出的片段记录保留。写盘成功后才替换内存里的文档。

```cpp
ResultCode EditDocument::commit_fragment_replacement(
    std::uint64_t expected_revision, const std::vector<std::string>& remove_capability_ids,
    const std::vector<FragmentView>& replacements, FileSystem& files, const std::string& path) {
  if (expected_revision != revision()) {
    return ResultCode::StateConflict;
  }
  for (const auto& fragment : replacements) {
    if (fragment.capability_id.empty()) {
      return ResultCode::StateConflict;
    }
  }

  EditDocument next = *this;
  next.clear_recovered_flag();
  next.records_.erase(std::remove_if(next.records_.begin(), next.records_.end(),
                                     [&](const Record& record) {
                                       if (record.type != kRecordFragment) {
                                         return false;
                                       }
                                       const auto parsed = parse_fragment(record.payload);
                                       if (!parsed) {
                                         return false;
                                       }
                                       return std::find(remove_capability_ids.begin(),
                                                        remove_capability_ids.end(),
                                                        parsed->id) != remove_capability_ids.end();
                                     }),
                      next.records_.end());
  for (const auto& fragment : replacements) {
    next.store_fragment(fragment.capability_id, fragment.version, fragment.inner);
  }
  next.set_revision(revision() + 1);
  const ResultCode saved = atomic_replace(files, path, next.encode());
  if (saved != ResultCode::Ok) {
    return saved;
  }
  *this = std::move(next);
  return ResultCode::Ok;
}
```

- [x] **Step 4: Run test to verify it passes**

Expected: `test_edit_document` 通过。

- [x] **Step 5: Commit**

```bash
git add kernel/editdoc tests/kernel/editdoc/test_edit_document.cpp docs/superpowers/plans/2026-11-03-kernel-preset-types.md
git commit -m "$(cat <<'EOF'
feat: replace a set of edit fragments as one revision

EOF
)"
```

### Task 2: 四种预设的名称、替换和覆盖警告

**Files:**
- Create: `kernel/preset/include/pixcake/preset/preset_library.hpp`
- Create: `kernel/preset/src/preset_library.cpp`
- Create: `kernel/preset/CMakeLists.txt`
- Modify: `CMakeLists.txt`
- Create: `tests/kernel/preset/test_preset_library.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `EditDocument::commit_fragment_replacement`、`fragment`
- Produces: `Preset`、`PresetCheck`、`PresetLibrary::save`、`import_preset`、`check_apply`、`apply`

`CMakeLists.txt` 在 `kernel/history` 后增加 `add_subdirectory(kernel/preset)`。

`kernel/preset/CMakeLists.txt`：

```cmake
add_library(pixcake_preset
  src/preset_library.cpp
)
target_include_directories(pixcake_preset PUBLIC include)
target_link_libraries(pixcake_preset PUBLIC pixcake_editdoc pixcake_warnings)
```

`tests/CMakeLists.txt` 增加：

```cmake
add_executable(test_preset_library kernel/preset/test_preset_library.cpp)
target_include_directories(test_preset_library PRIVATE ${CMAKE_SOURCE_DIR}/tests)
target_link_libraries(test_preset_library PRIVATE pixcake_preset pixcake_warnings)
add_test(NAME test_preset_library COMMAND test_preset_library)
```

- [ ] **Step 1: Write the failing test**

把下面的文件写成 `tests/kernel/preset/test_preset_library.cpp`。

```cpp
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
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake -S . -B build && cmake --build build --target test_preset_library && ctest --test-dir build -R '^test_preset_library$' --output-on-failure`

Expected: 编译失败，因为还没有 `PresetLibrary`。

- [ ] **Step 3: Write minimal implementation**

`preset_library.hpp` 声明 `kPresetColor`、`kPresetAiColor`、`kPresetPortrait`、`kPresetCombo`、`PresetFragment`、`Preset`、`PresetCheck` 和 `PresetLibrary`。方法就是这一步测试调用的四个：`save`、`import_preset`、`check_apply`、`apply`，外加 `presets()`。

`preset_library.cpp` 里的模块表只放架构第 7.3 节这些行：

| 能力标识 | 模块 |
| --- | --- |
| `color.match`、`color.style` | AI 色彩 |
| `color.global`、`color.local`、`color.post`、`detail` | 色彩 |
| `portrait.blemish`、`portrait.skin`、`portrait.reshape`、`portrait.feature`、`portrait.body` | 人像 |
| `image.background`、`image.idphoto`、`image.sky` | 图像 |
| `clothes` | 衣物 |
| `crop` | 裁剪 |

校验顺序固定：

1. 编号为空，返回状态冲突。
2. 名称按 UTF-8 解码成标量值。解码失败、一个标量值都没有、全部是 U+0009/U+000A/U+000D/U+0020、或个数大于 20，返回状态冲突。
3. 片段的能力标识必须在上表里。
4. `色彩`、`AI 色彩`、`人像` 的 `modules` 必须为空，片段模块必须等于该类型。
5. `组合` 的 `modules` 至少两项、不能重复，每项只能是 `人像`、`色彩`、`AI 色彩`、`图像`、`衣物`、`裁剪`。每个片段的模块必须出现在这个列表里。
6. 其他类型返回状态冲突。

`save` 在校验通过之后才改清单。编号已存在且 `overwrite_existing` 为假，或编号不存在且 `overwrite_existing` 为真，都返回状态冲突。`import_preset` 在 `source_version_newer` 或 `source_deleted` 为真时立刻返回状态冲突；否则调用 `save(preset, false)`。

`check_apply` 不改文档。校验失败时 `overwrite_color` 为假。校验通过，且这次要替换的模块含 `色彩`，文档里 `color.global`、`color.local`、`color.post`、`detail` 任一已有片段，则 `overwrite_color` 为真。

`apply` 使用同一套校验。删除列表是文档里属于被替换模块的已知能力。写入列表是预设片段。调用 `commit_fragment_replacement`。不调用 `add_preset_ref`。

头文件：

```cpp
#pragma once

#include "pixcake/editdoc/edit_document.hpp"
#include "pixcake/persist/file_system.hpp"
#include "pixcake/support/result_code.hpp"

#include <string>
#include <vector>

namespace pixcake {

inline constexpr char kPresetColor[] = "色彩";
inline constexpr char kPresetAiColor[] = "AI 色彩";
inline constexpr char kPresetPortrait[] = "人像";
inline constexpr char kPresetCombo[] = "组合";

struct PresetFragment {
  std::string capability_id;
  std::uint32_t version = 1;
  std::vector<std::uint8_t> inner;
};

struct Preset {
  std::string id;
  std::string name;
  std::string category;
  std::string type;
  std::vector<std::string> modules;
  std::vector<PresetFragment> fragments;
};

struct PresetCheck {
  ResultCode code = ResultCode::Ok;
  bool overwrite_color = false;
};

class PresetLibrary {
 public:
  ResultCode save(Preset preset, bool overwrite_existing);
  ResultCode import_preset(Preset preset, bool source_version_newer, bool source_deleted);
  PresetCheck check_apply(const EditDocument& document, const Preset& preset) const;
  ResultCode apply(EditDocument& document, const Preset& preset, std::uint64_t expected_revision,
                   FileSystem& files, const std::string& path);
  const std::vector<Preset>& presets() const;

 private:
  std::vector<Preset> presets_;
};

}  // namespace pixcake
```

`preset_library.cpp` 使用下面的结构。模块表、UTF-8 解码和校验都放在匿名命名空间里。

```cpp
namespace {

struct ModuleRow {
  const char* capability_id;
  const char* module;
};

const ModuleRow kModules[] = {
    {"color.match", "AI 色彩"}, {"color.style", "AI 色彩"},
    {"color.global", "色彩"},   {"color.local", "色彩"},
    {"color.post", "色彩"},     {"detail", "色彩"},
    {"portrait.blemish", "人像"}, {"portrait.skin", "人像"},
    {"portrait.reshape", "人像"}, {"portrait.feature", "人像"},
    {"portrait.body", "人像"},  {"image.background", "图像"},
    {"image.idphoto", "图像"},  {"image.sky", "图像"},
    {"clothes", "衣物"},        {"crop", "裁剪"},
};

const char* module_of(const std::string& capability_id) {
  for (const auto& row : kModules) {
    if (capability_id == row.capability_id) {
      return row.module;
    }
  }
  return nullptr;
}

std::optional<std::vector<char32_t>> code_points(const std::string& text) {
  std::vector<char32_t> points;
  std::size_t index = 0;
  while (index < text.size()) {
    const auto lead = static_cast<unsigned char>(text[index]);
    std::size_t need = 1;
    char32_t value = lead;
    if (lead < 0x80) {
      need = 1;
    } else if ((lead & 0xE0) == 0xC0) {
      need = 2;
      value = lead & 0x1F;
    } else if ((lead & 0xF0) == 0xE0) {
      need = 3;
      value = lead & 0x0F;
    } else if ((lead & 0xF8) == 0xF0) {
      need = 4;
      value = lead & 0x07;
    } else {
      return std::nullopt;
    }
    if (index + need > text.size()) {
      return std::nullopt;
    }
    for (std::size_t extra = 1; extra < need; ++extra) {
      const auto next = static_cast<unsigned char>(text[index + extra]);
      if ((next & 0xC0) != 0x80) {
        return std::nullopt;
      }
      value = (value << 6) | (next & 0x3F);
    }
    points.push_back(value);
    index += need;
  }
  return points;
}

bool blank_points(const std::vector<char32_t>& points) {
  if (points.empty()) {
    return true;
  }
  for (char32_t point : points) {
    if (point != 0x09 && point != 0x0A && point != 0x0D && point != 0x20) {
      return false;
    }
  }
  return true;
}

const char* kComboModules[] = {"人像", "色彩", "AI 色彩", "图像", "衣物", "裁剪"};

ResultCode validate(const Preset& preset) {
  if (preset.id.empty()) {
    return ResultCode::StateConflict;
  }
  const auto points = code_points(preset.name);
  if (!points || blank_points(*points) || points->size() > 20) {
    return ResultCode::StateConflict;
  }
  for (const auto& fragment : preset.fragments) {
    if (module_of(fragment.capability_id) == nullptr) {
      return ResultCode::StateConflict;
    }
  }
  if (preset.type == kPresetColor || preset.type == kPresetAiColor || preset.type == kPresetPortrait) {
    if (!preset.modules.empty()) {
      return ResultCode::StateConflict;
    }
    for (const auto& fragment : preset.fragments) {
      if (std::string(module_of(fragment.capability_id)) != preset.type) {
        return ResultCode::StateConflict;
      }
    }
    return ResultCode::Ok;
  }
  if (preset.type != kPresetCombo || preset.modules.size() < 2) {
    return ResultCode::StateConflict;
  }
  for (std::size_t i = 0; i < preset.modules.size(); ++i) {
    bool allowed = false;
    for (const char* name : kComboModules) {
      if (preset.modules[i] == name) {
        allowed = true;
      }
    }
    if (!allowed) {
      return ResultCode::StateConflict;
    }
    for (std::size_t j = i + 1; j < preset.modules.size(); ++j) {
      if (preset.modules[i] == preset.modules[j]) {
        return ResultCode::StateConflict;
      }
    }
  }
  for (const auto& fragment : preset.fragments) {
    bool listed = false;
    for (const auto& module : preset.modules) {
      if (module == module_of(fragment.capability_id)) {
        listed = true;
      }
    }
    if (!listed) {
      return ResultCode::StateConflict;
    }
  }
  return ResultCode::Ok;
}

std::vector<std::string> replaced_modules(const Preset& preset) {
  if (preset.type == kPresetCombo) {
    return preset.modules;
  }
  return {preset.type};
}

}  // namespace
```

`save`、`check_apply` 和 `apply` 都先调用 `validate`。`check_apply` 在校验成功后，只有 `replaced_modules` 含有 `色彩`，并且 `color.global`、`color.local`、`color.post`、`detail` 里至少有一个 `fragment()` 有值，才把 `overwrite_color` 设为真。`apply` 用这些已知能力里、模块落在替换列表中的标识组成删除列表。

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build --target test_preset_library test_edit_document && ctest --test-dir build --output-on-failure`

Expected: 全量测试通过，其中包含 `test_preset_library`、`test_edit_document`、`test_history` 和 `arch_dependencies`。

- [ ] **Step 5: Commit**

```bash
git add kernel/preset tests/kernel/preset CMakeLists.txt tests/CMakeLists.txt docs/superpowers/plans/2026-11-03-kernel-preset-types.md
git commit -m "$(cat <<'EOF'
feat: apply the four preset types onto a committed document

EOF
)"
```

执行时只在本地提交，不推远端，不建 Pull Request。
