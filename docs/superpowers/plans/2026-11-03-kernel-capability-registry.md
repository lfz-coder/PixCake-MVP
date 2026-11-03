# 精修能力注册表与管线元数据 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or **superpowers:executing-plans** to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 为阶段一建立精修能力的稳定目录、同步属性和默认管线顺序，让后续参数模式、预览和导出都从同一份内核元数据读取。

**Architecture:** 扩展现有 `kernel/capability`，把架构文档第 7.3 节的稳定能力标识、模块标签、展示名、注册状态和同步属性集中在一个只读注册表中；`kernel/sync` 只读取注册表，不再复制能力名称和同步规则。另提供架构第 7.4 节规定的阶段顺序数据，`detail` 只作为保留片段和管线阶段标记出现，不注册为本阶段可绘制能力。这个计划只做元数据和校验，不实现参数效果、渲染、网络账号或 Qt 页面。

**Tech Stack:** C++17、CMake、CTest。测试是独立可执行文件，不引入第三方测试框架。

## Global Constraints

- 用户可见的页面和工具展示以《工作区页面介绍》[https://www.pixcakeai.com/guide/32445](https://www.pixcakeai.com/guide/32445)（更新日期 2026-09-17）为准；该页点名的精修工具栏顺序是：显示比例调整、抓手、祛边、参考线、液化、污点修复、修补、仿制图章、智能消除、AI点修、重置效果、预设。
- 能力稳定标识、模块划分、接口责任、阶段顺序以 `docs/superpowers/specs/2026-11-02-pixcake-kernel-architecture-design.md` 第 7 节为准。
- 阶段一注册能力标识只能来自架构文档第 7.3 节：`color.match`、`color.style`、`color.global`、`color.local`、`color.post`、`portrait.blemish`、`portrait.skin`、`portrait.reshape`、`portrait.feature`、`portrait.body`、`image.background`、`image.idphoto`、`image.sky`、`clothes`、`pet`、`crop`、`manual.edge`、`manual.liquify`、`manual.heal`。`detail` 只保留旧片段和阶段标记，不注册绘制。
- `portrait.feature` 保持一个能力，展示名包含“表情、牙齿、眼睛、妆容、头发”，不拆成五个库。
- `color.match.local` 是同步树中的局部追色子项，不是新的稳定能力标识；它继续显示为不可批量同步，并且 `capability_id` 为空。
- `color.match` 和 `color.style` 不可批量同步，原因必须是“同步时按每张图重新计算，不是把数值原样抄过去”；局部追色原因必须是“局部追色不能批量同步”；自定义画笔蒙版原因必须是“自定义里的画笔不能同步”。
- 已注册能力的预设和批量同步属性由注册表提供；`detail`、局部追色和自定义画笔蒙版属于兼容性同步行，不是注册能力，保留在各自的兼容性边界中。不能在 `kernel/sync`、未来能力目录或 UI 里重新写已注册能力的标签和原因。
- 默认管线按架构第 7.4 节：`detail.geometry`、`detail.denoise`、`color.match`、`color.style`、`color.global`、`color.local`、人像能力、图像/衣物/宠物能力、`color.post`、手动能力、`crop`。`detail` 的两个阶段保留阶段号，但不产生绘制描述。
- 本计划不填写手册没有写出的滑杆范围、默认值、按钮、账号字段、网络协议或 GPU 方法体；不根据 Jarvis 9.3.0 页面目录添加额外入口。
- `kernel/` 不包含 Qt、OpenGL、HTTP、推理运行时；能力之间不互相 include；本计划不创建动态库。
- 只在本地提交，不推远端，不创建 Pull Request；不得把当前仓库已有的图层文档未提交修改放进提交。

本计划明确不做：参数模式和滑杆实现、能力编译公式、蒙版像素、人脸检测、GPU facade/backend、账号登录和短信、Qt 壳、AI 生成任务、预览帧和导出绘制。

---

### Task 1: 集中保存阶段一能力目录和同步属性

**Files:**
- Create: `kernel/capability/include/pixcake/capability/capability_registry.hpp`
- Create: `kernel/capability/src/capability_registry.cpp`
- Create: `tests/kernel/capability/test_capability_registry.cpp`
- Modify: `kernel/capability/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: C++17 `std::string`、`std::string_view`、`std::vector`
- Produces: `CapabilityDescriptor`、`CapabilityRegistry::phase_one()`、`CapabilityRegistry::find()`、`CapabilityRegistry::descriptors()`

- [x] **Step 1: Write the failing test**

在 `kernel/capability/CMakeLists.txt` 的源文件列表中加入 `src/capability_registry.cpp`。在 `tests/CMakeLists.txt` 的 `test_sync` 前加入 `test_capability_registry`，链接 `pixcake_capability` 和 `pixcake_warnings`。

创建 `tests/kernel/capability/test_capability_registry.cpp`：

```cpp
#include "pixcake/capability/capability_registry.hpp"

#include <cstdio>
#include <string>
#include <vector>

namespace {

int g_failures = 0;

void expect(bool condition, const char* expression, int line) {
  if (!condition) {
    std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, line, expression);
    ++g_failures;
  }
}

#define EXPECT(condition) expect(static_cast<bool>(condition), #condition, __LINE__)

}  // namespace

int main() {
  const pixcake::CapabilityRegistry registry = pixcake::CapabilityRegistry::phase_one();
  const std::vector<pixcake::CapabilityDescriptor> descriptors = registry.descriptors();

  const std::vector<std::string> expected_ids = {
      "color.match",       "color.style",       "color.global",     "color.local",
      "color.post",        "portrait.blemish",  "portrait.skin",    "portrait.reshape",
      "portrait.feature",  "portrait.body",     "image.background",  "image.idphoto",
      "image.sky",         "clothes",            "pet",               "crop",
      "manual.edge",       "manual.liquify",    "manual.heal"};
  EXPECT(descriptors.size() == expected_ids.size());
  for (std::size_t index = 0; index < expected_ids.size() && index < descriptors.size(); ++index) {
    EXPECT(descriptors[index].id == expected_ids[index]);
    EXPECT(descriptors[index].registered_for_drawing);
  }

  const pixcake::CapabilityDescriptor* feature = registry.find("portrait.feature");
  EXPECT(feature != nullptr);
  if (feature != nullptr) {
    EXPECT(feature->module_label == "人像");
    EXPECT(feature->label == "表情、牙齿、眼睛、妆容、头发");
    EXPECT(feature->can_use_in_preset);
    EXPECT(feature->can_batch_sync);
    EXPECT(feature->batch_sync_blocked_reason.empty());
  }

  const pixcake::CapabilityDescriptor* match = registry.find("color.match");
  EXPECT(match != nullptr);
  if (match != nullptr) {
    EXPECT(match->module_label == "AI 色彩");
    EXPECT(match->label == "AI 追色");
    EXPECT(match->registered_for_drawing);
    EXPECT(match->can_use_in_preset);
    EXPECT(!match->can_batch_sync);
    EXPECT(match->batch_sync_blocked_reason ==
           "同步时按每张图重新计算，不是把数值原样抄过去");
  }

  const pixcake::CapabilityDescriptor* detail = registry.find("detail");
  EXPECT(detail == nullptr);
  EXPECT(registry.find("color.match.local") == nullptr);
  EXPECT(registry.find("portrait.eyes") == nullptr);

  if (g_failures != 0) {
    std::fprintf(stderr, "%d failure(s)\n", g_failures);
    return 1;
  }
  return 0;
}
```

- [x] **Step 2: Run test to verify it fails**

Run: `cmake -S . -B build && cmake --build build --target test_capability_registry`

Expected: 配置或编译失败，因为 `capability_registry.hpp`、`CapabilityRegistry` 和 `src/capability_registry.cpp` 尚不存在。

- [x] **Step 3: Write minimal implementation**

创建 `kernel/capability/include/pixcake/capability/capability_registry.hpp`：

```cpp
#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace pixcake {

struct CapabilityDescriptor {
  std::string id;
  std::string module_label;
  std::string label;
  bool registered_for_drawing = true;
  bool can_use_in_preset = true;
  bool can_batch_sync = true;
  std::string batch_sync_blocked_reason;
};

class CapabilityRegistry {
 public:
  static CapabilityRegistry phase_one();

  const CapabilityDescriptor* find(std::string_view id) const;
  const std::vector<CapabilityDescriptor>& descriptors() const;

 private:
  explicit CapabilityRegistry(std::vector<CapabilityDescriptor> descriptors);

  std::vector<CapabilityDescriptor> descriptors_;
};

}  // namespace pixcake
```

创建 `kernel/capability/src/capability_registry.cpp`。能力目录只在这里出现一次，顺序就是文档标识的稳定顺序：

```cpp
#include "pixcake/capability/capability_registry.hpp"

#include <utility>

namespace pixcake {
namespace {

constexpr char kRecomputeReason[] = "同步时按每张图重新计算，不是把数值原样抄过去";

std::vector<CapabilityDescriptor> phase_one_descriptors() {
  return {
      {"color.match", "AI 色彩", "AI 追色", true, false, false, kRecomputeReason},
      {"color.style", "AI 色彩", "AI 色彩风格", true, false, false, kRecomputeReason},
      {"color.global", "色彩", "全图色彩工具", true, true, true, ""},
      {"color.local", "色彩", "局部调色", true, true, true, ""},
      {"color.post", "色彩", "后期调色", true, true, true, ""},
      {"portrait.blemish", "人像", "祛除瑕疵", true, true, true, ""},
      {"portrait.skin", "人像", "皮肤调整", true, true, true, ""},
      {"portrait.reshape", "人像", "面部重塑", true, true, true, ""},
      {"portrait.feature", "人像", "表情、牙齿、眼睛、妆容、头发", true, true, true, ""},
      {"portrait.body", "人像", "手、脚、全身美型", true, true, true, ""},
      {"image.background", "图像", "背景调整", true, true, true, ""},
      {"image.idphoto", "图像", "证件照换背景", true, true, true, ""},
      {"image.sky", "图像", "换天空", true, true, true, ""},
      {"clothes", "衣物", "衣物美化", true, true, true, ""},
      {"pet", "宠物", "宠物美化", true, false, true, ""},
      {"crop", "裁剪", "裁剪旋转", true, true, true, ""},
      {"manual.edge", "手动", "祛边", true, false, true, ""},
      {"manual.liquify", "手动", "液化", true, false, true, ""},
      {"manual.heal", "手动", "污点修复、修补、仿制", true, false, true, ""},
  };
}

}  // namespace

CapabilityRegistry::CapabilityRegistry(std::vector<CapabilityDescriptor> descriptors)
    : descriptors_(std::move(descriptors)) {}

CapabilityRegistry CapabilityRegistry::phase_one() {
  return CapabilityRegistry(phase_one_descriptors());
}

const CapabilityDescriptor* CapabilityRegistry::find(std::string_view id) const {
  for (const auto& descriptor : descriptors_) {
    if (descriptor.id == id) {
      return &descriptor;
    }
  }
  return nullptr;
}

const std::vector<CapabilityDescriptor>& CapabilityRegistry::descriptors() const { return descriptors_; }

}  // namespace pixcake
```

- [x] **Step 4: Run test to verify it passes**

Run: `cmake --build build --target test_capability_registry && ctest --test-dir build -R '^test_capability_registry$' --output-on-failure`

Expected: `test_capability_registry` 通过；注册表包含 19 个可绘制能力，`detail`、`color.match.local` 和提前拆出的 `portrait.eyes` 不在注册表中。

- [x] **Step 5: Commit**

```bash
git add kernel/capability/CMakeLists.txt kernel/capability/include/pixcake/capability/capability_registry.hpp kernel/capability/src/capability_registry.cpp tests/kernel/capability/test_capability_registry.cpp tests/CMakeLists.txt
git commit -m "feat: add the phase-one capability registry"
```

提交时只加入本任务文件，不加入 `kernel/layerdoc` 或其他已有未提交修改。

---

### Task 2: 固定阶段一默认管线顺序并验证无重复能力

**Files:**
- Modify: `kernel/capability/include/pixcake/capability/capability_registry.hpp`
- Modify: `kernel/capability/src/capability_registry.cpp`
- Modify: `tests/kernel/capability/test_capability_registry.cpp`

**Interfaces:**
- Consumes: `CapabilityRegistry::phase_one()` 和能力稳定标识
- Produces: `PipelineStep`、`phase_one_pipeline()`、`validate_phase_one_pipeline()`

- [x] **Step 1: Write the failing test**

在 `test_capability_registry.cpp` 中加入以下 include 和函数，并在 `main()` 的失败检查前调用 `test_pipeline()`：

```cpp
void test_pipeline() {
  const std::vector<pixcake::PipelineStep> pipeline = pixcake::phase_one_pipeline();
  const std::vector<std::string> expected = {
      "detail.geometry", "detail.denoise", "color.match", "color.style", "color.global",
      "color.local",    "portrait.blemish", "portrait.skin", "portrait.reshape",
      "portrait.feature", "portrait.body", "image.background", "image.idphoto", "image.sky",
      "clothes", "pet", "color.post", "manual.liquify", "manual.heal", "manual.edge", "crop"};
  EXPECT(pipeline.size() == expected.size());
  for (std::size_t index = 0; index < expected.size() && index < pipeline.size(); ++index) {
    EXPECT(pipeline[index].step_id == expected[index]);
  }

  EXPECT(pipeline[0].capability_id == "detail");
  EXPECT(!pipeline[0].registered_for_drawing);
  EXPECT(pipeline[1].capability_id == "detail");
  EXPECT(!pipeline[1].registered_for_drawing);
  EXPECT(pipeline[2].capability_id == "color.match");
  EXPECT(pipeline.back().capability_id == "crop");

  std::string error;
  EXPECT(pixcake::validate_phase_one_pipeline(pipeline, &error));
  EXPECT(error.empty());

  std::vector<pixcake::PipelineStep> duplicate = pipeline;
  duplicate.push_back(pixcake::PipelineStep{"duplicate", "color.match", true});
  EXPECT(!pixcake::validate_phase_one_pipeline(duplicate, &error));
  EXPECT(error == "能力在管线中重复: color.match");
}
```

- [x] **Step 2: Run test to verify it fails**

Run: `cmake --build build --target test_capability_registry`

Expected: 编译失败，因为 `PipelineStep`、`phase_one_pipeline()` 和 `validate_phase_one_pipeline()` 尚未声明。

- [x] **Step 3: Write minimal implementation**

在 `capability_registry.hpp` 的 `CapabilityRegistry` 后加入：

```cpp
struct PipelineStep {
  std::string step_id;
  std::string capability_id;
  bool registered_for_drawing = true;
};

std::vector<PipelineStep> phase_one_pipeline();
bool validate_phase_one_pipeline(const std::vector<PipelineStep>& pipeline, std::string* error);
```

在 `capability_registry.cpp` 中加入：

```cpp
#include <unordered_set>

std::vector<PipelineStep> phase_one_pipeline() {
  return {
      {"detail.geometry", "detail", false},
      {"detail.denoise", "detail", false},
      {"color.match", "color.match", true},
      {"color.style", "color.style", true},
      {"color.global", "color.global", true},
      {"color.local", "color.local", true},
      {"portrait.blemish", "portrait.blemish", true},
      {"portrait.skin", "portrait.skin", true},
      {"portrait.reshape", "portrait.reshape", true},
      {"portrait.feature", "portrait.feature", true},
      {"portrait.body", "portrait.body", true},
      {"image.background", "image.background", true},
      {"image.idphoto", "image.idphoto", true},
      {"image.sky", "image.sky", true},
      {"clothes", "clothes", true},
      {"pet", "pet", true},
      {"color.post", "color.post", true},
      {"manual.liquify", "manual.liquify", true},
      {"manual.heal", "manual.heal", true},
      {"manual.edge", "manual.edge", true},
      {"crop", "crop", true},
  };
}

bool validate_phase_one_pipeline(const std::vector<PipelineStep>& pipeline, std::string* error) {
  if (error != nullptr) {
    error->clear();
  }
  std::unordered_set<std::string> seen;
  for (const auto& step : pipeline) {
    if (step.capability_id == "detail") {
      continue;
    }
    if (!seen.insert(step.capability_id).second) {
      if (error != nullptr) {
        *error = "能力在管线中重复: " + step.capability_id;
      }
      return false;
    }
  }
  return true;
}
```

该校验只验证本计划能观察到的重复能力，不实现依赖图、渲染或循环修复；未来引入依赖边时必须先扩展契约和测试。

- [x] **Step 4: Run test to verify it passes**

Run: `cmake --build build --target test_capability_registry && ctest --test-dir build -R '^test_capability_registry$' --output-on-failure`

Expected: 管线顺序和 `detail` 保留阶段通过；重复能力返回 `false` 和精确原因，不修改原向量。

- [x] **Step 5: Commit**

```bash
git add kernel/capability/include/pixcake/capability/capability_registry.hpp kernel/capability/src/capability_registry.cpp tests/kernel/capability/test_capability_registry.cpp
git commit -m "feat: define the phase-one capability pipeline"
```

---

### Task 3: 让同步树消费注册表并保留非能力同步行

**Files:**
- Modify: `kernel/capability/src/capability_sync.cpp`
- Modify: `kernel/capability/include/pixcake/capability/capability_sync.hpp`
- Modify: `tests/kernel/sync/test_sync.cpp`
- Modify: `tests/CMakeLists.txt`
- Create: `tests/arch/check_capability_ownership.cmake`

**Interfaces:**
- Consumes: `CapabilityRegistry::phase_one()`、`CapabilityDescriptor`、现有 `CapabilitySyncRow`
- Produces: 与现有 `sync_tree()` 相同的 22 行同步树；能力行的标签、模块和禁用原因全部来自注册表

- [x] **Step 1: Write the failing test**

在 `tests/kernel/sync/test_sync.cpp` 的 `test_tree_reads_attributes_from_owners()` 末尾加入：

```cpp
  const auto registry = pixcake::CapabilityRegistry::phase_one();
  for (const auto& descriptor : registry.descriptors()) {
    const auto* row = find_item(tree, descriptor.id.c_str());
    EXPECT(row != nullptr);
    if (row == nullptr) {
      continue;
    }
    EXPECT(row->module_label == descriptor.module_label);
    EXPECT(row->label == descriptor.label);
    EXPECT(row->capability_id == descriptor.id);
    EXPECT(row->enabled == descriptor.can_batch_sync);
    EXPECT(row->blocked_reason == descriptor.batch_sync_blocked_reason);
  }
```

同时在文件顶部加入：

```cpp
#include "pixcake/capability/capability_registry.hpp"
```

创建 `tests/arch/check_capability_ownership.cmake`：

```cmake
if(NOT DEFINED SOURCE_DIR)
  message(FATAL_ERROR "SOURCE_DIR is required")
endif()

set(SYNC_SOURCE "${SOURCE_DIR}/kernel/capability/src/capability_sync.cpp")
file(READ "${SYNC_SOURCE}" sync_text)
foreach(label "AI 追色" "AI 色彩风格" "全图色彩工具" "局部调色" "后期调色"
              "祛除瑕疵" "皮肤调整" "面部重塑" "表情、牙齿、眼睛、妆容、头发"
              "手、脚、全身美型" "背景调整" "证件照换背景" "换天空" "衣物美化"
              "宠物美化" "裁剪旋转" "祛边" "液化" "污点修复、修补、仿制")
  string(FIND "${sync_text}" "${label}" position)
  if(NOT position EQUAL -1)
    message(FATAL_ERROR "capability_sync.cpp owns registry label: ${label}")
  endif()
endforeach()
```

在 `tests/CMakeLists.txt` 的 `arch_dependencies` 测试后加入：

```cmake
add_test(
  NAME capability_ownership
  COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${CMAKE_SOURCE_DIR} -P ${CMAKE_SOURCE_DIR}/tests/arch/check_capability_ownership.cmake
)
```

先运行观察当前差异：

Run: `cmake -S . -B build && cmake --build build --target test_sync && ctest --test-dir build -R '^(test_sync|capability_ownership)$' --output-on-failure`

Expected: `test_sync` 可以编译运行；`capability_ownership` 失败并指出 `capability_sync.cpp` 仍拥有注册能力标签。局部追色、`detail` 和自定义画笔蒙版不在禁止列表中，因为它们是兼容性同步行。

- [x] **Step 2: Run test to verify it fails**

Run: `ctest --test-dir build -R '^capability_ownership$' --output-on-failure`

Expected: 测试失败，原因是当前同步实现仍包含 19 个注册能力的展示名；不得修改测试去迁就旧表。

- [x] **Step 3: Write minimal implementation**

重写 `kernel/capability/src/capability_sync.cpp`：

1. 删除当前 `Row kRows[]` 中 19 个能力行的重复 `module_label`、`label`、`enabled` 和原因。
2. 调用 `CapabilityRegistry::phase_one()`，逐个把 `CapabilityDescriptor` 转成 `CapabilitySyncRow`。
3. 在 `color.post` 对应能力行之后追加一个明确的 `detail` 非绘制同步行；它的 `capability_id` 仍为 `detail`，但不把 `detail` 加入 Task 1 的可绘制注册表：

```cpp
CapabilitySyncRow detail;
detail.id = "detail";
detail.module_label = "色彩";
detail.label = "细节";
detail.capability_id = "detail";
detail.enabled = true;
detail.blocked_reason.clear();
rows.push_back(std::move(detail));
```

4. 在 `detail` 后追加两个明确的非能力同步项，不能把它们注册成能力：

```cpp
CapabilitySyncRow local_match;
local_match.id = "color.match.local";
local_match.module_label = "AI 色彩";
local_match.label = "局部追色";
local_match.capability_id.clear();
local_match.enabled = false;
local_match.blocked_reason = "局部追色不能批量同步";
rows.push_back(std::move(local_match));

CapabilitySyncRow brush;
brush.id = "mask.brush";
brush.module_label = "色彩";
brush.label = "自定义画笔蒙版";
brush.capability_id.clear();
brush.enabled = false;
brush.blocked_reason = "自定义里的画笔不能同步";
rows.push_back(std::move(brush));
```

5. 保持现有同步树的稳定顺序：`color.match.local` 位于 `color.match` 后，`detail` 位于 `color.post` 后，`mask.brush` 位于 `detail` 后；其余能力按注册表顺序。最终仍为 22 行：19 个注册能力、`detail`、局部追色、画笔蒙版。
6. `capability_sync.hpp` 只保留长期公开的 `CapabilitySyncRow` 和 `capability_sync_rows()`，不暴露注册表内部数组。

- [x] **Step 4: Run test to verify it passes**

Run:

```bash
cmake --build build && ctest --test-dir build -R '^(test_capability_registry|test_sync|arch_dependencies|capability_ownership)$' --output-on-failure
```

Expected: 注册表、同步树、依赖检查和能力所有权检查全部通过；`test_sync` 仍为 22 行，`detail`、局部追色和画笔蒙版仍按非绘制或非能力规则存在，`kernel/capability` 不包含 Qt、OpenGL 或其他能力目录头。

- [x] **Step 5: Commit**

```bash
git add kernel/capability/include/pixcake/capability/capability_sync.hpp kernel/capability/src/capability_sync.cpp tests/kernel/sync/test_sync.cpp tests/CMakeLists.txt tests/arch/check_capability_ownership.cmake
git commit -m "refactor: source sync metadata from capability registry"
```

### Task 4: 全量验证计划边界

**Files:**
- Modify: `docs/superpowers/plans/2026-11-03-kernel-capability-registry.md`

- [x] **Step 1: Run the complete validation set**

Run:

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
rg -n 'Qt|QOpenGL|sqlite3|pixcake/ledger|http|curl|system\\s*\\(' kernel/capability || true
git diff --check
```

Expected: 全量测试通过；架构依赖测试通过；能力模块没有出现 Qt、OpenGL、SQLite、网络、账本或 `system()` 依赖；`git diff --check` 无输出。

- [x] **Step 2: Update this plan's checkboxes**

只有在 Task 1–3 的测试和提交完成后，把本计划每个已完成步骤从 `[ ]` 改为 `[x]`。不要修改需求、架构或七关清单来掩盖尚未实现的参数模式和效果。

- [x] **Step 3: Commit the plan status**

```bash
git add docs/superpowers/plans/2026-11-03-kernel-capability-registry.md
git commit -m "docs: mark capability registry plan complete"
```

该提交只更新计划勾选状态，不包含其他模块文件。
