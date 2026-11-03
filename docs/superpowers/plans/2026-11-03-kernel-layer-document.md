# 图层文档 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 做出创意图层文档：记下从哪张精修、哪一次修订进来，维护图层树、混合、不透明度、锁定、隐藏、分组、蒙版参数、选区和生成记录，并在用户选择更新原图时只把「效果已烘焙、滑杆已重置」写进精修信封。

**Architecture:** `kernel/layerdoc` 只保存图层事实，不复制精修滑杆，不保存像素。落层按结果形状改树，同一结果编号只应用一次。批量设计占用时拒绝一切编辑。文件用和精修文档相同的小端记录、校验和和五步替换，魔数用 `PXLD`，避免被当成 `PXED`。更新原图调用精修文档上的新方法清掉能力片段并留下烘焙标记；不调用它时精修文档原样不动。

**Tech Stack:** C++17、CMake、CTest、现有的 `atomic_replace` 和 `open_durable_file`。测试是独立可执行文件，不引入第三方测试框架。文件系统用 `tests/support/fake_file_system.hpp`。

## Global Constraints

- 图层文档记下图层树、混合、不透明度、锁定、隐藏、分组、蒙版、选区、生成记录、来源图片和来源修订。成功时按结果形状落层，结果编号去重。批量设计进行中拒绝编辑。精修滑杆不在这里（契约 C-28、架构第 6.5 节）。
- 混合模式只有：正常、正片叠底、滤色、变亮、变暗。不透明度 0–100，默认 100。出处是手册 [创意模块功能说明](https://www.pixcakeai.com/guide/127932)（2026-09-14），不是从界面猜的。
- 选区羽化是 0–1000。出处是手册 [消除——AI祛路人](https://www.pixcakeai.com/guide/158249)（2026-09-17）。蒙版密度和蒙版羽化，[工作区页面介绍](https://www.pixcakeai.com/guide/32445)、创意模块功能说明、AI 祛路人都只写「可以调整」，没有写上下限。这两项保存非负整数，不套用不透明度的 100，也不套用选区羽化的 1000。负数是状态冲突。新蒙版先写成密度 100、羽化 0，这只是文件里的初始值，手册没有写默认值。
- 从精修进入时记录来源图片和来源修订，不复制精修滑杆。更新原图在精修信封上留下效果已烘焙、滑杆已重置。用户没选写回时，精修文档不动。更新之后不能靠重置效果回到原图（契约 C-29、架构第 6.5 节、第 8 节）。
- 结果形状只允许：新图层、替换当前生成图层、细节层加效果层、图层组、可编辑蒙版的图层、可改位置和大小的图层。工具不操作界面，图层文档执行形状（架构第 8 节）。
- 同一结果编号再落一次不新增图层（架构第 8 节、边界 B-72、B-106）。
- 下标越界拒绝这一次，当前树和相邻项不改（边界 B-29）。
- 批量设计进行中的编辑和图层命令拒绝（边界 B-70、需求第 16.3 节）。
- 字节布局沿边界 B-116 的规则，魔数换成 `PXLD`：小端；格式版本只能是 1，不是 1 就按文档损坏处理；正文是类型加长度的记录；不认识的记录原样保留；校验和是正文的 FNV-1a 64 位；临时文件是正式路径加 `.tmp`，备份是正式路径加 `.bak`；坏文件按现有 `open_durable_file` 隔离。没有这份文件时，结果不是成功，也不是文档损坏。
- 正式文件走现有五步替换。失败时留下上一份完整文件（契约 C-25）。
- `kernel/layerdoc` 不包含 Qt 头，不包含 `sqlite3.h`，不包含 `pixcake/ledger/`，不包含 `jpeglib.h`，不调用 `system`，不编码图片。
- 执行时只在本地提交，不推远端，不建 Pull Request。

本计划明确不做：像素、`.tsq` 的图像字节、项目库里「出现在原图左侧」的排序、配方执行、扣点、生成进程、历史步骤、Qt 壳、GPU、`kernel-cli`。`.tsq` 在本计划里只是一个路径字符串。

手册写了、这一份不做的还有三件。[工作区页面介绍](https://www.pixcakeai.com/guide/32445)（2026-09-17）写：把照片拖进画布会变成图层。那一步要像素。[创意模块功能说明](https://www.pixcakeai.com/guide/127932) 写：背景图层可以改对齐、位置、大小、旋转、翻转；右键可以复制图层。对齐和位置这几页没有写单位，复制图层这一份先不做。

## 手册里和图层有关的原文

[工作区页面介绍](https://www.pixcakeai.com/guide/32445) 写的是区域，不写滑杆上下限。创意页有手动工具栏、画布、缩略图、AI 工具箱、侧边栏和图层面板。侧边栏是 AI 工具、历史记录、批量设计、帮助、新手引导、反馈。图层面板显示当前图片的全部图层。画布上拖入照片会导入为图层。

[创意模块功能说明](https://www.pixcakeai.com/guide/127932) 写图层本身：五种混合模式；不透明度默认 100%，范围 0~100%；锁定后不能编辑，再点一次解锁；未选中图层时添加蒙版、锁定、删除都不可用；背景图层是原图图层；AI 祛路人蒙版可调密度和羽化，并用添加、减去画笔；批量设计任务中的图片不能编辑，也不能看历史和图层面板；更新原精修图会留下效果并重置滑杆，不能靠重置效果回到原图；应用为新图的扩展名是 `.tsq`，缩略图在原图左侧。

[消除——AI祛路人](https://www.pixcakeai.com/guide/158249) 写选区羽化的调节范围是 0~1000。同一段里，蒙版页只写可以调整密度和羽化，没有写这两个滑杆的数字范围。

---

### Task 1: 图层树和落层

**Files:**
- Create: `kernel/layerdoc/include/pixcake/layerdoc/layer_document.hpp`
- Create: `kernel/layerdoc/src/layer_document.cpp`
- Create: `kernel/layerdoc/CMakeLists.txt`
- Create: `tests/kernel/layerdoc/test_layer_document.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`
- Modify: `tests/arch/check_dependencies.cmake`

**Interfaces:**
- Consumes: `ResultCode`
- Produces: `LayerView`、`SelectionView`、`GenerationView`、`LayerDocument`

- [x] **Step 1: Write the failing test**

把 `tests/kernel/layerdoc/test_layer_document.cpp` 写成下面的文件。根 `CMakeLists.txt` 在 `kernel/editdoc` 后加入 `add_subdirectory(kernel/layerdoc)`。`tests/CMakeLists.txt` 在 `test_edit_document` 后加入 `test_layer_document`，链接 `pixcake_layerdoc`、`pixcake_warnings`。`kernel/layerdoc/CMakeLists.txt` 先只编译一个空的 `src/layer_document.cpp`，公开 `include`，并公开链接 `pixcake_support`、`pixcake_warnings`。

`tests/arch/check_dependencies.cmake` 在规则 12 后加入：

```cmake
# 13. 图层文档不保存像素，也不进账本或 SQLite。
scan("${SOURCE_DIR}/kernel/layerdoc" "rule 13" "sqlite3.h" "pixcake/ledger/" "jpeglib.h" "QOpenGL" "#include <Q")
```

```cpp
#include "pixcake/layerdoc/layer_document.hpp"

#include <cstdio>
#include <string>

namespace {

int g_failures = 0;

void expect(bool cond, const char* text, int line) {
  if (!cond) {
    std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, line, text);
    ++g_failures;
  }
}

#define EXPECT(cond) expect(static_cast<bool>(cond), #cond, __LINE__)

pixcake::LayerDocument opened() {
  auto document = pixcake::LayerDocument::create("img-1", 4);
  EXPECT(document.has_value());
  return std::move(*document);
}

void test_entry_does_not_copy_sliders_and_keeps_background() {
  EXPECT(!pixcake::LayerDocument::create("", 1).has_value());
  auto document = opened();
  EXPECT(document.source_image_id() == "img-1");
  EXPECT(document.source_revision() == 4);
  EXPECT(!document.batch_design());
  EXPECT(document.layers().size() == 1);
  EXPECT(document.layers()[0].id == "background");
  EXPECT(document.layers()[0].name == "背景");
  EXPECT(document.layers()[0].background);
  EXPECT(document.layers()[0].blend == "正常");
  EXPECT(document.layers()[0].opacity == 100);
  EXPECT(!document.layers()[0].mask);
  EXPECT(document.generations().empty());
}

void test_blend_opacity_lock_and_index() {
  auto document = opened();
  EXPECT(document.set_blend("background", "滤色") == pixcake::ResultCode::Ok);
  EXPECT(document.set_blend("background", "叠加") == pixcake::ResultCode::StateConflict);
  pixcake::LayerView background;
  EXPECT(document.find_layer("background", background));
  EXPECT(background.blend == "滤色");
  EXPECT(document.set_opacity("background", 0) == pixcake::ResultCode::Ok);
  EXPECT(document.set_opacity("background", 101) == pixcake::ResultCode::StateConflict);
  EXPECT(document.set_opacity("background", -1) == pixcake::ResultCode::StateConflict);
  EXPECT(document.find_layer("background", background));
  EXPECT(background.opacity == 0);
  EXPECT(document.set_locked("background", true) == pixcake::ResultCode::Ok);
  EXPECT(document.rename("background", "底板") == pixcake::ResultCode::StateConflict);
  EXPECT(document.set_locked("background", false) == pixcake::ResultCode::Ok);
  EXPECT(document.rename("background", "底板") == pixcake::ResultCode::Ok);
  EXPECT(document.remove_child("", 1) == pixcake::ResultCode::StateConflict);
  EXPECT(document.layers().size() == 1);
  EXPECT(document.remove("background") == pixcake::ResultCode::StateConflict);
}

void test_group_mask_selection_and_batch_design() {
  auto document = opened();
  EXPECT(document.group_layer("background", "group-1", "组") == pixcake::ResultCode::StateConflict);
  EXPECT(document.add_layer("layer-1", "人物") == pixcake::ResultCode::Ok);
  EXPECT(document.group_layer("layer-1", "group-1", "组") == pixcake::ResultCode::Ok);
  pixcake::LayerView layer;
  EXPECT(document.find_layer("layer-1", layer));
  EXPECT(layer.parent_id == "group-1");
  pixcake::LayerView group;
  EXPECT(document.find_layer("group-1", group));
  EXPECT(group.group);
  EXPECT(document.add_mask("layer-1") == pixcake::ResultCode::Ok);
  EXPECT(document.set_mask_density("layer-1", 40) == pixcake::ResultCode::Ok);
  EXPECT(document.set_mask_density("layer-1", 101) == pixcake::ResultCode::Ok);
  EXPECT(document.set_mask_density("layer-1", -1) == pixcake::ResultCode::StateConflict);
  pixcake::LayerView masked;
  EXPECT(document.find_layer("layer-1", masked));
  EXPECT(masked.mask_density == 101);
  EXPECT(document.set_mask_feather("layer-1", 1001) == pixcake::ResultCode::Ok);
  EXPECT(document.set_mask_feather("layer-1", -1) == pixcake::ResultCode::StateConflict);
  EXPECT(document.find_layer("layer-1", masked));
  EXPECT(masked.mask_feather == 1001);
  EXPECT(document.set_selection(true, 12) == pixcake::ResultCode::Ok);
  EXPECT(document.selection().present);
  EXPECT(document.selection().feather == 12);
  EXPECT(document.set_selection(false, 1001) == pixcake::ResultCode::StateConflict);
  EXPECT(document.selection().feather == 12);

  EXPECT(document.set_batch_design(true) == pixcake::ResultCode::Ok);
  EXPECT(document.add_layer("layer-2", "后来的") == pixcake::ResultCode::StateConflict);
  EXPECT(document.place_result("result-1", "新图层", "tool-a") == pixcake::ResultCode::StateConflict);
  EXPECT(document.layers().size() == 3);
  EXPECT(document.set_batch_design(false) == pixcake::ResultCode::Ok);
  EXPECT(document.add_layer("layer-2", "后来的") == pixcake::ResultCode::Ok);
}

void test_place_result_shapes_once() {
  auto document = opened();
  EXPECT(document.place_result("result-1", "没有这种形状", "tool-a") == pixcake::ResultCode::StateConflict);
  EXPECT(document.generations().empty());
  EXPECT(document.place_result("result-1", "新图层", "tool-a") == pixcake::ResultCode::Ok);
  EXPECT(document.place_result("result-1", "新图层", "tool-a") == pixcake::ResultCode::Ok);
  EXPECT(document.generations().size() == 1);
  int copies = 0;
  for (const auto& layer : document.layers()) {
    if (layer.id == "result-1") {
      ++copies;
    }
  }
  EXPECT(copies == 1);

  EXPECT(document.place_result("result-2", "细节层加效果层", "绘影") == pixcake::ResultCode::Ok);
  pixcake::LayerView detail;
  pixcake::LayerView effect;
  EXPECT(document.find_layer("result-2#detail", detail));
  EXPECT(document.find_layer("result-2#effect", effect));
  EXPECT(detail.name == "细节");
  EXPECT(effect.name == "效果");
  EXPECT(document.place_result("result-2", "细节层加效果层", "绘影") == pixcake::ResultCode::Ok);
  EXPECT(document.generations().size() == 2);

  EXPECT(document.place_result("result-3", "可编辑蒙版的图层", "祛路人") == pixcake::ResultCode::Ok);
  pixcake::LayerView masked;
  EXPECT(document.find_layer("result-3", masked));
  EXPECT(masked.mask);
  EXPECT(document.place_result("result-4", "可改位置和大小的图层", "超清") == pixcake::ResultCode::Ok);
  pixcake::LayerView placed;
  EXPECT(document.find_layer("result-4", placed));
  EXPECT(placed.position_editable);
  EXPECT(document.place_result("result-5", "图层组", "换衣服") == pixcake::ResultCode::Ok);
  pixcake::LayerView folder;
  EXPECT(document.find_layer("result-5", folder));
  EXPECT(folder.group);

  EXPECT(document.place_result("result-6", "替换当前生成图层", "重绘") == pixcake::ResultCode::Ok);
  EXPECT(document.find_layer("result-5", folder));
  EXPECT(folder.name == "重绘");
  EXPECT(!document.find_layer("result-6", placed));
  EXPECT(document.generations().size() == 6);
}

}  // namespace

int main() {
  test_entry_does_not_copy_sliders_and_keeps_background();
  test_blend_opacity_lock_and_index();
  test_group_mask_selection_and_batch_design();
  test_place_result_shapes_once();
  if (g_failures != 0) {
    std::fprintf(stderr, "%d failure(s)\n", g_failures);
    return 1;
  }
  return 0;
}
```

- [x] **Step 2: Run test to verify it fails**

Run: `cmake -S . -B build && cmake --build build --target test_layer_document`

Expected: 编译失败，因为还没有 `pixcake/layerdoc/layer_document.hpp`。

- [x] **Step 3: Write minimal implementation**

`kernel/layerdoc/include/pixcake/layerdoc/layer_document.hpp`：

```cpp
#pragma once

#include "pixcake/support/result_code.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace pixcake {

struct LayerView {
  std::string id;
  std::string name;
  std::string parent_id;
  bool group = false;
  bool locked = false;
  bool hidden = false;
  bool background = false;
  bool mask = false;
  bool position_editable = false;
  std::string blend = "正常";
  int opacity = 100;
  int mask_density = 100;
  int mask_feather = 0;
};

struct SelectionView {
  bool present = false;
  bool inverted = false;
  int feather = 0;
};

struct GenerationView {
  std::string result_id;
  std::string shape;
  std::string tool_id;
};

// 图层文档不保存精修滑杆，也不保存像素。
class LayerDocument {
 public:
  static std::optional<LayerDocument> create(std::string source_image_id, std::uint64_t source_revision);

  std::string source_image_id() const;
  std::uint64_t source_revision() const;
  bool batch_design() const;
  bool recovered_empty() const;
  SelectionView selection() const;
  std::vector<LayerView> layers() const;
  std::vector<GenerationView> generations() const;
  bool find_layer(const std::string& id, LayerView& out) const;

  ResultCode set_batch_design(bool on);
  ResultCode rename(const std::string& id, std::string name);
  ResultCode set_blend(const std::string& id, const std::string& blend);
  ResultCode set_opacity(const std::string& id, int opacity);
  ResultCode set_locked(const std::string& id, bool locked);
  ResultCode set_hidden(const std::string& id, bool hidden);
  ResultCode add_layer(const std::string& id, std::string name);
  ResultCode add_group(const std::string& id, std::string name, const std::string& parent_id);
  ResultCode group_layer(const std::string& layer_id, const std::string& group_id, std::string group_name);
  ResultCode remove(const std::string& id);
  ResultCode remove_child(const std::string& parent_id, int index);
  ResultCode move_child(const std::string& id, const std::string& parent_id, int index);
  ResultCode add_mask(const std::string& id);
  ResultCode remove_mask(const std::string& id);
  ResultCode set_mask_density(const std::string& id, int density);
  ResultCode set_mask_feather(const std::string& id, int feather);
  ResultCode set_selection(bool inverted, int feather);
  ResultCode clear_selection();
  ResultCode place_result(const std::string& result_id, const std::string& shape, const std::string& tool_id);

 private:
  LayerView* find(const std::string& id);
  const LayerView* find(const std::string& id) const;
  bool editable() const;
  bool parent_ok(const std::string& parent_id) const;
  bool is_descendant(const std::string& id, const std::string& maybe_child) const;
  std::vector<std::string> child_ids(const std::string& parent_id) const;

  std::string source_image_id_;
  std::uint64_t source_revision_ = 0;
  bool batch_design_ = false;
  bool recovered_empty_ = false;
  SelectionView selection_;
  std::vector<LayerView> layers_;
  std::vector<GenerationView> generations_;
  std::string current_generation_layer_id_;
};

}  // namespace pixcake
```

`kernel/layerdoc/src/layer_document.cpp` 整文件如下。`CMakeLists.txt` 改为编译这个文件。

```cpp
#include "pixcake/layerdoc/layer_document.hpp"

#include <utility>

namespace pixcake {
namespace {

bool has_nul(const std::string& text) { return text.find('\0') != std::string::npos; }

bool safe_id(const std::string& id) {
  return !id.empty() && id != "." && id != ".." && !has_nul(id) && id.find('/') == std::string::npos &&
         id.find('\\') == std::string::npos;
}

bool safe_name(const std::string& name) { return !name.empty() && !has_nul(name); }

bool known_blend(const std::string& blend) {
  return blend == "正常" || blend == "正片叠底" || blend == "滤色" || blend == "变亮" || blend == "变暗";
}

LayerView make_layer(std::string id, std::string name) {
  LayerView layer;
  layer.id = std::move(id);
  layer.name = std::move(name);
  return layer;
}

}  // namespace

std::optional<LayerDocument> LayerDocument::create(std::string source_image_id, std::uint64_t source_revision) {
  if (!safe_id(source_image_id)) {
    return std::nullopt;
  }
  LayerDocument document;
  document.source_image_id_ = std::move(source_image_id);
  document.source_revision_ = source_revision;
  LayerView background = make_layer("background", "背景");
  background.background = true;
  document.layers_.push_back(std::move(background));
  return document;
}

std::string LayerDocument::source_image_id() const { return source_image_id_; }
std::uint64_t LayerDocument::source_revision() const { return source_revision_; }
bool LayerDocument::batch_design() const { return batch_design_; }
bool LayerDocument::recovered_empty() const { return recovered_empty_; }
SelectionView LayerDocument::selection() const { return selection_; }
std::vector<LayerView> LayerDocument::layers() const { return layers_; }
std::vector<GenerationView> LayerDocument::generations() const { return generations_; }

bool LayerDocument::find_layer(const std::string& id, LayerView& out) const {
  const LayerView* found = find(id);
  if (found == nullptr) {
    return false;
  }
  out = *found;
  return true;
}

LayerView* LayerDocument::find(const std::string& id) {
  for (auto& layer : layers_) {
    if (layer.id == id) {
      return &layer;
    }
  }
  return nullptr;
}

const LayerView* LayerDocument::find(const std::string& id) const {
  for (const auto& layer : layers_) {
    if (layer.id == id) {
      return &layer;
    }
  }
  return nullptr;
}

bool LayerDocument::editable() const { return !batch_design_ && !recovered_empty_; }

bool LayerDocument::parent_ok(const std::string& parent_id) const {
  if (parent_id.empty()) {
    return true;
  }
  const LayerView* parent = find(parent_id);
  return parent != nullptr && parent->group;
}

bool LayerDocument::is_descendant(const std::string& id, const std::string& maybe_child) const {
  std::string walk = maybe_child;
  for (std::size_t hop = 0; hop < layers_.size() + 1 && !walk.empty(); ++hop) {
    if (walk == id) {
      return true;
    }
    const LayerView* node = find(walk);
    if (node == nullptr) {
      return false;
    }
    walk = node->parent_id;
  }
  return !walk.empty();
}

std::vector<std::string> LayerDocument::child_ids(const std::string& parent_id) const {
  std::vector<std::string> ids;
  for (const auto& layer : layers_) {
    if (layer.parent_id == parent_id) {
      ids.push_back(layer.id);
    }
  }
  return ids;
}

ResultCode LayerDocument::set_batch_design(bool on) {
  if (recovered_empty_) {
    return ResultCode::StateConflict;
  }
  batch_design_ = on;
  return ResultCode::Ok;
}

ResultCode LayerDocument::rename(const std::string& id, std::string name) {
  if (!editable() || !safe_name(name)) {
    return ResultCode::StateConflict;
  }
  LayerView* layer = find(id);
  if (layer == nullptr || layer->locked) {
    return ResultCode::StateConflict;
  }
  layer->name = std::move(name);
  return ResultCode::Ok;
}

ResultCode LayerDocument::set_blend(const std::string& id, const std::string& blend) {
  if (!editable() || !known_blend(blend)) {
    return ResultCode::StateConflict;
  }
  LayerView* layer = find(id);
  if (layer == nullptr || layer->locked) {
    return ResultCode::StateConflict;
  }
  layer->blend = blend;
  return ResultCode::Ok;
}

ResultCode LayerDocument::set_opacity(const std::string& id, int opacity) {
  if (!editable() || opacity < 0 || opacity > 100) {
    return ResultCode::StateConflict;
  }
  LayerView* layer = find(id);
  if (layer == nullptr || layer->locked) {
    return ResultCode::StateConflict;
  }
  layer->opacity = opacity;
  return ResultCode::Ok;
}

ResultCode LayerDocument::set_locked(const std::string& id, bool locked) {
  if (!editable()) {
    return ResultCode::StateConflict;
  }
  LayerView* layer = find(id);
  if (layer == nullptr) {
    return ResultCode::StateConflict;
  }
  layer->locked = locked;
  return ResultCode::Ok;
}

ResultCode LayerDocument::set_hidden(const std::string& id, bool hidden) {
  if (!editable()) {
    return ResultCode::StateConflict;
  }
  LayerView* layer = find(id);
  if (layer == nullptr || layer->locked) {
    return ResultCode::StateConflict;
  }
  layer->hidden = hidden;
  return ResultCode::Ok;
}

ResultCode LayerDocument::add_layer(const std::string& id, std::string name) {
  if (!editable() || !safe_id(id) || id == "background" || !safe_name(name) || find(id) != nullptr) {
    return ResultCode::StateConflict;
  }
  layers_.push_back(make_layer(id, std::move(name)));
  return ResultCode::Ok;
}

ResultCode LayerDocument::add_group(const std::string& id, std::string name, const std::string& parent_id) {
  if (!editable() || !safe_id(id) || !safe_name(name) || find(id) != nullptr || !parent_ok(parent_id)) {
    return ResultCode::StateConflict;
  }
  LayerView group = make_layer(id, std::move(name));
  group.group = true;
  group.parent_id = parent_id;
  layers_.push_back(std::move(group));
  return ResultCode::Ok;
}

ResultCode LayerDocument::group_layer(const std::string& layer_id, const std::string& group_id,
                                      std::string group_name) {
  if (!editable() || !safe_id(group_id) || !safe_name(group_name) || find(group_id) != nullptr) {
    return ResultCode::StateConflict;
  }
  LayerView* layer = find(layer_id);
  if (layer == nullptr || layer->background || layer->locked) {
    return ResultCode::StateConflict;
  }
  const std::string parent = layer->parent_id;
  LayerView group = make_layer(group_id, std::move(group_name));
  group.group = true;
  group.parent_id = parent;
  layers_.push_back(std::move(group));
  find(layer_id)->parent_id = group_id;
  return ResultCode::Ok;
}

ResultCode LayerDocument::remove(const std::string& id) {
  if (!editable()) {
    return ResultCode::StateConflict;
  }
  const LayerView* root = find(id);
  if (root == nullptr || root->background || root->locked) {
    return ResultCode::StateConflict;
  }
  std::vector<std::string> doomed;
  doomed.push_back(id);
  for (std::size_t index = 0; index < doomed.size(); ++index) {
    for (const auto& layer : layers_) {
      if (layer.parent_id != doomed[index]) {
        continue;
      }
      bool seen = false;
      for (const auto& gone : doomed) {
        if (gone == layer.id) {
          seen = true;
        }
      }
      if (seen) {
        continue;
      }
      if (layer.background || layer.locked) {
        return ResultCode::StateConflict;
      }
      doomed.push_back(layer.id);
    }
  }
  std::vector<LayerView> kept;
  for (const auto& layer : layers_) {
    bool drop = false;
    for (const auto& gone : doomed) {
      if (layer.id == gone) {
        drop = true;
      }
    }
    if (!drop) {
      kept.push_back(layer);
    }
  }
  layers_ = std::move(kept);
  if (current_generation_layer_id_ == id) {
    current_generation_layer_id_.clear();
  }
  return ResultCode::Ok;
}

ResultCode LayerDocument::remove_child(const std::string& parent_id, int index) {
  if (!parent_ok(parent_id)) {
    return ResultCode::StateConflict;
  }
  const std::vector<std::string> children = child_ids(parent_id);
  if (index < 0 || static_cast<std::size_t>(index) >= children.size()) {
    return ResultCode::StateConflict;
  }
  return remove(children[static_cast<std::size_t>(index)]);
}

ResultCode LayerDocument::move_child(const std::string& id, const std::string& parent_id, int index) {
  if (!editable() || !parent_ok(parent_id)) {
    return ResultCode::StateConflict;
  }
  LayerView* node = find(id);
  if (node == nullptr || node->background || node->locked || is_descendant(id, parent_id)) {
    return ResultCode::StateConflict;
  }
  std::vector<std::string> siblings;
  for (const auto& layer : layers_) {
    if (layer.parent_id == parent_id && layer.id != id) {
      siblings.push_back(layer.id);
    }
  }
  if (index < 0 || static_cast<std::size_t>(index) > siblings.size()) {
    return ResultCode::StateConflict;
  }
  LayerView moved = *node;
  moved.parent_id = parent_id;
  layers_.erase(layers_.begin() + static_cast<std::ptrdiff_t>(node - layers_.data()));
  std::size_t at = layers_.size();
  if (static_cast<std::size_t>(index) < siblings.size()) {
    for (std::size_t position = 0; position < layers_.size(); ++position) {
      if (layers_[position].id == siblings[static_cast<std::size_t>(index)]) {
        at = position;
        break;
      }
    }
  }
  layers_.insert(layers_.begin() + static_cast<std::ptrdiff_t>(at), std::move(moved));
  return ResultCode::Ok;
}

ResultCode LayerDocument::add_mask(const std::string& id) {
  if (!editable()) {
    return ResultCode::StateConflict;
  }
  LayerView* layer = find(id);
  if (layer == nullptr || layer->group || layer->locked) {
    return ResultCode::StateConflict;
  }
  layer->mask = true;
  return ResultCode::Ok;
}

ResultCode LayerDocument::remove_mask(const std::string& id) {
  if (!editable()) {
    return ResultCode::StateConflict;
  }
  LayerView* layer = find(id);
  if (layer == nullptr || !layer->mask || layer->locked) {
    return ResultCode::StateConflict;
  }
  layer->mask = false;
  layer->mask_density = 100;
  layer->mask_feather = 0;
  return ResultCode::Ok;
}

ResultCode LayerDocument::set_mask_density(const std::string& id, int density) {
  if (!editable() || density < 0) {
    return ResultCode::StateConflict;
  }
  LayerView* layer = find(id);
  if (layer == nullptr || !layer->mask || layer->locked) {
    return ResultCode::StateConflict;
  }
  layer->mask_density = density;
  return ResultCode::Ok;
}

ResultCode LayerDocument::set_mask_feather(const std::string& id, int feather) {
  if (!editable() || feather < 0) {
    return ResultCode::StateConflict;
  }
  LayerView* layer = find(id);
  if (layer == nullptr || !layer->mask || layer->locked) {
    return ResultCode::StateConflict;
  }
  layer->mask_feather = feather;
  return ResultCode::Ok;
}

ResultCode LayerDocument::set_selection(bool inverted, int feather) {
  if (!editable() || feather < 0 || feather > 1000) {
    return ResultCode::StateConflict;
  }
  selection_.present = true;
  selection_.inverted = inverted;
  selection_.feather = feather;
  return ResultCode::Ok;
}

ResultCode LayerDocument::clear_selection() {
  if (!editable()) {
    return ResultCode::StateConflict;
  }
  selection_ = SelectionView{};
  return ResultCode::Ok;
}

ResultCode LayerDocument::place_result(const std::string& result_id, const std::string& shape,
                                       const std::string& tool_id) {
  if (!editable() || !safe_id(result_id) || !safe_name(tool_id)) {
    return ResultCode::StateConflict;
  }
  for (const auto& generation : generations_) {
    if (generation.result_id == result_id) {
      return ResultCode::Ok;
    }
  }
  const bool known = shape == "新图层" || shape == "替换当前生成图层" || shape == "细节层加效果层" ||
                     shape == "图层组" || shape == "可编辑蒙版的图层" || shape == "可改位置和大小的图层";
  if (!known) {
    return ResultCode::StateConflict;
  }
  if (shape == "替换当前生成图层") {
    LayerView* current = find(current_generation_layer_id_);
    if (current != nullptr && !current->background && !current->locked) {
      current->name = tool_id;
      generations_.push_back(GenerationView{result_id, shape, tool_id});
      return ResultCode::Ok;
    }
  }
  auto adopt = [&](LayerView layer) {
    current_generation_layer_id_ = layer.id;
    layers_.push_back(std::move(layer));
  };
  if (shape == "细节层加效果层") {
    if (find(result_id + "#detail") != nullptr || find(result_id + "#effect") != nullptr) {
      return ResultCode::StateConflict;
    }
    layers_.push_back(make_layer(result_id + "#detail", "细节"));
    LayerView effect = make_layer(result_id + "#effect", "效果");
    adopt(std::move(effect));
  } else if (shape == "图层组") {
    if (find(result_id) != nullptr) {
      return ResultCode::StateConflict;
    }
    LayerView group = make_layer(result_id, tool_id);
    group.group = true;
    adopt(std::move(group));
  } else {
    if (find(result_id) != nullptr) {
      return ResultCode::StateConflict;
    }
    LayerView layer = make_layer(result_id, tool_id);
    if (shape == "可编辑蒙版的图层") {
      layer.mask = true;
    }
    if (shape == "可改位置和大小的图层") {
      layer.position_editable = true;
    }
    adopt(std::move(layer));
  }
  generations_.push_back(GenerationView{result_id, shape, tool_id});
  return ResultCode::Ok;
}

}  // namespace pixcake
```

`替换当前生成图层` 在已有当前生成图层时，只改那一层的名字并记下新的结果编号，不新增图层。还没有当前生成图层时，按新图层添加，编号用结果编号。同一结果编号再次调用直接返回成功。

- [x] **Step 4: Run test to verify it passes**

Run: `cmake --build build --target test_layer_document && ctest --test-dir build -R '^test_layer_document$' --output-on-failure`

Expected: `test_layer_document` 通过。

- [x] **Step 5: Commit**

```bash
git add kernel/layerdoc tests/kernel/layerdoc CMakeLists.txt tests/CMakeLists.txt tests/arch/check_dependencies.cmake docs/superpowers/plans/2026-11-03-kernel-layer-document.md
git commit -m "$(cat <<'EOF'
feat: keep a creative layer tree without retouch sliders

EOF
)"
```

执行时只在本地提交，不推远端，不建 Pull Request。

---

### Task 2: 保存图层文档并烘焙精修信封

**Files:**
- Modify: `kernel/layerdoc/include/pixcake/layerdoc/layer_document.hpp`
- Modify: `kernel/layerdoc/src/layer_document.cpp`
- Modify: `kernel/layerdoc/CMakeLists.txt`
- Modify: `kernel/editdoc/include/pixcake/editdoc/edit_document.hpp`
- Modify: `kernel/editdoc/src/edit_document.cpp`
- Modify: `tests/kernel/layerdoc/test_layer_document.cpp`

**Interfaces:**
- Consumes: `LayerDocument`、`EditDocument`、`LoadResult`、`atomic_replace`、`open_durable_file`、`FileSystem`
- Produces: `LayerDocument::save`、`LayerDocument::load`、`LayerDocument::encode`、`LayerDocument::decode`、`apply_to_original`、`applied_copy_document`、`tsq_sibling_path`、`EditDocument::effects_baked`、`EditDocument::commit_baked_reset`

- [x] **Step 1: Write the failing test**

在 `test_layer_document.cpp` 增加 `#include "pixcake/editdoc/edit_document.hpp"`、`#include "support/fake_file_system.hpp"`。给 `test_layer_document` 加上 `target_include_directories(test_layer_document PRIVATE ${CMAKE_SOURCE_DIR}/tests)`，链接改为 `pixcake_layerdoc pixcake_editdoc pixcake_warnings`。`pixcake_layerdoc` 改为公开链接 `pixcake_editdoc pixcake_persist pixcake_support pixcake_warnings`。从 `main` 调用下面两个测试。

```cpp
void test_round_trip_keeps_unknown_records_and_rejects_the_retouch_magic() {
  auto document = opened();
  EXPECT(document.add_layer("layer-1", "人物") == pixcake::ResultCode::Ok);
  EXPECT(document.append_unknown_record(9, {7, 8}) == pixcake::ResultCode::Ok);
  EXPECT(document.append_unknown_record(5, {1}) == pixcake::ResultCode::StateConflict);
  const std::vector<std::uint8_t> bytes = document.encode();
  EXPECT(bytes.size() > 4);
  EXPECT(bytes[0] == 'P' && bytes[1] == 'X' && bytes[2] == 'L' && bytes[3] == 'D');
  auto restored = pixcake::LayerDocument::decode(bytes);
  EXPECT(restored.has_value());
  EXPECT(restored->source_image_id() == "img-1");
  EXPECT(restored->unknown_records().size() == 1);
  EXPECT(restored->unknown_records()[0].second == std::vector<std::uint8_t>({7, 8}));

  auto edit = pixcake::EditDocument::create("img-1");
  EXPECT(edit.has_value());
  EXPECT(!pixcake::LayerDocument::decode(edit->encode()).has_value());

  pixcake::test::FakeFileSystem files;
  EXPECT(document.save(files, "/creative/img-1.pxlayer") == pixcake::ResultCode::Ok);
  pixcake::LayerDocument loaded;
  const pixcake::LoadResult opened_file = pixcake::LayerDocument::load(loaded, files, "/creative/missing.pxlayer");
  EXPECT(opened_file.absent);
  EXPECT(!opened_file.code.has_value());
  const pixcake::LoadResult present = pixcake::LayerDocument::load(loaded, files, "/creative/img-1.pxlayer");
  EXPECT(present.code == pixcake::ResultCode::Ok);
  pixcake::LayerView person;
  EXPECT(loaded.find_layer("layer-1", person));
  files.write_error = pixcake::ResultCode::DiskFull;
  EXPECT(document.save(files, "/creative/img-1.pxlayer") == pixcake::ResultCode::DiskFull);
  files.write_error = pixcake::ResultCode::Ok;
  pixcake::LayerDocument kept;
  const pixcake::LoadResult still = pixcake::LayerDocument::load(kept, files, "/creative/img-1.pxlayer");
  EXPECT(still.code == pixcake::ResultCode::Ok);
  EXPECT(kept.find_layer("layer-1", person));

  files.files["/creative/bad.pxlayer"] = {1, 2, 3, 4};
  const pixcake::LoadResult corrupt = pixcake::LayerDocument::load(loaded, files, "/creative/bad.pxlayer");
  EXPECT(corrupt.recovered_empty);
  EXPECT(corrupt.code == pixcake::ResultCode::DocumentCorrupt);
}

void test_apply_bakes_the_original_and_a_new_document_has_no_pixels() {
  auto layers = opened();
  auto edit = *pixcake::EditDocument::create("img-1");
  edit.store_fragment("color.global", 1, {9});
  edit.add_mask_id("mask-1");
  pixcake::test::FakeFileSystem files;
  EXPECT(edit.write_snapshot(files, "/docs/img.pxedit") == pixcake::ResultCode::Ok);
  const auto untouched = edit.revision();
  EXPECT(layers.source_image_id() == edit.image_id());
  EXPECT(edit.fragments().size() == 1);
  EXPECT(!edit.effects_baked());

  EXPECT(pixcake::apply_to_original(layers, edit, untouched, files, "/docs/img.pxedit") == pixcake::ResultCode::Ok);
  EXPECT(edit.effects_baked());
  EXPECT(edit.fragments().empty());
  EXPECT(edit.mask_ids().size() == 1);
  EXPECT(edit.revision() == untouched + 1);
  EXPECT(pixcake::apply_to_original(layers, edit, untouched, files, "/docs/img.pxedit") ==
         pixcake::ResultCode::StateConflict);
  EXPECT(edit.revision() == untouched + 1);
  EXPECT(pixcake::apply_to_original(layers, edit, edit.revision(), files, "/docs/img.pxedit") ==
         pixcake::ResultCode::Ok);
  EXPECT(edit.revision() == untouched + 1);

  auto other = *pixcake::EditDocument::create("img-2");
  EXPECT(pixcake::apply_to_original(layers, other, other.revision(), files, "/docs/img2.pxedit") ==
         pixcake::ResultCode::StateConflict);
  EXPECT(!other.effects_baked());

  auto copy = pixcake::applied_copy_document("img-new");
  EXPECT(copy.has_value());
  EXPECT(copy->image_id() == "img-new");
  EXPECT(copy->fragments().empty());
  EXPECT(!copy->effects_baked());
  std::string tsq;
  EXPECT(pixcake::tsq_sibling_path("/creative", "img-new", tsq) == pixcake::ResultCode::Ok);
  EXPECT(tsq == "/creative/img-new.tsq");
  EXPECT(files.files.count("/creative/img-new.tsq") == 0);
  EXPECT(pixcake::tsq_sibling_path("creative", "img-new", tsq) == pixcake::ResultCode::StateConflict);
}
```

- [x] **Step 2: Run test to verify it fails**

Run: `cmake --build build --target test_layer_document`

Expected: 编译失败，因为还没有 `save`、`apply_to_original` 或 `effects_baked`。

- [x] **Step 3: Write minimal implementation**

`edit_document.hpp` 在 `commit_preview` 声明后加入：

```cpp
  bool effects_baked() const;
  // 清掉能力片段和预览，留下烘焙标记，修订号加一。已经烘焙且没有片段时不写第二次。
  ResultCode commit_baked_reset(std::uint64_t expected_revision, FileSystem& files, const std::string& path);
```

`edit_document.cpp` 的匿名命名空间里加入 `constexpr std::uint32_t kRecordBaked = 10;`，并在文件末尾、命名空间结束前加入：

```cpp
bool EditDocument::effects_baked() const {
  for (const auto& record : records_) {
    if (record.type == kRecordBaked && record.payload.size() == 1 && record.payload[0] == 1) {
      return true;
    }
  }
  return false;
}

ResultCode EditDocument::commit_baked_reset(std::uint64_t expected_revision, FileSystem& files,
                                            const std::string& path) {
  if (expected_revision != revision()) {
    return ResultCode::StateConflict;
  }
  if (effects_baked() && fragments().empty() && preview_.empty()) {
    return ResultCode::Ok;
  }
  EditDocument next = *this;
  next.records_.erase(std::remove_if(next.records_.begin(), next.records_.end(),
                                     [](const Record& record) { return record.type == kRecordFragment; }),
                      next.records_.end());
  if (!next.effects_baked()) {
    Record baked;
    baked.type = kRecordBaked;
    baked.payload.push_back(1);
    next.records_.push_back(std::move(baked));
  }
  next.preview_.clear();
  next.set_revision(revision() + 1);
  const ResultCode saved = atomic_replace(files, path, next.encode());
  if (saved != ResultCode::Ok) {
    return saved;
  }
  *this = std::move(next);
  return ResultCode::Ok;
}
```

`layer_document.hpp` 增加 `#include <utility>`、`#include "pixcake/editdoc/edit_document.hpp"`。在现有 `private:` 之前加入这些公有方法，不要另起一个 `private:` 把原来的成员盖掉。在现有私有成员最后加 `unknown_`。类外、命名空间结束前加入三个自由函数。

```cpp
  static std::optional<LayerDocument> decode(const std::vector<std::uint8_t>& bytes);
  static LoadResult load(LayerDocument& destination, FileSystem& files, const std::string& path);
  std::vector<std::uint8_t> encode() const;
  ResultCode save(FileSystem& files, const std::string& path) const;
  ResultCode append_unknown_record(std::uint32_t type, std::vector<std::uint8_t> payload);
  std::vector<std::pair<std::uint32_t, std::vector<std::uint8_t>>> unknown_records() const;
```

```cpp
  std::vector<std::pair<std::uint32_t, std::vector<std::uint8_t>>> unknown_;
```

```cpp
ResultCode apply_to_original(const LayerDocument& layers, EditDocument& document,
                             std::uint64_t expected_revision, FileSystem& files, const std::string& path);
std::optional<EditDocument> applied_copy_document(const std::string& new_image_id);
ResultCode tsq_sibling_path(const std::string& creative_directory, const std::string& new_image_id,
                            std::string& out);
```

`layer_document.cpp` 增加：

```cpp
#include "pixcake/persist/atomic_file.hpp"
#include "pixcake/support/checksum.hpp"
```

在已有匿名命名空间末尾、`make_layer` 之后加入下面的读写函数。在 `place_result` 之后、`namespace pixcake` 结束之前加入后面的成员函数和三个自由函数。

记录类型：1 来源图片，2 来源修订，3 批量设计，4 选区，5 图层，6 生成记录，7 当前生成图层，8 空文档标记。9 及以上原样进 `unknown_`，写在已知记录后面。文件头 16 字节是 `PXLD`、小端格式版本 1、小端正文长度，然后是正文，最后 8 字节是正文的 FNV-1a 64 位。版本不是 1、魔数不对或校验和不对，`decode` 返回空。正式文件和 `.bak` 都不存在时 `load` 只把 `absent` 设为真，不填 `code`。两边都坏时用 `open_durable_file` 隔离，返回 `recovered_empty` 和文档损坏。

图层正文按顺序是：带长度的编号、名字、父编号、混合，然后四个小端 32 位：不透明度、标志、蒙版密度、蒙版羽化。标志从低位起是分组、锁定、隐藏、背景、蒙版、可改位置。选区是三个小端 32 位：是否存在、是否反选、羽化。

```cpp
constexpr std::uint32_t kLayerSource = 1;
constexpr std::uint32_t kLayerRevision = 2;
constexpr std::uint32_t kLayerBatch = 3;
constexpr std::uint32_t kLayerSelection = 4;
constexpr std::uint32_t kLayerNode = 5;
constexpr std::uint32_t kLayerGeneration = 6;
constexpr std::uint32_t kLayerCurrent = 7;
constexpr std::uint32_t kLayerRecovered = 8;
constexpr std::uint32_t kLayerFormat = 1;

void append_u32(std::vector<std::uint8_t>& out, std::uint32_t value) {
  out.push_back(static_cast<std::uint8_t>(value & 0xffu));
  out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xffu));
  out.push_back(static_cast<std::uint8_t>((value >> 16) & 0xffu));
  out.push_back(static_cast<std::uint8_t>((value >> 24) & 0xffu));
}

void append_u64(std::vector<std::uint8_t>& out, std::uint64_t value) {
  for (int shift = 0; shift < 64; shift += 8) {
    out.push_back(static_cast<std::uint8_t>((value >> shift) & 0xffu));
  }
}

bool read_u32(const std::vector<std::uint8_t>& in, std::size_t offset, std::uint32_t& out) {
  if (offset > in.size() || in.size() - offset < 4) {
    return false;
  }
  out = static_cast<std::uint32_t>(in[offset]) | (static_cast<std::uint32_t>(in[offset + 1]) << 8) |
        (static_cast<std::uint32_t>(in[offset + 2]) << 16) | (static_cast<std::uint32_t>(in[offset + 3]) << 24);
  return true;
}

bool read_u64(const std::vector<std::uint8_t>& in, std::size_t offset, std::uint64_t& out) {
  if (offset > in.size() || in.size() - offset < 8) {
    return false;
  }
  out = 0;
  for (int shift = 0; shift < 64; shift += 8) {
    out |= static_cast<std::uint64_t>(in[offset + static_cast<std::size_t>(shift / 8)]) << shift;
  }
  return true;
}

void append_string(std::vector<std::uint8_t>& out, const std::string& text) {
  append_u32(out, static_cast<std::uint32_t>(text.size()));
  out.insert(out.end(), text.begin(), text.end());
}

bool read_string(const std::vector<std::uint8_t>& in, std::size_t& offset, std::string& out) {
  std::uint32_t size = 0;
  if (!read_u32(in, offset, size) || size > in.size() - (offset + 4)) {
    return false;
  }
  out.assign(in.begin() + static_cast<std::ptrdiff_t>(offset + 4),
             in.begin() + static_cast<std::ptrdiff_t>(offset + 4 + size));
  offset += 4u + size;
  return true;
}

void append_record(std::vector<std::uint8_t>& body, std::uint32_t type, const std::vector<std::uint8_t>& payload) {
  append_u32(body, type);
  append_u32(body, static_cast<std::uint32_t>(payload.size()));
  body.insert(body.end(), payload.begin(), payload.end());
}

std::uint32_t layer_flags(const LayerView& layer) {
  std::uint32_t flags = 0;
  if (layer.group) {
    flags |= 1u;
  }
  if (layer.locked) {
    flags |= 2u;
  }
  if (layer.hidden) {
    flags |= 4u;
  }
  if (layer.background) {
    flags |= 8u;
  }
  if (layer.mask) {
    flags |= 16u;
  }
  if (layer.position_editable) {
    flags |= 32u;
  }
  return flags;
}

bool known_shape(const std::string& shape) {
  return shape == "新图层" || shape == "替换当前生成图层" || shape == "细节层加效果层" || shape == "图层组" ||
         shape == "可编辑蒙版的图层" || shape == "可改位置和大小的图层";
}

bool decode_layer(const std::vector<std::uint8_t>& payload, LayerView& layer) {
  std::size_t offset = 0;
  if (!read_string(payload, offset, layer.id) || !read_string(payload, offset, layer.name) ||
      !read_string(payload, offset, layer.parent_id) || !read_string(payload, offset, layer.blend)) {
    return false;
  }
  std::uint32_t opacity = 0;
  std::uint32_t flags = 0;
  std::uint32_t density = 0;
  std::uint32_t feather = 0;
  if (!read_u32(payload, offset, opacity) || !read_u32(payload, offset + 4, flags) ||
      !read_u32(payload, offset + 8, density) || !read_u32(payload, offset + 12, feather)) {
    return false;
  }
  offset += 16;
  if (offset != payload.size() || !safe_id(layer.id) || !safe_name(layer.name) || !known_blend(layer.blend)) {
    return false;
  }
  if (!layer.parent_id.empty() && !safe_id(layer.parent_id)) {
    return false;
  }
  if (opacity > 100) {
    return false;
  }
  layer.opacity = static_cast<int>(opacity);
  layer.mask_density = static_cast<int>(density);
  layer.mask_feather = static_cast<int>(feather);
  layer.group = (flags & 1u) != 0;
  layer.locked = (flags & 2u) != 0;
  layer.hidden = (flags & 4u) != 0;
  layer.background = (flags & 8u) != 0;
  layer.mask = (flags & 16u) != 0;
  layer.position_editable = (flags & 32u) != 0;
  return true;
}
```

成员函数：

空恢復文件的來源編號是空的，不寫來源記錄；解碼在已有空文件標記時不要求來源記錄，存檔再讀仍是空恢復文件。

```cpp
std::vector<std::uint8_t> LayerDocument::encode() const {
  std::vector<std::uint8_t> body;
  if (!recovered_empty_ || !source_image_id_.empty()) {
    std::vector<std::uint8_t> source;
    append_string(source, source_image_id_);
    append_record(body, kLayerSource, source);
  }
  std::vector<std::uint8_t> revision;
  append_u64(revision, source_revision_);
  append_record(body, kLayerRevision, revision);
  std::vector<std::uint8_t> batch;
  append_u32(batch, batch_design_ ? 1u : 0u);
  append_record(body, kLayerBatch, batch);
  std::vector<std::uint8_t> selection;
  append_u32(selection, selection_.present ? 1u : 0u);
  append_u32(selection, selection_.inverted ? 1u : 0u);
  append_u32(selection, static_cast<std::uint32_t>(selection_.feather));
  append_record(body, kLayerSelection, selection);
  for (const auto& layer : layers_) {
    std::vector<std::uint8_t> payload;
    append_string(payload, layer.id);
    append_string(payload, layer.name);
    append_string(payload, layer.parent_id);
    append_string(payload, layer.blend);
    append_u32(payload, static_cast<std::uint32_t>(layer.opacity));
    append_u32(payload, layer_flags(layer));
    append_u32(payload, static_cast<std::uint32_t>(layer.mask_density));
    append_u32(payload, static_cast<std::uint32_t>(layer.mask_feather));
    append_record(body, kLayerNode, payload);
  }
  for (const auto& generation : generations_) {
    std::vector<std::uint8_t> payload;
    append_string(payload, generation.result_id);
    append_string(payload, generation.shape);
    append_string(payload, generation.tool_id);
    append_record(body, kLayerGeneration, payload);
  }
  std::vector<std::uint8_t> current;
  append_string(current, current_generation_layer_id_);
  append_record(body, kLayerCurrent, current);
  if (recovered_empty_) {
    append_record(body, kLayerRecovered, {1});
  }
  for (const auto& unknown : unknown_) {
    append_record(body, unknown.first, unknown.second);
  }

  std::vector<std::uint8_t> out;
  out.push_back('P');
  out.push_back('X');
  out.push_back('L');
  out.push_back('D');
  append_u32(out, kLayerFormat);
  append_u64(out, body.size());
  out.insert(out.end(), body.begin(), body.end());
  append_u64(out, checksum_fnv1a64(body.data(), body.size()));
  return out;
}

std::optional<LayerDocument> LayerDocument::decode(const std::vector<std::uint8_t>& bytes) {
  if (bytes.size() < 24 || bytes[0] != 'P' || bytes[1] != 'X' || bytes[2] != 'L' || bytes[3] != 'D') {
    return std::nullopt;
  }
  std::uint32_t format = 0;
  std::uint64_t body_size = 0;
  if (!read_u32(bytes, 4, format) || !read_u64(bytes, 8, body_size) || format != kLayerFormat) {
    return std::nullopt;
  }
  if (body_size > bytes.size() - 24 || 16 + body_size + 8 != bytes.size()) {
    return std::nullopt;
  }
  const std::vector<std::uint8_t> body(bytes.begin() + 16, bytes.begin() + 16 + static_cast<std::ptrdiff_t>(body_size));
  std::uint64_t stored = 0;
  if (!read_u64(bytes, 16 + static_cast<std::size_t>(body_size), stored) ||
      stored != checksum_fnv1a64(body.data(), body.size())) {
    return std::nullopt;
  }

  LayerDocument document;
  bool saw_source = false;
  bool saw_revision = false;
  std::size_t offset = 0;
  while (offset < body.size()) {
    std::uint32_t type = 0;
    std::uint32_t size = 0;
    if (!read_u32(body, offset, type) || !read_u32(body, offset + 4, size) || size > body.size() - (offset + 8)) {
      return std::nullopt;
    }
    const std::vector<std::uint8_t> payload(body.begin() + static_cast<std::ptrdiff_t>(offset + 8),
                                            body.begin() + static_cast<std::ptrdiff_t>(offset + 8 + size));
    offset += 8u + size;
    if (type == kLayerSource) {
      std::size_t cursor = 0;
      if (!read_string(payload, cursor, document.source_image_id_) || cursor != payload.size() ||
          !safe_id(document.source_image_id_)) {
        return std::nullopt;
      }
      saw_source = true;
    } else if (type == kLayerRevision) {
      if (size != 8 || !read_u64(payload, 0, document.source_revision_)) {
        return std::nullopt;
      }
      saw_revision = true;
    } else if (type == kLayerBatch) {
      std::uint32_t on = 0;
      if (size != 4 || !read_u32(payload, 0, on) || on > 1) {
        return std::nullopt;
      }
      document.batch_design_ = on == 1;
    } else if (type == kLayerSelection) {
      std::uint32_t present = 0;
      std::uint32_t inverted = 0;
      std::uint32_t feather = 0;
      if (size != 12 || !read_u32(payload, 0, present) || !read_u32(payload, 4, inverted) ||
          !read_u32(payload, 8, feather) || present > 1 || inverted > 1 || feather > 1000) {
        return std::nullopt;
      }
      document.selection_.present = present == 1;
      document.selection_.inverted = inverted == 1;
      document.selection_.feather = static_cast<int>(feather);
    } else if (type == kLayerNode) {
      LayerView layer;
      if (!decode_layer(payload, layer)) {
        return std::nullopt;
      }
      for (const auto& existing : document.layers_) {
        if (existing.id == layer.id) {
          return std::nullopt;
        }
      }
      document.layers_.push_back(std::move(layer));
    } else if (type == kLayerGeneration) {
      std::size_t cursor = 0;
      GenerationView generation;
      if (!read_string(payload, cursor, generation.result_id) || !read_string(payload, cursor, generation.shape) ||
          !read_string(payload, cursor, generation.tool_id) || cursor != payload.size() ||
          !safe_id(generation.result_id) || !known_shape(generation.shape) || !safe_name(generation.tool_id)) {
        return std::nullopt;
      }
      for (const auto& existing : document.generations_) {
        if (existing.result_id == generation.result_id) {
          return std::nullopt;
        }
      }
      document.generations_.push_back(std::move(generation));
    } else if (type == kLayerCurrent) {
      std::size_t cursor = 0;
      if (!read_string(payload, cursor, document.current_generation_layer_id_) || cursor != payload.size()) {
        return std::nullopt;
      }
      if (!document.current_generation_layer_id_.empty() && !safe_id(document.current_generation_layer_id_)) {
        return std::nullopt;
      }
    } else if (type == kLayerRecovered) {
      if (size != 1 || payload[0] != 1) {
        return std::nullopt;
      }
      document.recovered_empty_ = true;
    } else if (type >= 9) {
      document.unknown_.push_back({type, payload});
    } else {
      return std::nullopt;
    }
  }
  if (!document.recovered_empty_ && (!saw_source || !saw_revision)) {
    return std::nullopt;
  }
  for (const auto& layer : document.layers_) {
    if (layer.parent_id.empty()) {
      continue;
    }
    const LayerView* parent = document.find(layer.parent_id);
    if (parent == nullptr || !parent->group || document.is_descendant(layer.id, layer.parent_id)) {
      return std::nullopt;
    }
  }
  return document;
}

LoadResult LayerDocument::load(LayerDocument& destination, FileSystem& files, const std::string& path) {
  LoadResult result;
  const DurableLoad opened = open_durable_file(files, path, [](const std::vector<std::uint8_t>& bytes) {
    return decode(bytes).has_value();
  });
  if (opened.absent) {
    result.absent = true;
    return result;
  }
  if (opened.recovered_empty || opened.code != ResultCode::Ok) {
    if (opened.recovered_empty || opened.code == ResultCode::DocumentCorrupt) {
      destination = LayerDocument{};
      destination.recovered_empty_ = true;
      result.code = ResultCode::DocumentCorrupt;
      result.recovered_empty = true;
      return result;
    }
    result.code = opened.code;
    return result;
  }
  auto decoded = decode(opened.bytes);
  if (!decoded) {
    destination = LayerDocument{};
    destination.recovered_empty_ = true;
    result.code = ResultCode::DocumentCorrupt;
    result.recovered_empty = true;
    return result;
  }
  destination = std::move(*decoded);
  result.code = ResultCode::Ok;
  result.used_backup = opened.used_backup;
  return result;
}

ResultCode LayerDocument::save(FileSystem& files, const std::string& path) const {
  return atomic_replace(files, path, encode());
}

ResultCode LayerDocument::append_unknown_record(std::uint32_t type, std::vector<std::uint8_t> payload) {
  if (type < 9) {
    return ResultCode::StateConflict;
  }
  unknown_.push_back({type, std::move(payload)});
  return ResultCode::Ok;
}

std::vector<std::pair<std::uint32_t, std::vector<std::uint8_t>>> LayerDocument::unknown_records() const {
  return unknown_;
}

ResultCode apply_to_original(const LayerDocument& layers, EditDocument& document, std::uint64_t expected_revision,
                             FileSystem& files, const std::string& path) {
  if (layers.recovered_empty() || layers.source_image_id() != document.image_id()) {
    return ResultCode::StateConflict;
  }
  return document.commit_baked_reset(expected_revision, files, path);
}

std::optional<EditDocument> applied_copy_document(const std::string& new_image_id) {
  return EditDocument::create(new_image_id);
}

ResultCode tsq_sibling_path(const std::string& creative_directory, const std::string& new_image_id, std::string& out) {
  if (creative_directory.size() < 2 || creative_directory.front() != '/' || creative_directory.back() == '/' ||
      creative_directory.find('\0') != std::string::npos || !safe_id(new_image_id)) {
    return ResultCode::StateConflict;
  }
  out = creative_directory + "/" + new_image_id + ".tsq";
  return ResultCode::Ok;
}
```

`apply_to_original` 只在来源图片编号和精修文档一致时调用 `commit_baked_reset`。`applied_copy_document` 只调用 `EditDocument::create`，不写像素。`tsq_sibling_path` 只拼路径，不创建文件。

- [x] **Step 4: Run test to verify it passes**

Run: `cmake --build build --target test_layer_document test_edit_document && ctest --test-dir build --output-on-failure`

Expected: 全量测试通过。`test_edit_document` 仍通过。`arch_dependencies` 通过。

- [x] **Step 5: Commit**

```bash
git add kernel/layerdoc kernel/editdoc tests/kernel/layerdoc docs/superpowers/plans/2026-11-03-kernel-layer-document.md
git commit -m "$(cat <<'EOF'
feat: save the layer document and bake the retouch file

EOF
)"
```

执行时只在本地提交，不推远端，不建 Pull Request。
