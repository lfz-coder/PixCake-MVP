# Camera Raw 移植接入渲染执行器 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 让预览和导出真正执行 PhotoCraft 的公式：`RenderExecutor` 的 CPU reference 与 OpenGL 路径改用已移植的 Camera Raw 逐像素阶段（白平衡、曝光、对比度、鲜艳度/饱和度、HSL 八色带、色彩分级）和 Photoshop 现代亮度，使这些参数满足移植规范第 8 节门禁，从 `photocraft-reference-only` 升到 `photocraft-ported`（范围：8 位 sRGB 输入）。

**Architecture:** `pixcake::gpu::camera_raw` 拆成按阶段调用的函数（`developPixel` 由它们组成，向量测试守住不变），新增 `pixcake::gpu::brightness`（`modern_lut` 移植）。`render_executor.cpp` 的 `color()`/`tone()`/`apply_hsl()`/`apply_grading()` 中对应部分改为调用这些函数，旧的高光/阴影/白色/黑色与清晰度/纹理/去雾、色阶、黑白、可选颜色和 AI 风格偏移保留为 legacy 并在新顺序中占位。GLSL 同步翻译，C57_C60 原生 CGL readback 比对 GPU 与 reference。

**Tech Stack:** C++17、GLSL 330、现有 assert 测试；PhotoCraft 向量用 `tools/photocraft_vectors`（Rust）。

**Spec:** [PhotoCraft 能力来源与 PixCake 移植规范](../specs/2026-11-09-photocraft-reference-and-porting-policy.md)；上一步计划与证据：[逐像素移植](2026-11-09-camera-raw-per-pixel-port.md)、[移植证据](../evidence/2026-11-09-camera-raw-per-pixel-port.md)。

## Global Constraints

- PhotoCraft 提交 `885a6ddf27cc58c07faf1722e5c327cd38953657`；移植文件头沿用上一计划的 MIT 声明并指向 `third_party/photocraft-LICENSES/LICENSE-MIT`（`check_photocraft_license` 会检查新文件，见 Task 2）。
- 不改 `RenderDescription`、参数模式、`kernel`；只改 `gpu/render`、`tests/gpu`、`tools/photocraft_vectors` 和文档。
- 像素域：executor 读到的 RGBA8 就是解码器输出的 sRGB 编码值，直接作为 Camera Raw 的显示编码输入（`encoding_gamma = 0`）。不在 executor 内做线性化；在代码注释和矩阵中写明这一点。
- 每像素管线顺序（global/local/post 共用，后四步只属于 `color.global`）：
  1. Camera Raw 阶段 1（temperature、tint、exposure）
  2. legacy AI 风格偏移（`amount`，原 `color()` 中 style）
  3. 现代亮度（`brightness`，4096 点 LUT，逐通道）
  4. legacy 影调占位：高光/阴影/白色/黑色 lift 与清晰度/纹理/去雾 local（原 `tone()` 中这两段，系数不变）
  5. Camera Raw 阶段 3 对比度
  6. Camera Raw 阶段 4 鲜艳度/饱和度
  7. legacy 色阶 → 黑白档时 legacy 黑白，否则 Camera Raw 阶段 6 HSL → legacy 可选颜色 → Camera Raw 阶段 7 色彩分级
  8. 颗粒、合成、写回（写回时裁剪并量化，与现在相同）

  这个顺序中 1、5、6、7(HSL、分级) 按 Camera Raw；3 插在阶段 1 之后（用户裁决 2026-11-09）；2、4、色阶、黑白、可选颜色是 PixCake 现有 legacy 的位置，不是 PhotoCraft 顺序。
- `grade_balance` 固定 0（PixCake 未注册该参数）。
- 快速路径（`scalar_tone_operation`）只保留逐通道独立的 exposure、brightness、levels；contrast 改为基于亮度，必须移出快速路径。

## Review Focus

1. 半透明像素经新管线后的合成必须与旧行为一致（composite 不变）——Task 3 的快速路径等价测试已覆盖半透明，保留并扩到 brightness。
2. 黑白档（profile = monochrome）时不得执行 Camera Raw HSL——Task 3 的 `test_monochrome_skips_color_mixer` 固定。
3. `color.local` 带蒙版的操作走同一新公式——Task 3 的 `test_local_uses_camera_raw` 固定。
4. 亮度为负（压暗分支是反函数采样）在 GPU 上与 LUT 的误差——Task 4 的 C57_C60 新用例 `brightness_down` 固定。
5. 分块导出（32 px tiles）与整帧结果一致——Task 4 沿用 C57_C60 已有的 tiles 比对，对新用例同样执行。

---

### Task 1: 按阶段拆分 camera_raw 模块

**Files:**
- Modify: `gpu/render/include/pixcake/gpu/camera_raw.hpp`
- Modify: `gpu/render/src/camera_raw.cpp`

**Interfaces:**
- Produces（`pixcake::gpu::camera_raw`，`developPixel` 改为调用它们，行为不变）：

```cpp
// Stage 1: gains per channel (white balance normalised to unit luminance times 2^exposure);
// apply encodes(max(decode(v) * gain, 0)).  wbNeutral settings need no call.
Rgb balanceGains(float temperature, float tint, float exposure);
Rgb applyBalance(const Rgb& encoded, const Rgb& gains);
Rgb applyContrast(const Rgb& c, float contrast);                 // slider -100..100, 0 = no call
Rgb applyPresence(const Rgb& c, float vibrance, float saturation);
Rgb applyColorMixer(const Rgb& c, const std::array<float, 8>& hue, const std::array<float, 8>& sat,
                    const std::array<float, 8>& lum);           // greys returned unchanged
struct Grade { std::array<Rgb, 4> tints{}; std::array<float, 4> lums{}; float blend = 0.0F, balance = 0.0F; };
Grade resolveGrade(const std::array<Wheel, 4>& wheels, float blending, float balance);
Rgb applyColorGrading(const Rgb& c, const Grade& g);
bool gradeNeutral(const std::array<Wheel, 4>& wheels);
bool colorMixerNeutral(const std::array<float, 8>& hue, const std::array<float, 8>& sat, const std::array<float, 8>& lum);
```

- [ ] **Step 1: 确认基线** — `ctest --test-dir build-camera-raw -R test_camera_raw_port`；Expected：通过，worst difference 8.34e-07。
- [ ] **Step 2: 重构** 为上面的函数；`balanceGains` 内部顺序保持 `g[i] / norm * std::pow(2.0F, exposure)`。
- [ ] **Step 3: 再跑同一测试** — Expected：通过，worst difference 仍为 8.34e-07（逐位不变；变化说明重构改了运算顺序）。
- [ ] **Step 4: Commit** — `git commit -m "refactor(render): Camera Raw port callable stage by stage"`

### Task 2: 移植 Photoshop 现代亮度

**Files:**
- Create: `gpu/render/include/pixcake/gpu/brightness.hpp`、`gpu/render/src/brightness.cpp`
- Modify: `gpu/render/CMakeLists.txt`（加 `src/brightness.cpp`）
- Modify: `tools/photocraft_vectors/Cargo.toml`（加 `photocraft-compose = { path = "../../third_party/photocraft/crates/compose" }`）、`tools/photocraft_vectors/src/main.rs`（新增第二个输出）、`tools/photocraft_vectors/README.md`
- Create（生成物）：`tests/gpu/data/modern_brightness_vectors.inc`
- Create: `tests/gpu/test_brightness_port.cpp`；Modify: `tests/CMakeLists.txt`（三行 + include 目录 `tests/gpu`）
- Modify: `tests/gpu/check_photocraft_license.cmake`（把两个新文件加入 `ported` 列表）

**Interfaces:**
- Produces（`pixcake::gpu::brightness`）：

```cpp
constexpr std::size_t kLutSize = 4096;
// PhotoCraft compose::adjust::modern_lut(brightness, 0): 4096 samples over 0..1.
std::vector<float> modernLut(float brightness);
// compose::adjust::tone::lut: clamp v to 0..1, linear between neighbouring samples.
float sampleLut(const std::vector<float>& table, float v);
// Constants of the raised curve for the GPU (Brightness::new of |brightness|).
struct Curve { bool raise = true; float s = 1, e = 1, x0 = 1, alpha = 1; bool hermite = true; };
Curve curve(float brightness);
float raised(const Curve& c, float x);
```

- 数学：照搬 `crates/compose/src/adjust/tone.rs:155-259`（`Brightness::new`、`raised`、`modern_lut` 的升/降两支，降支在 `4·4096` 个细采样上单调反查）。`modern_contrast(b, 0)` 为恒等，最后 `clamp(0,1)` 保留。
- 向量：`main.rs` 读取命令行参数 `brightness` 时输出 `modern_brightness_vectors.inc`：对 brightness ∈ {−150, −100, −50, −10, −1, 1, 10, 50, 100, 150}，每 64 个样本取一个（k = 0, 64, …, 4032）再加 k = 4095，行格式 `{brightness, k, value},`。

- [ ] **Step 1: 生成向量** — `cargo run --release --manifest-path tools/photocraft_vectors/Cargo.toml -- brightness > tests/gpu/data/modern_brightness_vectors.inc`（无参数时仍输出 Camera Raw 向量，生成物逐字节不变：`git diff --exit-code tests/gpu/data/camera_raw_per_pixel_vectors.inc`）。Expected：10 × 65 = 650 行。
- [ ] **Step 2: 写失败测试** `test_brightness_port.cpp`：`test_matches_photocraft_lut()`（650 行，`|ours − photocraft| ≤ 2e-6`）、`test_zero_is_identity()`（`modernLut(0)[k] == k/4095.0f`）、`test_sample_lut_clamps()`（`sampleLut(t, -0.5) == t[0]`、`sampleLut(t, 1.5) == t[4095]`）、`test_curve_matches_lut()`（brightness ∈ {−100, −10, 10, 100}，在 x = 0, 0.1, …, 1 上：升支 `raised(curve(b), x)` 与 `modernLut(b)` 采样差 ≤ 2e-6；降支用 32 次二分求 `raised(curve(−b), y) = x` 的 y 与 `sampleLut(modernLut(b), x)` 差 ≤ 1e-4——这是 GPU 将用的算法）。
- [ ] **Step 3: 运行确认失败** — Expected：`brightness.hpp` 不存在导致编译失败。
- [ ] **Step 4: 实现** `brightness.cpp`。
- [ ] **Step 5: 运行确认通过** — `ctest --test-dir build-camera-raw -R "test_brightness_port|check_photocraft_license" --output-on-failure`；Expected：2/2 通过。
- [ ] **Step 6: Commit** — `git commit -m "feat(render): port Photoshop modern brightness from PhotoCraft"`

### Task 3: CPU reference 改用移植公式

**Files:**
- Modify: `gpu/render/src/render_executor.cpp`（`ToneColor`/`tone_color_of`/`color()`/`tone()`/`tone_is_active`、`ColorGroups`/`color_groups()`/`apply_hsl()`/`apply_grading()`/`apply_color_groups()`、`draw_reference()`、`scalar_tone_operation()`/`draw_scalar_tone()`）
- Modify: `tests/gpu/test_reference_color_groups.cpp`
- Modify: `tests/gpu/test_gpu_facade.cpp`（PP-WB-001 Bradford 期望值）

**Interfaces:**
- Consumes: Task 1 的阶段函数、Task 2 的 `modernLut`/`sampleLut`。
- Produces（供 Task 4 设置 uniform）：`ToneColor` 新字段 `bool cr_balance; Rgb cr_gains; float contrast /*slider*/; float vibrance, saturation /*slider*/; float brightness /*slider*/; std::vector<float> brightness_lut;`；`ColorGroups` 新字段 `std::array<float,8> cr_hue, cr_sat, cr_lum; camera_raw::Grade grade; bool hsl, grading;`。旧字段 `white_balance_matrix`、`exposure_gain`、`contrast_amount`、`hue/saturation/lightness`（HSL）、`zone_*` 删除；`highlights/shadows/whites/blacks/local/style` 保留。

- [ ] **Step 1: 写失败测试**（`test_reference_color_groups.cpp`，`Pixel` 期望值由测试内调用 `camera_raw`/`brightness` 函数在 `byte/255` 上算出再 `lround(clamp01(x)*255)`）：
  - `test_global_uses_camera_raw()`：像素 (200, 120, 40)，`temperature 60, tint −20, exposure 0.7, contrast 40, vibrance 30, saturation −10, hsl_hue_orange 60, hsl_saturation_orange −50, hsl_luminance_orange 40, grading_shadows_* (220,60,−30), grading_midtones_* (40,50,20), grading_highlights_* (50,70,30), grading_blending 70`；期望 = `developPixel` 同设置。
  - `test_brightness_after_balance()`：像素 (90, 140, 210)，`exposure 0.5, brightness −40`；期望 = `sampleLut(modernLut(−40), applyBalance(c, balanceGains(0,0,0.5)))`。
  - `test_monochrome_skips_color_mixer()`：`profile monochrome` + `hsl_hue_red 100`，结果等于只有 `profile monochrome` 的结果。
  - `test_local_uses_camera_raw()`：`color.local` 操作带 `exposure 1, contrast 50`、无蒙版；期望 = `applyContrast(applyBalance(c, balanceGains(0,0,1)), 50)` 量化。
  - 快速路径等价测试中加入 brightness-only 与 exposure+brightness+levels 两组（不透明与半透明两张图）。
- [ ] **Step 2: 运行确认失败** — `cmake --build build-camera-raw --target test_reference_color_groups && ./build-camera-raw/tests/test_reference_color_groups`；Expected：`test_global_uses_camera_raw` 断言失败（旧公式）。
- [ ] **Step 3: 实现** 按 Global Constraints 的顺序重写每像素管线；`draw_scalar_tone` 的浮点表改为 `applyBalance`（exposure-only 的 gains）→ `sampleLut(brightness)` → legacy levels；`scalar_tone_operation` 删除 `contrast`。旧的 `white_balance_matrix()`、`tint_matrix()`、Bradford 常量、`apply_exposure()` 若无其他引用则删除。注释写明“输入为解码器输出的 sRGB 编码值”。
- [ ] **Step 4: 更新钉住旧值的测试**：`test_hsl()`、`test_grading()` 的期望值改为 Camera Raw 方向性/数值断言（用 `developPixel` 计算），`test_gpu_facade.cpp` 的 PP-WB-001（D65→D50）改为 `balanceGains(100, 0, 0)` 作用于中灰的期望值。每改一处在提交信息里列出旧值→新值。
- [ ] **Step 5: 运行 CPU 侧全部测试** — `ctest --test-dir build-camera-raw -E "C57_C60|gpu_backend|shader_path" --output-on-failure`；Expected：全部通过（GPU 比对在 Task 4 恢复，此时 C57_C60 预期失败，记录到账本）。
- [ ] **Step 6: Commit** — `git commit -m "feat(render): reference path runs the PhotoCraft Camera Raw and brightness formulas"`

### Task 4: GLSL 同步翻译

**Files:**
- Modify: `gpu/render/src/shader_sources.hpp`（`kColor`）
- Modify: `gpu/render/src/render_executor.cpp`（`set_color_uniforms`）
- Modify: `tests/gpu/test_gpu_backend_native.cpp`（`cases[]` 增加用例）
- Modify: `tests/gpu/test_gpu_shader_path.cpp`（注释与期望说明改为 Camera Raw）

**Interfaces:**
- Consumes: Task 3 的 `ToneColor`/`ColorGroups` 新字段，Task 2 的 `Curve`/`curve()`。
- Uniform 布局（替换 `wb0..2`、`tone0..2` 中被替换的分量；未列出的保持）：
  - `cr0` = (gain.r, gain.g, gain.b, 1=阶段 1 开)
  - `cr1` = (contrast/100·0.6, vibrance, saturation, 1=presence 开)
  - `bright0` = (s, e, x0, alpha)；`bright1` = (1=亮度开, 1=提亮, 1=hermite, −)
  - `tone0` = (style, −, −, −)；`tone1` = (highlights, shadows, whites, blacks)；`tone2` = (local, −, −, −)
  - HSL 复用 `hue_a/b`、`sat_a/b`、`light_a/b`，内容改为滑杆原值（−100..100）
  - 分级复用 `zone_tint0..3`（Grade.tints）、`zone_light`（Grade.lums），`groups1` = (grading 开, absolute, blend, balance)
- GLSL 亮度：提亮 `raised(x)`；压暗用 32 次二分求反函数（与 Task 2 的 `test_curve_matches_lut` 同算法）；`v` 先 `clamp(0,1)`。
- `hsv_to_rgb` 的扇区用 `int(floor(h))` 并 `clamp(0,5)`；`rem_euclid` 用 `x - m*floor(x/m)`。

- [ ] **Step 1: 写失败测试**：`cases[]` 新增 `wb_warm`（temperature 60, tint −20）、`exposure_brightness`（exposure 0.5, brightness 40）、`brightness_down`（brightness −60）、`contrast_cr`（contrast 70）、`presence`（vibrance 60, saturation −30）、`grading_cr`（三区色轮 + blending 20）。容差沿用现有 max diff ≤ 1。
- [ ] **Step 2: 运行确认失败** — `./build-camera-raw/tests/test_gpu_backend_native`；Expected：新用例与 `tone`/`hsl`/`grading` 报 GPU 与 reference 不一致。
- [ ] **Step 3: 翻译** `kColor` 主体按 Global Constraints 顺序；更新 `set_color_uniforms`。
- [ ] **Step 4: 运行** — Expected：全部用例 max diff ≤ 1（整帧与 32 px tiles）；`test_gpu_shader_path`、`test_gpu_backend_qt` 通过。
- [ ] **Step 5: 全套** — `ctest --test-dir build-camera-raw`；Expected：99+ 全部通过。
- [ ] **Step 6: Commit** — `git commit -m "feat(render): GPU path runs the PhotoCraft Camera Raw and brightness formulas"`

### Task 5: 状态与公式文档

**Files:**
- Modify: `docs/superpowers/evidence/2026-11-09-pixel-processing-status-matrix.md`、`docs/superpowers/evidence/2026-11-09-pixel-formula-registry.md`、`docs/superpowers/specs/2026-11-06-pixel-processing-provenance.md`、`docs/superpowers/evidence/2026-11-09-camera-raw-per-pixel-port.md`
- Modify: `docs/superpowers/evidence/2026-11-09-preview-latency-optimization.md`（快速路径不再含 contrast；重跑 `bench_reference_render` 的曝光/色阶两行并更新数字）

- [ ] **Step 1: 矩阵**：白平衡、曝光、对比度、亮度、鲜艳度/饱和度、HSL、色彩分级七行移植状态改为 `photocraft-ported`（范围注明“8 位 sRGB 输入；executor 输入未线性化”），CPU reference/GPU/预览/导出列改为新公式与证据；保留 legacy 的行不变。验收结论改写。
- [ ] **Step 2: 公式登记**：上述参数的“当前实际公式”改为移植公式摘要和管线顺序（复制 Global Constraints 的 8 步），旧公式移到“迁移前记录”小节。
- [ ] **Step 3: 来源登记**：对应条目状态句改为 `photocraft-ported`；`PP-WB-001` Bradford 记录标为已被替换。
- [ ] **Step 4: 证据**：在移植证据追加“接入”一节：C57_C60 新用例 max diff、tiles 结果、快速路径等价测试、bench 数字、门禁 1–6 逐条对应的证据。
- [ ] **Step 5: 检查** — 文档链接脚本与 `git diff --check`；Expected：无新增坏链接。
- [ ] **Step 6: Commit** — `git commit -m "docs: Camera Raw per-pixel stages and brightness are photocraft-ported"`

## 不在本计划内

- 白色/黑色（整幅图像统计）、高光/阴影/纹理/清晰度/去雾（空间阶段）、曲线、颗粒、暗角的移植。
- 输入线性化与 ICC/RAW 色彩管理；Power-law 编码、32 位浮点。
- 色阶、可选颜色、3D LUT、混合的 PhotoCraft 移植（B 族，下一批）。
