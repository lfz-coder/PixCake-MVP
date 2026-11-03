# 批量同步勾选片段 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 按勾选的能力把来源图上已提交的片段同步到多张目标图，复制和粘贴则不经过勾选框，直接搬运当前已提交片段。

**Architecture:** `kernel/capability` 声明每个架构第 7.3 节能力能否批量同步，以及不能同步的原因。局部追色挂在追色能力上。`kernel/mask` 声明画笔蒙版不可同步。`kernel/sync` 只读取这些属性来组勾选树、拒绝禁用项，并把允许的能力片段交给 `EditDocument::commit_fragment_replacement`。一项失败不回滚已经成功的目标。

**Tech Stack:** C++17、CMake、CTest、系统没有新增依赖。测试是独立可执行文件，不引入第三方测试框架。

## Global Constraints

- 批量同步按能力勾选片段。复制效果和粘贴效果不经过勾选框，搬运当前已提交片段（需求第 18 节、契约 C-45）。
- 不能同步的项由自身属性标出，同步模块只展示原因。同步代码禁止出现「画笔」和「局部追色」（契约 C-45、架构第 6.6 节、第 6.9 节）。
- 局部追色不能批量同步，由追色能力声明。勾选树里禁用，原因用需求第 10.1 节的「局部追色不能批量同步」。勾了它，这次同步任何目标都不写（边界 B-37）。
- 自定义画笔蒙版在勾选树里禁用，原因用需求第 10.4 节的「自定义里的画笔不能同步」。属性放在蒙版模块。勾了它，这次同步任何目标都不写（需求第 18 节、边界 B-37）。
- `color.match` 和 `color.style` 不能把数值原样抄到别的图。原因用需求第 10.1 节的「同步时按每张图重新计算，不是把数值原样抄过去」。重算函数没有文档，这一步不编造重算结果，这两项禁用。勾了它们，这次同步任何目标都不写。
- 复制和粘贴仍搬运这两段已提交字节。需求把「按每张图重新计算」写在批量同步和保存预设上，没有写在复制粘贴上（契约 C-45）。
- 勾选树有两级：一级是架构第 7.3 节的模块名，二级是该表的能力。`detail` 的显示名用同一节里的「细节」，因为它写明同步时仍可作为二级项勾选。其余二级名称用该表「对应需求」列。
- 蒙版字节布局没有文档。这一步不从 `color.local` 里拆画笔，也不做蒙版类型的叠加或替换，也不按「同一人」对齐。「所有人」不要求是同一个人这句话因此不在本计划里实现。
- 勾选必须非空、不能重复，并且每一项都是树上已启用、带能力标识的二级项。否则状态冲突，任何目标都不写。
- 来源没有这段能力时，目标上删掉这段。来源有，就写成来源这段。其他能力、不认识的记录和预设引用不动。
- 目标路径与来源路径相同、路径为空、文档或文件系统指针为空：这一项状态冲突，不写这一项。其他目标继续（边界 B-68）。
- 目标修订号必须等于该文档当前修订号。写盘失败时这一项目标的内存和正式文件保持调用前。已经成功的目标不回滚。重试只重试失败项（边界 B-68、B-73、B-111）。
- 粘贴的片段列表为空：不写文件，修订号不变。粘贴不读取预览副本。
- `kernel/` 不包含 Qt 头，不包含 `sqlite3.h`。不调用 `system`。

本计划明确不做：蒙版字节拆分、叠加或替换、按每张图重算 AI 色彩、人物对齐、快捷键、弹窗、命令入口、历史接线、预设落盘、筛选、账本、导出队列、Qt 壳、`kernel-cli`。

---

### Task 1: 能力和蒙版自己声明可否同步

**Files:**
- Create: `kernel/capability/include/pixcake/capability/capability_sync.hpp`
- Create: `kernel/capability/src/capability_sync.cpp`
- Create: `kernel/capability/CMakeLists.txt`
- Create: `kernel/mask/include/pixcake/mask/mask_sync.hpp`
- Create: `kernel/mask/src/mask_sync.cpp`
- Create: `kernel/mask/CMakeLists.txt`
- Create: `kernel/sync/include/pixcake/sync/sync_batch.hpp`
- Create: `kernel/sync/src/sync_batch.cpp`
- Create: `kernel/sync/CMakeLists.txt`
- Create: `tests/kernel/sync/test_sync.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`
- Modify: `tests/arch/check_dependencies.cmake`

**Interfaces:**
- Consumes: 架构第 7.3 节的能力标识和模块名
- Produces: `capability_sync_rows()`、`mask_sync_rows()`、`sync_tree()`、`SyncItem`

- [x] **Step 1: Write the failing test**

把 `tests/kernel/sync/test_sync.cpp` 写成下面的文件。在根 `CMakeLists.txt` 的 `kernel/preset` 后面加入三个子目录。在 `tests/CMakeLists.txt` 的 `test_preset_library` 后面加入 `test_sync`。在 `tests/arch/check_dependencies.cmake` 末尾、`arch dependencies ok` 之前加入规则 11：扫描 `kernel/sync`，禁止出现 `画笔` 和 `局部追色`。

```cpp
#include "pixcake/sync/sync_batch.hpp"

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

const pixcake::SyncItem* find_item(const std::vector<pixcake::SyncItem>& tree, const char* id) {
  for (const auto& item : tree) {
    if (item.id == id) {
      return &item;
    }
  }
  return nullptr;
}

void test_tree_reads_attributes_from_owners() {
  const auto tree = pixcake::sync_tree();
  EXPECT(tree.size() == 22);

  const auto* match = find_item(tree, "color.match");
  EXPECT(match != nullptr);
  EXPECT(match->module_label == "AI 色彩");
  EXPECT(match->label == "AI 追色");
  EXPECT(match->capability_id == "color.match");
  EXPECT(!match->enabled);
  EXPECT(match->blocked_reason == "同步时按每张图重新计算，不是把数值原样抄过去");

  const auto* local = find_item(tree, "color.match.local");
  EXPECT(local != nullptr);
  EXPECT(local->module_label == "AI 色彩");
  EXPECT(local->label == "局部追色");
  EXPECT(local->capability_id.empty());
  EXPECT(!local->enabled);
  EXPECT(local->blocked_reason == "局部追色不能批量同步");

  const auto* style = find_item(tree, "color.style");
  EXPECT(style != nullptr);
  EXPECT(!style->enabled);
  EXPECT(style->blocked_reason == match->blocked_reason);

  const auto* detail = find_item(tree, "detail");
  EXPECT(detail != nullptr);
  EXPECT(detail->module_label == "色彩");
  EXPECT(detail->label == "细节");
  EXPECT(detail->enabled);
  EXPECT(detail->capability_id == "detail");

  const auto* brush = find_item(tree, "mask.brush");
  EXPECT(brush != nullptr);
  EXPECT(brush->module_label == "色彩");
  EXPECT(brush->label == "自定义画笔蒙版");
  EXPECT(brush->capability_id.empty());
  EXPECT(!brush->enabled);
  EXPECT(brush->blocked_reason == "自定义里的画笔不能同步");

  const char* enabled[] = {"color.global",     "color.local",      "color.post",
                           "portrait.blemish",  "portrait.skin",    "portrait.reshape",
                           "portrait.feature",  "portrait.body",    "image.background",
                           "image.idphoto",     "image.sky",        "clothes",
                           "pet",               "crop",             "manual.edge",
                           "manual.liquify",    "manual.heal"};
  for (const char* id : enabled) {
    const auto* item = find_item(tree, id);
    EXPECT(item != nullptr);
    EXPECT(item->enabled);
    EXPECT(item->capability_id == id);
    EXPECT(item->blocked_reason.empty());
  }
  EXPECT(find_item(tree, "manual.edge")->module_label == "手动");
  EXPECT(find_item(tree, "pet")->module_label == "宠物");
}

}  // namespace

int main() {
  test_tree_reads_attributes_from_owners();
  if (g_failures != 0) {
    std::fprintf(stderr, "%d failure(s)\n", g_failures);
    return 1;
  }
  return 0;
}
```

根 `CMakeLists.txt` 增加：

```cmake
add_subdirectory(kernel/capability)
add_subdirectory(kernel/mask)
add_subdirectory(kernel/sync)
```

`tests/CMakeLists.txt` 增加：

```cmake
add_executable(test_sync kernel/sync/test_sync.cpp)
target_include_directories(test_sync PRIVATE ${CMAKE_SOURCE_DIR}/tests)
target_link_libraries(test_sync PRIVATE pixcake_sync pixcake_warnings)
add_test(NAME test_sync COMMAND test_sync)
```

`tests/arch/check_dependencies.cmake` 增加：

```cmake
# 11. 同步模块不写死画笔或局部追色。原因只放在能力和蒙版的声明里。
scan("${SOURCE_DIR}/kernel/sync" "rule 11" "画笔" "局部追色")
```

三个 `CMakeLists.txt` 先写成会参与编译的目标。`kernel/sync/src/sync_batch.cpp` 此时可以是空文件，用来让编译停在缺少 `sync_tree`。

- [x] **Step 2: Run test to verify it fails**

Run: `cmake -S . -B build && cmake --build build --target test_sync`

Expected: 编译失败，因为还没有 `sync_tree`。实际失败是找不到 `sync_batch.hpp`。

- [x] **Step 3: Write minimal implementation**

`kernel/capability/include/pixcake/capability/capability_sync.hpp`：

```cpp
#pragma once

#include <string>
#include <vector>

namespace pixcake {

struct CapabilitySyncRow {
  std::string id;
  std::string module_label;
  std::string label;
  std::string capability_id;
  bool enabled = false;
  std::string blocked_reason;
};

std::vector<CapabilitySyncRow> capability_sync_rows();

}  // namespace pixcake
```

`kernel/mask/include/pixcake/mask/mask_sync.hpp`：

```cpp
#pragma once

#include <string>
#include <vector>

namespace pixcake {

struct MaskSyncRow {
  std::string id;
  std::string module_label;
  std::string label;
  bool can_batch_sync = false;
  std::string blocked_reason;
};

std::vector<MaskSyncRow> mask_sync_rows();

}  // namespace pixcake
```

`kernel/sync/include/pixcake/sync/sync_batch.hpp` 先只放树需要的类型。`SyncTarget`、`apply_checked`、`retry_failed`、`copy_committed`、`paste_committed` 留到 Task 2，避免这一步出现还没被测试调用的入口。

```cpp
#pragma once

#include <string>
#include <vector>

namespace pixcake {

struct SyncItem {
  std::string id;
  std::string module_label;
  std::string label;
  std::string capability_id;
  bool enabled = false;
  std::string blocked_reason;
};

std::vector<SyncItem> sync_tree();

}  // namespace pixcake
```

能力表按架构第 7.3 节的顺序。`color.match` 后面紧跟追色能力自己声明的 `color.match.local`。`color.match` 与 `color.style` 的 `enabled` 为假，原因是「同步时按每张图重新计算，不是把数值原样抄过去」。`detail` 的 `label` 是「细节」。其余能力 `enabled` 为真，`capability_id` 等于 `id`，原因为空。

蒙版表只有一行：`mask.brush`，模块「色彩」，名称「自定义画笔蒙版」，`can_batch_sync` 为假，原因「自定义里的画笔不能同步」。

`sync_tree` 先追加能力行，再追加蒙版行。不写「画笔」或「局部追色」这两个词。

- [x] **Step 4: Run test to verify it passes**

Run: `cmake --build build --target test_sync && ctest --test-dir build -R '^(test_sync|arch_dependencies)$' --output-on-failure`

Expected: 这两项通过。

- [x] **Step 5: Commit**

```bash
git add kernel/capability kernel/mask kernel/sync tests/kernel/sync CMakeLists.txt tests/CMakeLists.txt tests/arch/check_dependencies.cmake docs/superpowers/plans/2026-11-03-kernel-sync-checked-fragments.md
git commit -m "$(cat <<'EOF'
feat: declare which edits can be batch synced

EOF
)"
```

执行时只在本地提交，不推远端，不建 Pull Request。

---

### Task 2: 勾选同步与复制粘贴已提交片段

**Files:**
- Modify: `kernel/editdoc/include/pixcake/editdoc/edit_document.hpp`
- Modify: `kernel/editdoc/src/edit_document.cpp`
- Modify: `kernel/sync/include/pixcake/sync/sync_batch.hpp`
- Modify: `kernel/sync/src/sync_batch.cpp`
- Modify: `tests/kernel/sync/test_sync.cpp`

**Interfaces:**
- Consumes: `sync_tree()`、`EditDocument::fragment`、`EditDocument::commit_fragment_replacement`
- Produces: `EditDocument::fragments()`、`apply_checked`、`retry_failed`、`copy_committed`、`paste_committed`

- [x] **Step 1: Write the failing test**

在 `test_sync.cpp` 增加下面的测试，并从 `main` 调用。补上 `pixcake/editdoc/edit_document.hpp` 和 `support/fake_file_system.hpp`。

```cpp
pixcake::SyncTarget target_of(pixcake::EditDocument& document, pixcake::test::FakeFileSystem& files,
                              const char* path) {
  pixcake::SyncTarget target;
  target.document = &document;
  target.expected_revision = document.revision();
  target.files = &files;
  target.path = path;
  return target;
}

void test_checked_sync_copies_only_selected_fragments() {
  auto source = *pixcake::EditDocument::create("source");
  source.store_fragment("portrait.blemish", 1, {1});
  source.store_fragment("color.local", 1, {2});
  source.store_fragment("detail", 1, {3});
  pixcake::test::FakeFileSystem source_files;
  EXPECT(source.write_snapshot(source_files, "docs/source.pxedit") == pixcake::ResultCode::Ok);

  auto first = *pixcake::EditDocument::create("first");
  first.store_fragment("color.local", 1, {9});
  first.store_fragment("portrait.blemish", 1, {8});
  first.append_raw_record(40, {4});
  first.add_preset_ref("preset-1");
  pixcake::test::FakeFileSystem first_files;
  EXPECT(first.write_snapshot(first_files, "docs/first.pxedit") == pixcake::ResultCode::Ok);
  auto second = *pixcake::EditDocument::create("second");
  second.store_fragment("portrait.blemish", 1, {7});
  pixcake::test::FakeFileSystem second_files;
  EXPECT(second.write_snapshot(second_files, "docs/second.pxedit") == pixcake::ResultCode::Ok);
  std::vector<pixcake::SyncTarget> targets = {
      target_of(first, first_files, "docs/first.pxedit"),
      target_of(second, second_files, "docs/second.pxedit")};
  const std::vector<std::string> checked = {"portrait.blemish", "detail"};
  const auto synced = pixcake::apply_checked(source, "docs/source.pxedit", checked, targets);
  EXPECT(synced.size() == 2);
  EXPECT(synced[0].code == pixcake::ResultCode::Ok);
  EXPECT(synced[1].code == pixcake::ResultCode::Ok);
  EXPECT(first.revision() == 1);
  EXPECT(first.fragment("portrait.blemish")->inner == std::vector<std::uint8_t>({1}));
  EXPECT(first.fragment("detail")->inner == std::vector<std::uint8_t>({3}));
  EXPECT(first.fragment("color.local")->inner == std::vector<std::uint8_t>({9}));
  EXPECT(first.payloads_of_type(40).size() == 1);
  EXPECT(first.preset_refs().size() == 1);
  EXPECT(second.fragment("portrait.blemish")->inner == std::vector<std::uint8_t>({1}));
  EXPECT(second.fragment("detail")->inner == std::vector<std::uint8_t>({3}));
  EXPECT(source.revision() == 0);
  const auto second_saved = second_files.files["docs/second.pxedit"];

  auto blocked = pixcake::apply_checked(source, "docs/source.pxedit", {"mask.brush"}, targets);
  EXPECT(blocked[0].code == pixcake::ResultCode::StateConflict);
  EXPECT(blocked[1].code == pixcake::ResultCode::StateConflict);
  EXPECT(first.revision() == 1);
  EXPECT(second_files.files["docs/second.pxedit"] == second_saved);

  second_files.write_error = pixcake::ResultCode::DiskFull;
  targets[0].expected_revision = first.revision();
  targets[1].expected_revision = second.revision();
  const auto failed = pixcake::apply_checked(source, "docs/source.pxedit", {"color.global"}, targets);
  EXPECT(failed[0].code == pixcake::ResultCode::Ok);
  EXPECT(failed[1].code == pixcake::ResultCode::DiskFull);
  EXPECT(second.revision() == 1);
  EXPECT(second_files.files["docs/second.pxedit"] == second_saved);

  second_files.write_error = pixcake::ResultCode::Ok;
  const auto retried =
      pixcake::retry_failed(source, "docs/source.pxedit", {"color.global"}, targets, failed);
  EXPECT(retried[0].code == pixcake::ResultCode::Ok);
  EXPECT(retried[1].code == pixcake::ResultCode::Ok);
  EXPECT(first.revision() == 2);
  EXPECT(second.revision() == 2);
  EXPECT(!second.fragment("color.global").has_value());
}

void test_copy_paste_moves_committed_fragments_without_a_tree() {
  auto source = *pixcake::EditDocument::create("source");
  source.store_fragment("color.match", 1, {5});
  source.store_fragment("portrait.blemish", 1, {6});
  source.append_raw_record(40, {1});
  EXPECT(source.set_preview("portrait.blemish", "degree", "10") == pixcake::ResultCode::Ok);
  const auto copied = pixcake::copy_committed(source);
  EXPECT(copied.fragments.size() == 2);
  EXPECT(copied.fragments[0].capability_id == "color.match");

  auto target = *pixcake::EditDocument::create("target");
  target.store_fragment("color.local", 1, {9});
  pixcake::test::FakeFileSystem files;
  EXPECT(target.write_snapshot(files, "docs/target.pxedit") == pixcake::ResultCode::Ok);
  EXPECT(pixcake::paste_committed(target, copied, 0, files, "docs/target.pxedit") ==
         pixcake::ResultCode::Ok);
  EXPECT(target.revision() == 1);
  EXPECT(target.fragment("color.match")->inner == std::vector<std::uint8_t>({5}));
  EXPECT(target.fragment("color.local")->inner == std::vector<std::uint8_t>({9}));
  EXPECT(target.payloads_of_type(40).empty());

  const auto saved = files.files["docs/target.pxedit"];
  EXPECT(pixcake::paste_committed(target, copied, 0, files, "docs/target.pxedit") ==
         pixcake::ResultCode::StateConflict);
  EXPECT(target.revision() == 1);
  EXPECT(files.files["docs/target.pxedit"] == saved);

  pixcake::EffectClipboard empty;
  EXPECT(pixcake::paste_committed(target, empty, 1, files, "docs/target.pxedit") ==
         pixcake::ResultCode::Ok);
  EXPECT(target.revision() == 1);
  EXPECT(files.files["docs/target.pxedit"] == saved);
}
```

同路径的目标要单独断言：来源路径等于该目标路径时，这一项是状态冲突，来源修订号不变；另一张不同路径的目标仍写成来源片段。空勾选、重复勾选、勾选 `color.match`，都在任何写盘之前让全部目标返回状态冲突。`previous` 的数量和目标数不同时，重试不写盘。粘贴遇到磁盘满时，目标修订号和正式文件保持调用前。

- [x] **Step 2: Run test to verify it fails**

Run: `cmake --build build --target test_sync`

Expected: 编译失败，因为还没有 `apply_checked`。

- [x] **Step 3: Write minimal implementation**

`EditDocument` 增加 `std::vector<FragmentView> fragments() const;`。按记录顺序返回能解析的片段。解析不出的片段记录不放进这个列表，文件里的原记录也不删。

`sync_batch.hpp` 增加：

```cpp
struct SyncTarget {
  EditDocument* document = nullptr;
  std::uint64_t expected_revision = 0;
  FileSystem* files = nullptr;
  std::string path;
};

struct SyncItemResult {
  std::string path;
  ResultCode code = ResultCode::Ok;
};

struct EffectClipboard {
  std::vector<FragmentView> fragments;
};

std::vector<SyncItemResult> apply_checked(const EditDocument& source, const std::string& source_path,
                                         const std::vector<std::string>& checked_ids,
                                         const std::vector<SyncTarget>& targets);
std::vector<SyncItemResult> retry_failed(const EditDocument& source, const std::string& source_path,
                                        const std::vector<std::string>& checked_ids,
                                        const std::vector<SyncTarget>& targets,
                                        const std::vector<SyncItemResult>& previous);
EffectClipboard copy_committed(const EditDocument& source);
ResultCode paste_committed(EditDocument& target, const EffectClipboard& clipboard,
                           std::uint64_t expected_revision, FileSystem& files, const std::string& path);
```

`apply_checked` 先检查勾选。不合法则每个目标都是状态冲突，不调用写盘。合法时按目标顺序调用 `commit_fragment_replacement`：删除列表是勾选能力，替换列表只含来源上存在的那些片段。

`retry_failed` 在 `previous.size()` 与目标数不同时，每个结果都是状态冲突，不写盘。相等时，上次结果为成功的目标保持成功且不再写。上次失败的目标才再次应用。

`copy_committed` 返回 `source.fragments()`。`paste_committed` 在列表为空时返回成功且不写。否则用列表里的能力标识做删除列表，用列表本身做替换。

- [x] **Step 4: Run test to verify it passes**

Run: `cmake --build build --target test_sync test_edit_document && ctest --test-dir build --output-on-failure`

Expected: 全量测试通过，其中包含 `test_sync`、`test_edit_document`、`test_preset_library` 和 `arch_dependencies`。

- [x] **Step 5: Commit**

```bash
git add kernel/editdoc kernel/sync tests/kernel/sync docs/superpowers/plans/2026-11-03-kernel-sync-checked-fragments.md
git commit -m "$(cat <<'EOF'
feat: sync checked fragments onto the selected photos

EOF
)"
```

执行时只在本地提交，不推远端，不建 Pull Request。
