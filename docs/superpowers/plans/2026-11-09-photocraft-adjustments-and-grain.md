# PhotoCraft 调整图层与颗粒移植 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 色阶、可选颜色、3D LUT 采样、normal/multiply/screen 合成和颗粒基础三项改为执行 PhotoCraft 公式（CPU reference 与 GLSL），状态升到 `photocraft-ported`。

**Architecture:** B 族（`crates/compose`）的逐像素函数移植到新文件 `gpu/render/src/adjust.cpp`；Camera Raw 阶段 9 颗粒移植进现有 `camera_raw.cpp`。`render_executor.cpp` 的旧 `apply_levels`/`apply_selective`/`lut3d`/`composite`/`apply_grain` 改为调用移植函数，`kColor`/`kGrain`/合成着色器同步翻译。对照向量继续由 `tools/photocraft_vectors` 生成。

**Tech Stack:** C++17、OpenGL 4.1 GLSL、CMake/CTest、Rust 1.99（只用于生成向量）。

**Spec:** [PhotoCraft 能力来源与 PixCake 移植规范](../specs/2026-11-09-photocraft-reference-and-porting-policy.md)（第 8 节门禁）；上一批：[Camera Raw 接入](2026-11-09-camera-raw-executor-wiring.md)。

## Global Constraints

- PhotoCraft 提交 `885a6ddf27cc58c07faf1722e5c327cd38953657`，MIT；移植文件头写版权与来源提交，并加入 `tests/gpu/check_photocraft_license.cmake`。
- 位深：8 位。色阶用 `levels_q(ch, v, Some(255.0))`（PhotoCraft 对 U8 的 `adjustment_quantum`），由它生成 4096 项表（`LUT_SIZE`），按 `lut()` 采样（表内线性插值、输入裁到 0..1——色阶本身把输入白点以上映射到输出白，裁剪是语义）。
- 参数映射（单位按 PixCake 参数模式，不改注册）：
  - 色阶：PixCake 只有 RGB 复合通道 = Photoshop master；per-channel 为恒等。`in_black = input_black/255`，`in_white = input_white/255`，`gamma = 10^input_gray`，`out_black/out_white = output_*/255`。
  - 可选颜色：PixCake 九目标顺序 red, yellow, green, cyan, blue, magenta, white, neutral, black 与 `selective_color_weights` 一致；墨量 -100..100 直接传；`selective_mode` relative → `relative = true`。
  - 3D LUT：`lut3d_sample(table, n, c, tetrahedral=false)`（PixCake 资源当前是三线性；四面体保留在接口里，不接参数）。
  - 合成：normal/multiply/screen 用 `psblend::composite`（W3C 一般公式，带 alpha）；`LAB_MIX` 不启用。
  - 颗粒：`cell = 1 + grain_size/25`（全分辨率像素），`roughness = grain_roughness/100`，`amount = grain_amount/100·0.12`，`seed = 0`；噪声 `value_noise(fx,fy,seed)·(1−rough) + value_noise(2.3fx,2.3fy,seed+7)·rough`，`fx = x·scale/cell`；权重 `0.4 + 2.4·l·(1−l)`；`g = (n−0.5)·2·amount·weight` 加到三个通道。
  - 颗粒的 PixCake-specific 扩展：`grain_shadows/midtones/highlights` 乘一个分区系数 `zone(l) / zone_default(l)`（`zone` 用现有三角分区，默认 30/50/15 时系数恒为 1，即与 PhotoCraft 完全一致）；`grain_color` 在单色噪声上混入逐通道独立噪声（种子 `seed+1..3`，同一公式），比例 `grain_color/100`。
- 全分辨率坐标：`RenderInputs` 新增 `float pixel_scale = 1.0F`（帧像素 / 全分辨率输出像素，0.01..1）与 `std::uint32_t origin_x = 0, origin_y = 0`（帧在全分辨率输出中的位置，供以后分区渲染）；renderd 按预览缩放比填写，exportd 保持 1。颗粒 `scale = 1/pixel_scale`。
- 逐像素顺序不变（上一批 Global Constraints 第 7、8 步），只替换各步内部公式。
- GPU：OpenGL 4.1 没有 64 位整数；`hash01` 照 `crates/gpu/src/camera_raw.wgsl` 的 `mul_wide`/`mul64`/`shr64` 用 `uvec2` 模拟。CPU/GPU 差 ≤ 1 级（整帧与 32 px 分块）。

## Review Focus

1. 色阶 gamma > 1 的暗部“toe”与 gamma ≤ 1 的纯幂函数分支——Task 1 向量覆盖 gamma 0.5/1/1.3/2.0/9.99。
2. 可选颜色 absolute 模式在墨量正负两侧的 `min(1, 2·room)` 缩放——Task 1 向量覆盖九目标 × ±100 × 两种模式。
3. 预览（600/2000/4000 px）与导出的颗粒在同一全分辨率位置一致——Task 3 `test_grain_full_resolution_coordinates`：pixel_scale 0.5 的帧逐像素等于全分辨率帧 2× 下采样位置的颗粒值。
4. 半透明底图上 multiply/screen 的 alpha 合成——Task 3 向量覆盖 backdrop alpha 0/0.5/1。
5. 颗粒在 GPU 上的 64 位哈希模拟——Task 4 C57_C60 `grain_pc` 用例差 ≤ 1。

---

### Task 1: 调整图层逐像素函数移植（B 族）

**Files:**
- Create: `gpu/render/include/pixcake/gpu/adjust.hpp`、`gpu/render/src/adjust.cpp`
- Create: `tests/gpu/test_adjust_port.cpp`、`tests/gpu/data/adjust_vectors.inc`
- Modify: `tools/photocraft_vectors/src/main.rs`（新增 `adjust` 模式）、`gpu/render/CMakeLists.txt`、`tests/CMakeLists.txt`、`tests/gpu/check_photocraft_license.cmake`

**Interfaces:**
- Produces（namespace `pixcake::gpu::adjust`）：
  - `struct LevelsChannel { float in_black = 0, in_white = 1, gamma = 1, out_black = 0, out_white = 1; };`
  - `float levelsGamma(float u, float g);` `float levelsQ(const LevelsChannel&, float v, float quantum);`
  - `std::vector<float> levelsLut(const LevelsChannel& master, float quantum);`（4096 项，per-channel 恒等）
  - `float sampleLut(const std::vector<float>&, float v);`（PhotoCraft `lut`）
  - `std::array<float, 9> selectiveWeights(const std::array<float, 3>& c);`
  - `std::array<float, 3> selectiveColor(const std::array<float, 3>& c, bool relative, const std::array<std::array<float, 4>, 9>& adj);`
  - `std::array<float, 3> lut3dSample(const std::vector<float>& table, std::size_t n, const std::array<float, 3>& c, bool tetrahedral);`
  - `enum class BlendMode { Normal, Multiply, Screen };` `std::array<float, 4> composite(BlendMode, const std::array<float, 4>& backdrop, const std::array<float, 4>& source, float opacity);`

- [ ] **Step 1: 生成向量** — `main.rs adjust`：色阶 6 组 channel（含 gamma 0.5、1、1.3、2.0、9.99，黑白点 44/214，输出 48..255）× 256 级；可选颜色 9 目标 × CMYK 各 ±100 × relative/absolute × 73 个像素（同上一批像素集）；3D LUT 用一张 n=17 的确定性表（`c → (c.g, c.b, c.r)` 加 0.1·sin），trilinear 与 tetrahedral × 73 像素；合成 3 模式 × backdrop alpha {0, 0.5, 1} × source alpha {0.5, 1} × opacity {0.3, 1} × 16 色对。写入 `tests/gpu/data/adjust_vectors.inc`。
- [ ] **Step 2: 写失败测试** — `test_adjust_port.cpp`：`test_levels_matches_photocraft`、`test_selective_matches_photocraft`、`test_lut3d_matches_photocraft`、`test_composite_matches_photocraft`，容差 2e-6；解析测试 `test_levels_identity`（默认 channel 下 `levelsQ(v)` 等于 `round(v·255)/255`）、`test_selective_relative_leaves_white`。
- [ ] **Step 3: 运行** — `cmake --build build-camera-raw --target test_adjust_port`；Expected：编译失败（`adjust.hpp` 不存在）。
- [ ] **Step 4: 实现** `adjust.cpp`，逐行对照 `compose/src/adjust/tone.rs:298-353`、`adjust/color.rs:127-185,271-323`、`psblend.rs:197-230`（normal 分支用 `normal_rgb`）。
- [ ] **Step 5: 运行** — `ctest --test-dir build-camera-raw -R "test_adjust_port|check_photocraft_license"`；Expected：PASS。
- [ ] **Step 6: Commit** — `feat(render): port PhotoCraft levels, selective color, 3D LUT sampling and blending`

### Task 2: 颗粒移植（Camera Raw 阶段 9）

**Files:**
- Modify: `gpu/render/include/pixcake/gpu/camera_raw.hpp`、`gpu/render/src/camera_raw.cpp`
- Modify: `tools/photocraft_vectors/src/main.rs`（`grain` 模式）、`tests/gpu/test_camera_raw_port.cpp`
- Create: `tests/gpu/data/grain_vectors.inc`

**Interfaces:**
- Produces（`pixcake::gpu::camera_raw`）：
  - `float hash01(std::int64_t x, std::int64_t y, std::uint64_t seed);`（`photo_util.rs:329`）
  - `float valueNoise(float x, float y, std::uint64_t seed);`
  - `struct Grain { float cell, roughness, amount; std::uint64_t seed; float scale; };`
  - `Grain grainOf(float amount, float size, float roughness, std::uint64_t seed, float pixel_scale);`
  - `float grainNoise(const Grain&, float x, float y);`（两倍频混合后的 n）
  - `Rgb applyGrain(const Rgb& c, const Grain&, float x, float y);`

- [ ] **Step 1: 向量** — 调 PhotoCraft `develop` 只开颗粒（amount 20/60/100、size 0/25/100、roughness 0/50/100、pixel_scale 1/0.5/0.25，seed 0），16×16 图块逐像素输出。
- [ ] **Step 2: 写失败测试** `test_grain_matches_photocraft`（容差 2e-6）、`test_hash01_known_values`（向量里的 8 个哈希值逐位相等）。
- [ ] **Step 3–5:** 运行看失败 → 实现 → `ctest -R test_camera_raw_port` PASS。
- [ ] **Step 6: Commit** — `feat(render): port PhotoCraft Camera Raw grain`

### Task 3: reference 路径接入

**Files:**
- Modify: `gpu/render/include/pixcake/gpu/render_executor.hpp`（`RenderInputs::pixel_scale/origin_x/origin_y`）、`gpu/render/src/render_executor.cpp`
- Modify: `apps/renderd/src/render_service.cpp`（填写 pixel_scale）
- Test: `tests/gpu/test_reference_color_groups.cpp`、`tests/gpu/test_gpu_facade.cpp`、`tests/apps/test_exportd.cpp` 等钉住旧公式的测试

**Interfaces:**
- Consumes：Task 1、Task 2 的全部函数。
- Produces：`ColorGroups` 持有 `std::vector<float> levels_lut`、`std::array<std::array<float,4>,9> selective_adj`、`bool selective_relative`；`Grain` 改为包含 `camera_raw::Grain` 与扩展系数。`set_color_uniforms`/`set_grain_uniforms` 新布局（Task 4 消费）：levels 用一张 4096×1 `R32F` 纹理 `levels_lut`；选择颜色 `ink0..8` 存原始百分比、`groups1.y` = relative；颗粒 `grain0 = (cell, roughness, amount, scale)`，`grain1 = (shadows, midtones, highlights, colour)`，`grain2 = (origin_x, origin_y, −, −)`。

- [ ] **Step 1: 写失败测试**：`test_levels_uses_photocraft`（色阶 44/214、gamma 10^0.3、输出 48/240，像素 0..255 逐级等于 `levelsLut` 采样）；`test_selective_uses_photocraft`（九目标各一，relative 与 absolute）；`test_layers_use_photocraft_composite`（半透明底图 multiply/screen）；`test_grain_uses_photocraft`（默认扩展值时逐像素等于 `applyGrain`）；`test_grain_full_resolution_coordinates`；快速路径等价测试保留并覆盖新色阶。
- [ ] **Step 2: 运行**；Expected：新测试失败（旧公式）。
- [ ] **Step 3: 实现**：替换 `apply_levels`、`apply_selective`、LUT 采样、`composite`、`apply_grain`；renderd 写入 `pixel_scale = 帧长边 / 全分辨率输出长边`。
- [ ] **Step 4: 更新钉住旧值的测试**，在提交说明里逐条写“旧 → 新”。
- [ ] **Step 5: 运行** `ctest --test-dir build-camera-raw -E "C57_C60|gpu_backend|shader_path"`；Expected：PASS（GPU 测试等 Task 4）。
- [ ] **Step 6: Commit** — `feat(render): reference path runs PhotoCraft levels, selective color, LUT, blending and grain`

### Task 4: GLSL

**Files:** Modify `gpu/render/src/shader_sources.hpp`（`kColor` levels/selective、`kGrain`、合成）、`gpu/render/src/render_executor.cpp`（levels 纹理上传）、`tests/gpu/test_gpu_backend_native.cpp`

- [ ] **Step 1: 新用例**：`levels_pc`（gamma 2.0 带 toe）、`selective_abs_pc`、`grain_pc`（amount 80、size 50、roughness 60）、`grain_ext`（扩展三项非默认 + colour 50）、`layers_alpha`（半透明底图 multiply/screen）。Expected：失败（uniform 不存在）。
- [ ] **Step 2: 实现**：levels 用纹理 `texelFetch` + 线性插值（与 `sampleLut` 同式）；`hash01` 按 `camera_raw.wgsl:230-262` 的 `uvec2` 64 位模拟；合成按 W3C 一般公式。
- [ ] **Step 3: 运行** `./build-camera-raw/tests/test_gpu_backend_native`；Expected：所有用例整帧与 tiles 差 ≤ 1。
- [ ] **Step 4: 全套** `ctest --test-dir build-camera-raw`；Expected：全部通过（另一会话未提交的 shell 改动导致的失败单独说明）。
- [ ] **Step 5: Commit** — `feat(render): GPU path runs PhotoCraft levels, selective color, blending and grain`

### Task 5: 文档

- [ ] 矩阵：色阶、可选颜色、3D LUT（采样）、混合模式、颗粒基础三项改 `photocraft-ported`（范围 8 位 sRGB；3D LUT 资源来源仍未闭环，单独注明）；颗粒扩展四项仍是 `PixCake-specific`，写明默认值下与 PhotoCraft 一致。
- [ ] 公式登记：第 2、5、7、9 节换成移植公式，旧公式移到第 11 节。
- [ ] 来源登记、移植证据（新增“调整图层与颗粒”一节，门禁 1–6 逐条）。
- [ ] 链接检查与 `git diff --check`；Commit `docs: PhotoCraft adjustments and grain are photocraft-ported`。

## 不在本计划内

- 白色/黑色（整图统计）、去雾、高光/阴影（局部拉普拉斯）、纹理/清晰度（空间阶段）——下一份计划。
- 曲线（PixCake 未注册参数）、色阶独立 R/G/B 通道（未注册）、黑白八色带（PixCake-specific）、色彩编辑器（no-equivalent）。
- 16 位/浮点、ICC/RAW。
