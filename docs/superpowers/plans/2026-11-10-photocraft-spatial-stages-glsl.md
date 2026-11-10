# PhotoCraft Camera Raw 整帧阶段 GLSL Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 去雾、高光/阴影、对比度+纹理+清晰度、白色/黑色在 OpenGL 上执行，与 C++ reference（`gpu/render/src/camera_raw_spatial.cpp`）的 8 位输出差 ≤1 级，四行状态从 `photocraft-reference-only` 升到 `photocraft-ported`（8 位 sRGB 输入）。

**Architecture:** GL 封装层增加 `R32F`/`Rgba32F` 纹理与浮点读回。新文件 `camera_raw_gl.cpp` 用多遍片元着色器实现各整帧阶段：浮点帧纹理进出，中间平面是 R32F。分位数、排序、大气光、黑白点这类整图统计，读回小代理平面后在 CPU 上算，函数与 reference 相同。executor 对“帧 ≤ 一个 GPU 分块”的整帧操作在 GPU 上分段执行：`kColor` 前段 → 整帧阶段 → `kColor` 后段 + 回混。更大的帧（分块导出）仍整幅回退 reference。

**Tech Stack:** C++17、OpenGL 3.3 core（macOS CGL；没有 compute shader）、GLSL 330、CMake/CTest。

**Spec:** [PhotoCraft 能力来源与 PixCake 移植规范](../specs/2026-11-09-photocraft-reference-and-porting-policy.md)（第 8 节门禁）；上一批：[空间与整图统计阶段移植](2026-11-09-photocraft-spatial-stages.md)、[整帧阶段移植证据](../evidence/2026-11-09-photocraft-spatial-stages-port.md)。

## Global Constraints

- 门禁原文（规范第 8 节第 4 条）：“CPU 与 OpenGL 的误差在测试声明的容差内（参考 PhotoCraft Camera Raw GPU 测试的 `1/4096`，8 位输出不超过 1 级）……PhotoCraft 只有 CPU 的空间阶段以 PixCake C++ reference 为 oracle。”
  - 本计划的阶段级测试（浮点帧对浮点帧）容差为 `1/4096`。
  - 端到端测试（RGBA8 对 RGBA8）容差为 ≤1 级。
- Oracle 是 `pixcake::gpu::camera_raw` 的 C++ 函数：`dehaze`、`localTone`、`toneDetail`、`whitesBlacks`、`luminancePoints`、`boxMean`、`gauss`、`guided`、`guidedFast`、`gaussBoxRadii`、`bilinear`。不改它们的行为，它们已与 PhotoCraft 逐位一致。
- GLSL 330 core，与 `shader_sources.hpp` 一样以 `kPrelude` 开头。
  - 只用 `texelFetch`，不依赖浮点纹理的线性过滤，也不依赖浮点混合（blending）。
  - 累加用两张纹理来回读写（ping-pong）。
- 浮点纹理一律 NEAREST 过滤、CLAMP_TO_EDGE。越界坐标的处理（edge clamp 或 mirror）在着色器里按 reference 的规则显式计算。
- 统计量在 CPU 上算，用 reference 的同名逻辑：
  - 分位数用 `total_cmp` 顺序的 `nth_element`
  - 大气光用 `stable_sort`
  - 均值按顺序求和
  - 读回的平面 ≤ 1600×1600（local_tone 工作平面）或 ≤ 768（去雾代理）或 ≤ 256（统计副本）
- GPU 整帧路径只在 `width ≤ gpu_tile_ && height ≤ gpu_tile_`（默认 4096）时启用。更大的帧整幅回退 reference，诊断写明原因。
- 任何 GL 失败都返回 false，不写出部分像素：renderd/exportd 的现有逻辑会回退 reference。
- 新代码进 `pixcake_gpu_render`，保持 `-ffp-contract=off`。移植文件头和 `check_photocraft_license.cmake` 与前几批相同。
- 共用工作区：只暂存自己的文件。`apps/shell`、`apps/generated`、`kernel/*` 是其他会话在改的。

## Review Focus

1. **超过一个分块的帧**（6000×4000 导出、32 px 测试分块）带整帧参数：整幅走 reference，绝不按分块计算。由 Task 6 `test_gpu_backend_native` 的分块用例钉住：期望回退，差 0。
2. **超出 0..1 的中间值**（曝光 +2 后再去雾、高光/阴影；阶段之间不裁剪）：GPU 与 reference 一致。由 Task 3–5 的 `scaled` 帧（×2.5）和 Task 6 的 `exposure 2 + spatial` 端到端用例钉住。
3. **极小帧**（1×1、1×5、7×1、2×2、2×7）：GPU 按 reference 的早退返回，金字塔层数为 1 时不产生零尺寸纹理或 GL 错误。由 Task 4 `test_local_tone_gl_tiny` 和 Task 6 `tiny` 端到端用例钉住。
4. **带蒙版的 `color.local` 整帧操作在 GPU 上**：整帧阶段看整幅图，后段按蒙版回混，与 reference 差 ≤1。由 Task 6 用例 `local spatial, 2 masks` 钉住。
5. **拖动尺寸的耗时**：GPU 路径在 600×450 上不得慢于 reference（88 ms）。由 Task 6 bench 钉住，判定规则：
   - 若慢于 reference，executor 对像素数 < `kSpatialGpuMinPixels` 的帧选 reference。
   - 常量取 bench 交叉点，记入证据文档。

---

### Task 1: 浮点纹理与浮点读回

**Files:**
- Modify: `gpu/facade/include/pixcake/gl/gl.hpp`、`gpu/facade/src/gl.cpp`（`Texture2D`/`Context` 的转发）、`gpu/backend/native/src/native_backend.cpp`、`gpu/backend/qt/src/qt_backend.cpp`
- Create: `tests/gpu/test_gl_float_textures.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Produces:
  - `enum class PixelFormat { Rgba8, R32F, Rgba32F };`
  - `std::size_t bytesPerPixel(PixelFormat)`，返回 4 / 4 / 16。
  - `ImageView::row_stride` 以字节计。`upload` 按 `format` 选 `GL_UNSIGNED_BYTE`/`GL_FLOAT` 与 `GL_RGBA`/`GL_RED`。
  - `allocate(w, h, PixelFormat)` 用 `GL_RGBA8`/`GL_R32F`/`GL_RGBA32F`；浮点格式用 NEAREST 过滤。
  - `bool Context::readPixels(std::uint32_t w, std::uint32_t h, PixelFormat format, std::vector<std::byte>& data, std::string& error)`：读当前绑定的 framebuffer。原 4 参数版本等价于 `Rgba8`。

- [ ] **Step 1: 写失败测试 `test_gl_float_textures`**（native backend；无 context 时打印 skip 并返回 0）：
  - `test_rgba32f_roundtrip`：上传 3×2 的 RGBA32F，值含 −0.5、1.75、1e-6、65504.0、3.0e-39（次正规数）。一个 `texelFetch` 拷贝着色器把它画进 RGBA32F framebuffer，再按 `Rgba32F` 读回，逐位相等（次正规数允许被冲成 ±0）。
  - `test_r32f_roundtrip`：R32F 同上，5×3。
  - `test_rgba8_unchanged`：原 RGBA8 上传、读回不变。
- [ ] **Step 2: 运行**
  - 命令：`cmake --build build-camera-raw --target test_gl_float_textures && ./build-camera-raw/tests/test_gl_float_textures`
  - Expected：编译失败，没有 `PixelFormat::R32F`。
- [ ] **Step 3: 实现**两个 backend 与 facade；Qt backend 用同样的 GL 调用。
- [ ] **Step 4: 运行**
  - 命令：上面的命令，以及 `ctest --test-dir build-camera-raw -R "test_gpu_backend|test_gpu_shader_path"`
  - Expected：全部 PASS，原 RGBA8 路径不变。
- [ ] **Step 5: Commit** `feat(gl): R32F and RGBA32F textures and float readback`

### Task 2: GPU 滤波基础

**Files:**
- Create: `gpu/render/include/pixcake/gpu/camera_raw_gl.hpp`、`gpu/render/src/camera_raw_gl.cpp`、`gpu/render/src/camera_raw_gl_shaders.hpp`、`tests/gpu/test_camera_raw_gl.cpp`
- Modify: `gpu/render/CMakeLists.txt`、`tests/CMakeLists.txt`、`tests/gpu/check_photocraft_license.cmake`

**Interfaces:**
- Consumes：Task 1 的 `PixelFormat::R32F/Rgba32F`、`readPixels(..., PixelFormat, ...)`。
- Produces（namespace `pixcake::gpu::camera_raw_gl`）：
  - `class Gl { public: explicit Gl(gl::Context&); bool ok() const; const std::string& error() const; ... };`
    - 持有 context、按尺寸和格式复用的纹理/framebuffer 池，以及已编译的程序。
    - 以下函数遇到任何 GL 错误都返回 false 并写入 `error()`。
  - `struct Plane { gl::Texture2D tex; std::uint32_t w, h; };`
  - `bool upload(Gl&, const std::vector<float>&, std::uint32_t w, std::uint32_t h, Plane& out);`
  - `bool download(Gl&, const Plane&, std::vector<float>& out);`
  - 以下每个函数与 CPU 同名函数一一对应：
    - `bool boxMean(Gl&, const Plane& in, std::size_t r, Plane& out);`（edge clamp，窗口直接求和；不用 running sum，因为 GPU 每个像素独立）
    - `bool gauss(Gl&, const Plane&, float sigma, Plane& out);`（σ < 0.3 时直接拷贝；半径用 CPU `gaussBoxRadii`）
    - `bool guided(Gl&, const Plane& i, const Plane& p, std::size_t r, float eps, Plane& out);`
    - `bool guidedFast(Gl&, const Plane& i, const Plane& p, std::size_t r, float eps, Plane& out);`（`s = clamp(r/8,1,8)`，块平均缩小，双线性放大公式同 `bilinear`）
    - `bool rankFilter(Gl&, const Plane&, std::size_t r, bool take_max, Plane& out);`（窗口裁到图内，同 reference）

- [ ] **Step 1: 写失败测试**（无 context 时 skip）。每个用例对比 CPU 同名函数，最大绝对差 ≤ 1/4096：
  - `test_box_mean_gl`：r = 1, 3, 12；平面为 37×23 的 xorshift 噪声（−1..3），以及 1×9、9×1。
  - `test_gauss_gl`：σ = 0.2, 1.5, 4.8。
  - `test_guided_gl`、`test_guided_fast_gl`：r = 3, 24, 81，eps = 0.005, 1e-3；guide 和 input 用两张不同的噪声；含一张纯灰平面（方差 0，只靠 eps）。
  - `test_rank_filter_gl`：min/max，r = 2, 12。
- [ ] **Step 2: 运行** `cmake --build build-camera-raw --target test_camera_raw_gl && ./build-camera-raw/tests/test_camera_raw_gl`。Expected：编译失败，没有 `camera_raw_gl.hpp`。
- [ ] **Step 3: 实现。** 可分离滤波每个方向一遍。guided 的逐元素运算（a、b、ma·I + mb）各一遍，写 R32F。着色器源码放在 `camera_raw_gl_shaders.hpp`。
- [ ] **Step 4: 运行**，Expected：PASS，打印每个用例的最大差。做变异检查（eps 写成 2·eps），应 FAIL。
- [ ] **Step 5: Commit** `feat(render): GPU box, gauss, guided and rank filters for Camera Raw stages`

### Task 3: GPU 去雾与对比度+纹理+清晰度

**Files:** Modify `camera_raw_gl.hpp/.cpp`、`camera_raw_gl_shaders.hpp`、`tests/gpu/test_camera_raw_gl.cpp`；Create `tests/gpu/spatial_frames.hpp`（从 `tests/gpu/test_camera_raw_spatial.cpp` 和 `tests/gpu/test_reference_spatial.cpp` 抽出帧函数，两个文件改为 include 它）。

**Interfaces:**
- Produces：
  - `bool uploadFrame(Gl&, const camera_raw::Frame&, Plane& rgba);`（Rgba32F）
  - `bool downloadFrame(Gl&, const Plane& rgba, camera_raw::Frame& out);`
  - `bool dehaze(Gl&, Plane& rgba, float amount);`，原地更新。代理缩小、暗通道、rank filter、透射率、guided、放大与合成都在 GPU 上。暗通道和 `opened`/`norm_dark` 读回 CPU，用于 0.1 % 大气光（`stable_sort`）和两个均值。
  - `bool toneDetail(Gl&, Plane& rgba, float contrast, float texture, float clarity, float pixel_scale);`

- [ ] **Step 1: 写失败测试** `test_dehaze_gl`、`test_tone_detail_gl`：GPU 对 CPU，`downloadFrame` 后每通道最大绝对差 ≤ 1/4096。帧（复用 `test_camera_raw_spatial.cpp` 的帧函数，抽到 `tests/gpu/spatial_frames.hpp`）：
  - `detail`、`grey`、`block`（24×16）
  - `strip`（840×6）
  - `scene`（400×300，与 `test_reference_spatial` 的合成图同一函数）
  - `scaled`：detail ×2.5，超出 1

  参数：
  - 去雾：±30、±100
  - 纹理/清晰度：100、−50，pixel_scale 0.5，以及 −50/40/−30 的组合
- [ ] **Step 2: 运行**，Expected：编译失败，没有 `camera_raw_gl::dehaze`。
- [ ] **Step 3: 实现。**
- [ ] **Step 4: 运行**，Expected：PASS。变异检查：去雾权重 0.95 → 0.96，应 FAIL。
- [ ] **Step 5: Commit** `feat(render): GPU dehaze, texture and clarity`

### Task 4: GPU 高光/阴影（局部拉普拉斯）

**Files:** 同 Task 3。

**Interfaces:**
- Produces：`bool localTone(Gl&, Plane& rgba, float shadows, float highlights);`，结构与 `camera_raw::localTone` 一一对应：
  - log2 亮度平面
  - 宽或高 >1600 时 `shrink`
  - `blur_step` / `down` / `up`：mirror 规则、零填充上采样，权重 1,4,6,4,1 /16
  - `llf`：每个参考层一遍 remap，再建一套高斯金字塔；每层的 Laplacian 加权累加（ping-pong）
  - `range`：≤256 副本读回，在 CPU 上算分位数
  - 两个 shift 分位数：读回工作平面，在 CPU 上算
  - `guided_upsample`
  - `exp2(change)` 增益合成
- 早退与 reference 相同：w<2 或 h<2；`hi − lo ≤ 1e-6` 或 NaN。

- [ ] **Step 1: 写失败测试**：
  - `test_local_tone_gl`：上面那组帧，再加 `long`（1700×12，触发 shrink）。阴影 100、高光 −100、阴影 −50/高光 50，最大差 ≤ 1/4096。
  - `test_local_tone_gl_tiny`：1×1、1×5、7×1、2×2、2×7，结果与 CPU 逐位相等（这些帧走早退或深度 1），且 `Gl::ok()` 为 true。
- [ ] **Step 2: 运行**，Expected：编译失败，没有 `camera_raw_gl::localTone`。
- [ ] **Step 3: 实现。** 一层金字塔的平面尺寸为 `div_ceil`。最细一层 1×N 或 N×1 时 mirror 的 `n == 1` 分支要照抄。
- [ ] **Step 4: 运行**，Expected：PASS。变异检查：阴影 sigma 0.571 → 0.6，应 FAIL。
- [ ] **Step 5: Commit** `feat(render): GPU highlights and shadows (local Laplacian)`

### Task 5: GPU 白色/黑色

**Files:** 同 Task 3。

**Interfaces:**
- Produces：`bool whitesBlacks(Gl&, Plane& rgba, float whites, float blacks);`
  - ≤256 副本的块平均在 GPU 上做，读回后用 CPU 的 `luminancePoints` 同一逻辑求分位数和 `amounts`。
  - 把 `row_at(whites)`、`row_at(blacks)` 两行（各 33 个值）上传为 33×2 R32F 纹理。着色器按 `curve` 的灰阶插值和逐通道/按亮度两种分支计算。
  - 为此 CPU 端在 `camera_raw_spatial.hpp` 增加 `std::array<float, 33> whitesRow(float amount)`、`std::array<float, 33> blacksRow(float amount)` 和 `std::array<float, 2> whitesBlacksAmounts(float whites, float blacks, float bp, float wp)`。
    - 前两个是新移植的 PhotoCraft `row_at` / `whites_row` / `blacks_row`（`whites_blacks.rs:320-336`），C++ 版还没有。
    - 第三个公开现有的内部函数 `amounts`。
    - `whitesBlacks` 本身仍用 `curve`，行为不变。

- [ ] **Step 1: 写失败测试** `test_whites_blacks_gl`：上面那组帧，白色/黑色 ±100、±50 及组合，最大差 ≤ 1/4096。另写 `test_whites_blacks_rows`：`curve(v)` 与“把行插值到 v”在 33 级灰阶和 1000 个随机 v 上差 ≤ 1e-6（证明换成行插值是安全的）。
- [ ] **Step 2: 运行**，Expected：编译失败。
- [ ] **Step 3: 实现。**
- [ ] **Step 4: 运行**，Expected：PASS。变异检查：照片黑色系数 1.2 → 1.3，应 FAIL。
- [ ] **Step 5: Commit** `feat(render): GPU whites and blacks`

### Task 6: executor 接入

**Files:** Modify `gpu/render/src/render_executor.cpp`、`gpu/render/src/shader_sources.hpp`、`tests/gpu/test_gpu_backend_native.cpp`、`tests/gpu/test_reference_spatial.cpp`、`tests/gpu/bench_reference_render.cpp`。

**Interfaces:**
- Consumes：Task 2–5 的 `camera_raw_gl::*`。
- `kColor` 的 `stage0` 改为（colour step, tone step, spatial input, raw output）：
  - spatial input = 1：`p` 从 `spatial`（unit 3，Rgba32F）`texelFetch`，`before` 仍取 `source`。
  - raw output = 1：跳过分组，写出不裁剪的 `vec4(p, before.a)`，目标是 Rgba32F。
- 前段：colour step、只开亮度的 tone step（`cr1.x = 0`、`cr1.w = 0`）、raw output。
- 后段：只开 Presence 的 tone step（`bright1.x = 0`、`cr1.x = 0`）、spatial input、分组、`finish` 回混。Contrast 由 `toneDetail` 处理；纹理和清晰度都为 0 时，`toneDetail` 也能处理纯 Contrast，与 reference 的 `draw_spatial` 一致。
- `tone_channels()` 重新包含七个整帧通道。`gpu_formula_supported` 不变。`execute()` 在任一 draw 带整帧参数且帧大于 `gpu_tile_` 时整幅回退 reference，诊断为“含 Camera Raw 整帧参数且帧大于 GPU 分块，已回退 reference”。
- 整帧 draw 在单分块循环里执行：前段写入 Rgba32F 帧，运行 `dehaze` → `localTone` → `toneDetail` → `whitesBlacks`，后段写回 RGBA8 的 `next`。
- `constexpr std::uint64_t kSpatialGpuMinPixels`：由 bench 决定，见 Review Focus 5；未达到时该操作走 reference。

- [ ] **Step 1: 改测试**（先失败）：
  - `test_gpu_backend_native`：
    - “tone, whole-frame (reference)” 改名为 “tone, whole-frame”，`on_gpu = true`。
    - 新增：`exposure 2 + spatial`；`local spatial, 2 masks`（`color.local`，shadows 60、whites 30，蒙版 m1、m2，multiply）；`tiny`（帧 7×1，全部整帧参数）；`spatial then grain`（全局整帧操作后接颗粒操作）。
    - 以上用例 readback 与 reference 差 ≤1。
    - 分块（32 px）运行期望回退，与 reference 差 0。`Case` 增加 `bool tiles_on_gpu`，整帧用例设为 false。
  - `test_reference_spatial`：`test_spatial_falls_back_to_reference` 改为 `test_spatial_classified_for_gpu`：七个通道非零时 `classify` 为 `Implemented`。
- [ ] **Step 2: 运行** `cmake --build build-camera-raw -j8 && ./build-camera-raw/tests/test_gpu_backend_native`。Expected：FAIL “expected the GPU path”。
- [ ] **Step 3: 实现。**
- [ ] **Step 4: 运行**
  - 命令：`ctest --test-dir build-camera-raw -j8`
  - Expected：除其他会话的已知失败外全部 PASS；整帧用例 readback 差 ≤1。
- [ ] **Step 5: bench。** `bench_reference_render` 增加 GPU 列的“interactive 600px, spatial”和“4000px, spatial”，在 Release 构建下运行，按 Review Focus 5 定 `kSpatialGpuMinPixels`（GPU 不慢时设为 0）。
- [ ] **Step 6: Commit** `feat(render): GPU path runs PhotoCraft spatial Camera Raw stages`；提交说明逐条写“旧 → 新”（回退 → GPU）。

### Task 7: 文档

- [ ] 状态矩阵：高光/阴影、白色/黑色、纹理/清晰度、去雾改为 `photocraft-ported`（范围：8 位 sRGB 输入；帧大于一个 GPU 分块时整幅回退 reference）。GPU 列写 conditional 和实测差。验收结论同步更新。
- [ ] 公式登记第 1、8、10 节，来源登记，以及[整帧阶段移植证据](../evidence/2026-11-09-photocraft-spatial-stages-port.md)：补 GPU 一节，含阶段级最大差、端到端差、bench、`kSpatialGpuMinPixels`，门禁 3/4 改为满足。
- [ ] 链接检查与 `git diff --check`；矩阵只暂存自己的行。Commit `docs: PhotoCraft spatial Camera Raw stages are photocraft-ported`。

## 不在本计划内

- 超过一个 GPU 分块的整帧 GPU 计算（需要跨分块的金字塔和统计）。这些帧继续走 CPU reference（24 MP 约 1.2 s）。
- 16 位/浮点输入、ICC/RAW；曲线（参数未注册）。
