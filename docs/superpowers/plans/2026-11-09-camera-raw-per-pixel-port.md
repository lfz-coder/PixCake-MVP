# Camera Raw 逐像素阶段移植 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 把 PhotoCraft Camera Raw 管线中不依赖邻域、不依赖图像统计的逐像素阶段（白平衡与曝光、仅对比度、鲜艳度/饱和度、HSL 八色带、色彩分级）移植为 PixCake 的 C++ reference，并用 PhotoCraft 自己算出的向量证明一致，使这些参数的移植状态从 `legacy-provisional` 升到 `photocraft-reference-only`。

**Architecture:** 新增独立模块 `pixcake::gpu::camera_raw`（`gpu/render` 内的新文件，不改 `render_executor.cpp`），输入输出都是 PhotoCraft `develop()` 使用的显示编码 RGB 浮点值。PhotoCraft 向量由一个只在开发机运行的 Rust 小工具调用 `photocraft_algo::camera_raw::develop` 生成，结果作为 C++ 数据头文件提交，PixCake 构建不依赖 Rust。接入 RenderExecutor 与 GLSL 是下一份计划（见文末“不在本计划内”）。

**Tech Stack:** C++17、CMake、现有 `assert` 风格测试可执行文件；生成向量用 Rust ≥ 1.95（rustup，仅开发机）。

**Spec:** [PhotoCraft 能力来源与 PixCake 移植规范](../specs/2026-11-09-photocraft-reference-and-porting-policy.md)，逐参数目标见[公式登记](../evidence/2026-11-09-pixel-formula-registry.md)。

## Global Constraints

- PhotoCraft 提交固定为 `885a6ddf27cc58c07faf1722e5c327cd38953657`（`third_party/photocraft.lock.cmake`）。
- 移植的 C++ 文件头写：`Ported from PhotoCraft crates/algo/src/camera_raw.rs and camera_raw/per_pixel.rs @ 885a6ddf27cc58c07faf1722e5c327cd38953657, MIT (elected from MIT OR Apache-2.0). Copyright (c) 2026 ArtCraft Team and the PhotoCraft contributors.`
- `kernel` 不得 include 新头文件；新代码不依赖 Qt、OpenGL、Rust。
- 编译选项 `-Wall -Wextra -Werror`（`pixcake_warnings`）。
- 不修改 `gpu/render/src/render_executor.cpp`、`gpu/render/src/shader_sources.hpp`：另一会话正在改它们（共用工作区，见 [[shared-worktree-commits]]）。`tests/CMakeLists.txt` 和 `gpu/render/CMakeLists.txt` 只追加本计划的行，按 hunk 暂存。
- 参数单位与 Camera Raw 相同，直接传值：`temperature/tint/contrast/vibrance/saturation/hsl_*` ∈ −100..100，`exposure` ∈ −5..5 EV，色轮 hue 0..360、sat 0..100、lum −100..100，`grade_blending` 0..100（默认 50），`grade_balance` −100..100（PixCake 未注册，固定 0）。
- 本计划只覆盖 sRGB 编码（PhotoCraft `encoding_gamma = 0`）。

## 移植的数学（来自 PhotoCraft，执行者照此实现）

阶段顺序固定为 1 → 3 → 4 → 6 → 7 → 结尾裁剪；跳过的阶段完全不触碰像素。

- `luma(c) = 0.2126 r + 0.7152 g + 0.0722 b`
- sRGB：`decode(v) = v ≤ 0.04045 ? v/12.92 : ((v+0.055)/1.055)^2.4`；`encode(v) = v ≤ 0.0031308 ? 12.92 v : 1.055 v^(1/2.4) − 0.055`
- **阶段 1**（仅当 temperature、tint、exposure 不全为 0）：`g = [1+0.35t, 1−0.25tn, 1−0.35t]`（t=temperature/100，tn=tint/100）；`gain = g / luma(g) · 2^exposure`；每通道 `encode(max(decode(v)·gain, 0))`。
- **阶段 3**（仅当 contrast ≠ 0）：`k = contrast/100·0.6`；`l0 = max(luma,0)`，`lc = min(l0,1)`，`lv = lc − k·sin(2π·clamp(lc,0,1))/(2π)`，`lv = max(lv,0) + (l0 − lc)`；`l0 > 1e-5` 时每通道乘 `lv/l0`，否则每通道加 `lv − l0`。
- **阶段 4**（vibrance 或 saturation ≠ 0）：`l = luma`，`sat = mx>1e-6 ? (mx−mn)/mx : 0`，`hue = hsv(c).h`，`skin = clamp(1 − |hue−25|/25, 0, 1)·0.5`，`k = 1 + saturation/100 + vibrance/100·(1−sat)·(1−skin)`，每通道 `l + (v−l)·max(k,0)`。
- **阶段 6**（任一 hsl 非 0）：`[h,s,v] = rgb_to_hsv(max(c,0))`；`s ≤ 1e-4` 时不变。否则 `w = band_weights(h)`，`dh/ds/dl = Σ w_k·hsl_{hue,sat,lum}[k]`；`out = hsv_to_rgb(h + dh/100·30, clamp(s·(1+ds/100),0,1), v)`；`luma(out) > 1e-6` 时 `out *= luma(c)/luma(out)`；最后 `c = out · 2^(dl/100·s)`。
- `band_weights(h)`：中心 `[0,30,60,120,180,240,270,300]`，对每个 k：`diff = ((h − c_k + 540) mod 360) − 180`，`span = diff ≥ 0 ? (c_{k+1} − c_k) mod⁺ 360 : (c_k − c_{k−1}) mod⁺ 360`，`w_k = max(1 − |diff|/max(span,1e-3), 0)`。`mod⁺` 为非负余数（Rust `rem_euclid`）；第一个 `%` 是 Rust `%`（被除数非负时与 `fmod` 相同）。
- `rgb_to_hsv`、`hsv_to_rgb`：照搬 `camera_raw.rs:547-577`（hue 用 `rem_euclid(6.0)`；`hsv_to_rgb` 先 `h.rem_euclid(360)/60`，扇区用 `(u32)h`，`x = c·(1 − |fmod(h,2) − 1|)`）。
- **阶段 7**（任一色轮 sat 或 lum 非 0）：每个色轮 `tint = (hsv_to_rgb(hue,1,1) − luma(...)) · sat/100 · 0.3`；`blend = 0.15 + clamp(blending,0,100)/100·0.35`，`bal = clamp(balance,−100,100)/100·0.25`；`l = clamp(luma,0,1)`，`sh = 1 − smoothstep(0, 0.5+bal+blend, l)`，`hi = smoothstep(0.5+bal−blend, 1, l)`，`mid = max(1−sh−hi, 0)`，`masks = [sh, mid, hi, 1]`；依次对 k=0..3：`c += masks_k·tint_k`；`lum_k ≠ 0` 时 `c *= 2^(masks_k·lum_k/100·0.6)`。`smoothstep(e0,e1,x)`：`t = clamp((x−e0)/(e1−e0),0,1)`，`t²(3−2t)`。
- **结尾**：设置不是恒等时，每通道裁剪到 [0,1]（PixCake 只处理 8 位输入，对应 PhotoCraft `float = false`）。

## Review Focus

1. 灰色像素（s ≤ 1e-4）经 HSL 阶段必须原样通过——Task 3 的 `test_hsl_leaves_greys` 固定。
2. 色相正好落在色带中心或 360° 回绕处（如 h = 359.9、h = 0）——Task 3 的 `test_band_weights` 固定 0°、15°、359° 三点。
3. 曝光 +5 / −5 极值导致的溢出与全黑——向量用例 `exposure_max`、`exposure_min`（Task 1）由 Task 4 比对。
4. 纯黑输入（l0 ≤ 1e-5）走对比度的加法分支——Task 2 的 `test_contrast_black` 固定。
5. 设置全为默认（含 blending = 50）时必须逐位不变且不裁剪——Task 2 的 `test_identity` 固定。

---

### Task 1: PhotoCraft 向量生成工具

**Files:**
- Create: `tools/photocraft_vectors/Cargo.toml`
- Create: `tools/photocraft_vectors/src/main.rs`
- Create: `tools/photocraft_vectors/README.md`
- Create（生成物）：`tests/gpu/data/camera_raw_per_pixel_vectors.inc`
- Modify: `.gitignore`（追加 `tools/photocraft_vectors/target/`）

**Interfaces:**
- Produces: `camera_raw_per_pixel_vectors.inc`，内容为 C++ 聚合初始化列表，Task 4 以如下方式包含：

```cpp
struct VectorCase { const char* name; float settings[kSettingCount]; float in[3]; float out[3]; };
static const VectorCase kVectors[] = {
#include "data/camera_raw_per_pixel_vectors.inc"
};
```

  `settings` 的顺序（`kSettingCount = 48`）：temperature, tint, exposure, contrast, vibrance, saturation, hsl_hue[8], hsl_sat[8], hsl_lum[8], 四个色轮各 (hue, sat, lum)（shadows, midtones, highlights, global）, grade_blending, grade_balance。每行 `{"name", {48 个值}, {r,g,b}, {r,g,b}},`，浮点用 `{:.9e}` 加 `f` 后缀。文件头注释写 PhotoCraft 提交和“generated by tools/photocraft_vectors — do not edit”。

- [ ] **Step 1: 安装 Rust 工具链（需用户同意）** — `brew install rustup && rustup-init -y --default-toolchain stable`，确认 `cargo --version` ≥ 1.95。
- [ ] **Step 2: 写 `Cargo.toml`**：独立 package `photocraft-vectors`（不加入任何 workspace，`[workspace]` 空表），`edition = "2024"`，依赖 `photocraft-algo = { path = "../../third_party/photocraft/crates/algo" }`。
- [ ] **Step 3: 写 `main.rs`**：输入像素集合 = `{0.05, 0.3, 0.6, 0.95}³` 的 64 色 + 灰阶 `{0, 0.18, 0.5, 1}` + 纯红/绿/蓝/橙(1,0.5,0)/青(0,1,1)，排成 1×N 图，对每个用例构造 `CameraRaw::default()` 后改字段，调用 `develop(&mut px, n, 1, &p, false)`，逐像素输出一行。用例：
  - `identity`（全默认）
  - `wb_warm` t=60 tn=−20；`wb_cool` t=−100 tn=100；`exposure_up` 1.5；`exposure_max` 5；`exposure_min` −5
  - `contrast_up` 100；`contrast_down` −100
  - `vibrance_up` 80；`vibrance_down` −100；`saturation_up` 60；`saturation_down` −100
  - 八个色带各一个 `hsl_<band>`：hue=60、sat=−50、lum=40 同时设在该带
  - `grade_shadows`(hue 220, sat 60, lum −30)、`grade_midtones`(40,50,20)、`grade_highlights`(50,70,30)、`grade_global`(300,25,−10)；`grade_blend0` 与 `grade_blend100`（三个区色轮同 grade_shadows/midtones/highlights 一起设，blending 分别 0/100）；`grade_balance_pos`（balance 60）
  - `everything`：上面 wb_warm + exposure 0.7 + contrast 40 + vibrance 30 + saturation −10 + hsl_orange + 三区色轮 + blending 70
  所有用例 highlights/shadows/whites/blacks/texture/clarity/dehaze/grain/vignette/curves/calibration 都为 0，因此 `develop` 只跑逐像素阶段。
- [ ] **Step 4: 运行并写出生成物** — `cargo run --release --manifest-path tools/photocraft_vectors/Cargo.toml > tests/gpu/data/camera_raw_per_pixel_vectors.inc`；Expected：行数 = 用例数 × 像素数，首行注释含 `885a6ddf27cc58c07faf1722e5c327cd38953657`。
- [ ] **Step 5: README** 写清用途、复现命令、PhotoCraft 提交、“不参与 CMake 构建”。
- [ ] **Step 6: Commit** — `git add tools/photocraft_vectors/Cargo.toml tools/photocraft_vectors/Cargo.lock tools/photocraft_vectors/src/main.rs tools/photocraft_vectors/README.md tests/gpu/data/camera_raw_per_pixel_vectors.inc .gitignore`；`git commit -m "test(render): PhotoCraft Camera Raw per-pixel vectors and their generator"`

### Task 2: 模块骨架、白平衡/曝光与对比度

**Files:**
- Create: `gpu/render/include/pixcake/gpu/camera_raw.hpp`
- Create: `gpu/render/src/camera_raw.cpp`
- Modify: `gpu/render/CMakeLists.txt`（`add_library` 追加 `src/camera_raw.cpp`）
- Create: `tests/gpu/test_camera_raw_port.cpp`
- Modify: `tests/CMakeLists.txt`（追加 `test_camera_raw_port`，仿照 `test_reference_color_groups` 的三行，并 `target_include_directories(... PRIVATE ${CMAKE_SOURCE_DIR}/tests/gpu)`）

**Interfaces:**
- Produces（命名空间 `pixcake::gpu::camera_raw`）：

```cpp
using Rgb = std::array<float, 3>;
struct Wheel { float hue = 0.0F, sat = 0.0F, lum = 0.0F; };
struct Settings {
  float temperature = 0.0F, tint = 0.0F, exposure = 0.0F, contrast = 0.0F;
  float vibrance = 0.0F, saturation = 0.0F;
  std::array<float, 8> hsl_hue{}, hsl_sat{}, hsl_lum{};   // red, orange, yellow, green, aqua, blue, purple, magenta
  std::array<Wheel, 4> grade{};                           // shadows, midtones, highlights, global
  float grade_blending = 50.0F, grade_balance = 0.0F;
};
bool isIdentity(const Settings& s);
float srgbToLinear(float v);
float linearToSrgb(float v);
float luma(const Rgb& c);
Rgb whiteBalanceGains(float temperature, float tint);
// One display-encoded (sRGB) pixel through stages 1, 3, 4, 6, 7 and the final clip.
Rgb developPixel(const Rgb& encoded, const Settings& s);
```

  `developPixel` 每次调用自行计算常量；性能优化（预解析结构）留给接入计划。本任务只实现阶段 1、3 和结尾裁剪，阶段 4/6/7 在 Task 3 加入。

- [ ] **Step 1: 写失败测试**

```cpp
void test_identity() {           // 默认设置（含 blending = 50）逐位不变
  Settings s;
  assert(isIdentity(s));
  for (Rgb c : {Rgb{0.2F,0.4F,0.6F}, Rgb{1.0F,0.0F,0.5F}}) assert(developPixel(c, s) == c);
}
void test_white_balance_gains() {
  const Rgb g = whiteBalanceGains(100.0F, -100.0F);
  assert(near(g[0], 1.35F) && near(g[1], 1.25F) && near(g[2], 0.65F));
}
void test_exposure_one_stop() {  // 中灰线性值翻倍
  Settings s; s.exposure = 1.0F;
  const float in = linearToSrgb(0.18F);
  const Rgb out = developPixel({in, in, in}, s);
  assert(near(srgbToLinear(out[0]), 0.36F, 1e-5F));
}
void test_contrast_black() {     // l0 ≤ 1e-5 走加法分支，结果裁剪后仍为 0
  Settings s; s.contrast = 100.0F;
  assert(developPixel({0.0F,0.0F,0.0F}, s) == (Rgb{0.0F,0.0F,0.0F}));
}
void test_contrast_keeps_mid_grey() { // sin(2π·0.5)=0：中灰不动
  Settings s; s.contrast = 100.0F;
  const Rgb out = developPixel({0.5F,0.5F,0.5F}, s);
  assert(near(out[0], 0.5F, 1e-6F));
}
```

  `near(a, b, tol = 1e-6F)` 为测试内的 `std::fabs(a-b) <= tol`。

- [ ] **Step 2: 运行确认失败** — `cmake --build build --target test_camera_raw_port`；Expected：编译失败，`camera_raw.hpp` 不存在。
- [ ] **Step 3: 实现** 头文件声明与 `camera_raw.cpp` 中的阶段 1、阶段 3、结尾裁剪，数学见上文“移植的数学”；文件头写 Global Constraints 里的移植声明。
- [ ] **Step 4: 运行确认通过** — `cmake --build build --target test_camera_raw_port && ctest --test-dir build -R test_camera_raw_port --output-on-failure`；Expected：`100% tests passed`。
- [ ] **Step 5: Commit** — 只暂存本任务的 hunk（`tests/CMakeLists.txt` 用 `git diff -U0 tests/CMakeLists.txt` 取出自己的 hunk，`git apply --cached`）；`git commit -m "feat(render): port Camera Raw white balance, exposure and contrast from PhotoCraft"`

### Task 3: 鲜艳度/饱和度、HSL 八色带、色彩分级

**Files:**
- Modify: `gpu/render/src/camera_raw.cpp`
- Modify: `gpu/render/include/pixcake/gpu/camera_raw.hpp`（追加两个声明）
- Modify: `tests/gpu/test_camera_raw_port.cpp`

**Interfaces:**
- Consumes: Task 2 的 `Settings`、`developPixel`、`luma`。
- Produces：`std::array<float, 8> bandWeights(float hue);`、`Rgb hsvToRgb(float h, float s, float v);`（测试用，也供接入计划的 GLSL 对照）。

- [ ] **Step 1: 写失败测试**

```cpp
void test_band_weights() {
  auto w0 = bandWeights(0.0F);    assert(near(w0[0], 1.0F));
  auto w15 = bandWeights(15.0F);  assert(near(w15[0], 0.5F) && near(w15[1], 0.5F));
  auto w359 = bandWeights(359.0F); assert(near(w359[0] + w359[7], 1.0F) && w359[0] > w359[7]);
}
void test_hsl_leaves_greys() {
  Settings s; s.hsl_hue.fill(100.0F); s.hsl_sat.fill(100.0F); s.hsl_lum.fill(100.0F);
  const Rgb g{0.4F, 0.4F, 0.4F};
  assert(developPixel(g, s) == g);
}
void test_saturation_minus_100_greys_out() {
  Settings s; s.saturation = -100.0F;
  const Rgb out = developPixel({0.8F, 0.2F, 0.1F}, s);
  assert(near(out[0], out[1], 1e-6F) && near(out[1], out[2], 1e-6F));
}
void test_grade_global_tint_direction() { // 全局色轮偏红：R 上升、B 下降
  Settings s; s.grade[3] = {0.0F, 100.0F, 0.0F};
  const Rgb out = developPixel({0.5F, 0.5F, 0.5F}, s);
  assert(out[0] > 0.5F && out[2] < 0.5F);
}
```

- [ ] **Step 2: 运行确认失败** — 同 Task 2 Step 4 命令；Expected：`bandWeights` 未声明导致编译失败。
- [ ] **Step 3: 实现** 阶段 4、6、7 与 `rgb_to_hsv`/`hsvToRgb`/`bandWeights`/`smoothstep`，插在阶段 3 之后、结尾裁剪之前；`isIdentity` 补上 presence/hsl/grade 判定。
- [ ] **Step 4: 运行确认通过** — Expected：`100% tests passed`。
- [ ] **Step 5: Commit** — `git commit -m "feat(render): port Camera Raw presence, color mixer and color grading from PhotoCraft"`

### Task 4: 与 PhotoCraft 向量逐像素比对

**Files:**
- Modify: `tests/gpu/test_camera_raw_port.cpp`

**Interfaces:**
- Consumes: Task 1 的 `.inc`（字段顺序见 Task 1），Task 2/3 的 `Settings`、`developPixel`。

- [ ] **Step 1: 写测试** `test_matches_photocraft_vectors()`：按 Task 1 的顺序把 48 个值填进 `Settings`，对每行断言三个通道 `|ours − photocraft| ≤ 2e-6`（同为 CPU f32，差异只来自 libm 的 `pow`/`sin` 几个 ulp）；失败时 `std::fprintf(stderr, ...)` 打印用例名、输入、两边输出再 `assert`。另断言 `kVectors` 行数大于 0，并统计用例名个数等于 Task 1 列出的用例数（28）。
- [ ] **Step 2: 运行** — Expected：通过。若某用例超出容差，先对照 `camera_raw.rs` 对应段落查移植错误，不得放宽容差；确属 libm 差异时把容差提到 `1e-5` 并在测试注释写明原因和最大观测差。
- [ ] **Step 3: 变异检查** — 临时把阶段 7 的 `0.3` 改成 `0.31`，确认 `grade_*` 用例失败后改回；在提交信息中记录。
- [ ] **Step 4: Commit** — `git commit -m "test(render): Camera Raw per-pixel port matches PhotoCraft vectors"`

### Task 5: 状态文档

**Files:**
- Modify: `docs/superpowers/evidence/2026-11-09-pixel-processing-status-matrix.md`
- Modify: `docs/superpowers/evidence/2026-11-09-pixel-formula-registry.md`
- Create: `docs/superpowers/evidence/2026-11-09-camera-raw-per-pixel-port.md`

- [ ] **Step 1: 写证据文件**：PhotoCraft 提交、移植文件、向量用例与像素数、最大观测差、变异检查结果、未覆盖项（power-law 编码、F32、白色/黑色、曲线、颗粒、空间阶段、GPU、预览/导出）。
- [ ] **Step 2: 更新矩阵**：`temperature/tint`、`exposure`、`contrast`、`vibrance/saturation`、HSL 八色带、色彩分级六行的“移植状态”改为 `photocraft-reference-only`，“CPU reference”写“有（PhotoCraft 移植，未接入 executor）”；GPU、预览、导出列保持旧路径描述并注明“仍跑 legacy 公式”。验收结论里“目前没有任何一项达到”改为列出这六行。
- [ ] **Step 3: 更新公式登记** 对应条目的当前状态句。
- [ ] **Step 4: 检查** — 运行文档链接检查脚本（同 2026-11-09 文档统一时用的 Python 片段）与 `git diff --check`；Expected：无新增坏链接、无空白错误。
- [ ] **Step 5: Commit** — `git commit -m "docs: Camera Raw per-pixel stages are photocraft-reference-only"`

## 不在本计划内（下一份计划）

- 接入 `RenderExecutor` 的 reference 路径（替换 `tone()`/`color()`/`apply_hsl()`/`apply_grading()` 中对应部分）并决定亮度在阶段 1 之后的插入点——等另一会话提交 `render_executor.cpp` 当前改动后再做。
- GLSL 翻译与 readback parity（对照 `crates/gpu/src/camera_raw.wgsl` 与 `crates/gpu/tests/it/camera_raw.rs`，容差 1/4096）。
- 线性输入契约下的 transfer 适配：现在 macOS 解码器实际输出 sRGB 编码值，接入时须在 executor 边界写明这一点。
- 白色/黑色（需要整幅图像统计）、曲线（参数未注册）、颗粒、空间阶段（高光/阴影、纹理、清晰度、去雾）。
