# PhotoCraft Camera Raw 空间与整图统计阶段移植 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 去雾、高光/阴影、纹理/清晰度、白色/黑色改为执行 PhotoCraft Camera Raw 的公式（C++ reference），在预览和导出中按 PhotoCraft 的阶段顺序运行；GPU 遇到这些参数明确回退 reference。状态到 `photocraft-reference-only`（executor 已接入），GLSL 留给下一份计划。

**Architecture:** 新文件 `gpu/render/src/camera_raw_spatial.cpp` 移植整帧阶段（作用于 `std::vector<std::array<float,4>>` 浮点帧）。`draw_reference` 对含空间参数的操作改为“分段”执行：逐像素前段 → 整帧阶段 → 逐像素后段 → 按蒙版回混；不含空间参数的操作仍走现有逐像素循环和 GPU。`PassRegistry::gpuSupported` 对非零空间参数返回 false（状态 `ReferenceOnly`，诊断写明原因）。

**Tech Stack:** C++17、CMake/CTest、Rust 1.99（只用于向量）。

**Spec:** [PhotoCraft 能力来源与 PixCake 移植规范](../specs/2026-11-09-photocraft-reference-and-porting-policy.md)；前两批：[Camera Raw 接入](2026-11-09-camera-raw-executor-wiring.md)、[调整图层与颗粒](2026-11-09-photocraft-adjustments-and-grain.md)。

## Global Constraints

- PhotoCraft `885a6ddf27cc58c07faf1722e5c327cd38953657`，MIT；移植文件头与 `check_photocraft_license.cmake` 同前两批；`pixcake_gpu_render` 保持 `-ffp-contract=off`。
- 来源：`crates/algo/src/camera_raw.rs` `dehaze`、`box_mean`、`gauss`、`guided_fast`、`guided`、阶段 3 的 contrast/texture/clarity 与 `tone()`、`luminance_points`；`camera_raw/local_tone.rs`（高光/阴影，局部拉普拉斯，`WORK_SIDE` 1600 + 引导上采样）；`camera_raw/whites_blacks.rs`（`amounts`、`apply`）。编码固定 sRGB（`Encoding::of(0)`）。
- 单位：Camera Raw 滑杆 −100..100（去雾、纹理、清晰度、高光、阴影、白色、黑色）直接传值；`pixel_scale` 用上一批的 `RenderInputs::pixel_scale`（纹理 sigma 用到）。
- 新的逐像素 + 整帧顺序（替换 PixCake 旧的 lift/局部对比度占位，与 PhotoCraft `develop` 一致，亮度位置沿用用户裁决）：
  1. Camera Raw 阶段 1 白平衡/曝光 → AI 风格偏移 → 现代亮度（逐像素）
  2. 去雾（整帧）
  3. 高光/阴影（整帧，local_tone）
  4. 对比度 + 纹理 + 清晰度（纹理或清晰度非零时整帧，用 PhotoCraft `tone()`；否则逐像素 Contrast，同现状）
  5. 白色/黑色（整帧统计 `luminance_points` + 曲线）
  6. Presence → 全局工具分组 → 颗粒（逐像素，同现状），最后按覆盖率回混
- 整图统计和滤波都在当前帧上计算（PhotoCraft 的代理尺寸 256/768/1600 已让预览与导出接近），不另传全分辨率统计。
- 局部/后期调色带蒙版时：整帧阶段在整帧上算，结果按蒙版回混（与公式登记第 8 节一致）。
- 帧之间的中间值保持浮点、不量化；只在操作结束写回 8 位。

## Review Focus

1. 预览（600/2000/4000 px）与全分辨率导出的高光/阴影、白色/黑色结果接近——Task 4 `test_spatial_preview_matches_export`：同一合成图 4× 下采样后渲染，与全尺寸渲染再下采样比较，平均差 ≤ 2 级、最大差 ≤ 8 级。
2. 均匀图像经高光/阴影、去雾、清晰度不变或只整体变化（无光晕）——Task 2/3 向量含纯灰图。
3. 极小帧（宽或高 < 2、1×1 缩略图）不崩溃、原样或按 PhotoCraft 早退——Task 4 `test_spatial_tiny_frames`。
4. 含空间参数的操作在 GPU 可用时回退 reference 并给出诊断，结果与纯 reference 逐字节相同——Task 4 `test_spatial_falls_back_to_reference`。
5. 带蒙版的局部调色使用整帧阶段后按蒙版回混——Task 4 `test_spatial_local_masked`。

---

### Task 1: 整帧基础与去雾、纹理/清晰度

**Files:** Create `gpu/render/include/pixcake/gpu/camera_raw_spatial.hpp`、`gpu/render/src/camera_raw_spatial.cpp`、`tests/gpu/test_camera_raw_spatial.cpp`、`tests/gpu/data/spatial_vectors.inc`；Modify `tools/photocraft_vectors`（`spatial` 模式）、CMake、license 检查。

**Interfaces（namespace `pixcake::gpu::camera_raw`）：**
- `struct Frame { std::uint32_t width = 0, height = 0; std::vector<std::array<float, 4>> px; };`
- `void dehaze(Frame&, float amount /* slider/100 */);`
- `void toneDetail(Frame&, float contrast, float texture, float clarity, float pixel_scale);`（阶段 3 的整帧分支）
- 内部：`boxMean`、`gauss`、`guidedFast`、`guided`（照 `camera_raw.rs:340-461`）。

- [ ] 向量：`develop` 只开 dehaze（±30、±100）或 texture/clarity（±50、±100，含 contrast 40 组合），输入 3 张 48×32 合成图（渐变 + 硬边 + 小块细节、纯灰 0.5、高光大块），pixel_scale 1 与 0.5；逐像素输出。
- [ ] 失败测试 `test_dehaze_matches_photocraft`、`test_tone_detail_matches_photocraft`，容差 1e-5（整帧求和顺序与 PhotoCraft 一致时应为 0）→ 实现 → 通过 → 变异检查一处常数 → Commit `feat(render): port PhotoCraft dehaze, texture and clarity`。

### Task 2: 高光/阴影（local_tone）

**Interfaces：** `void localTone(Frame&, float shadows, float highlights);`（`local_tone.rs:344` `apply`，含 `shrink`、`guided_upsample`、`gaussian`/`llf` 金字塔、`quantile`/`range`）。

- [ ] 向量：同上三张图 + 一张 2000×40 长条（触发 `WORK_SIDE` 缩小与引导上采样），shadows/highlights 各 ±50、±100 与组合。
- [ ] 失败测试 `test_local_tone_matches_photocraft`（容差 1e-5）→ 实现 → 通过 → 变异 → Commit `feat(render): port PhotoCraft highlights and shadows (local Laplacian)`。

### Task 3: 白色/黑色

**Interfaces：** `std::pair<float, float> luminancePoints(const Frame&);` `void whitesBlacks(Frame&, float whites, float blacks);`（`camera_raw.rs:891` 与 `whites_blacks.rs` 全部表与 `amounts`/`apply`）。

- [ ] 向量：三张图 × whites/blacks 各 ±50、±100 与组合；另输出每张图的 `(bp, wp)`。
- [ ] 失败测试 `test_luminance_points_match`、`test_whites_blacks_matches_photocraft` → 实现 → 通过 → 变异 → Commit `feat(render): port PhotoCraft whites and blacks`。

### Task 4: executor 接入与 GPU 回退

**Files:** Modify `gpu/render/src/render_executor.cpp`；Test `tests/gpu/test_reference_spatial.cpp`（新）及钉住旧 lift/局部对比度的测试。

- [ ] 失败测试：
  - `test_spatial_order_matches_develop`：全局操作同时含 exposure、brightness 0、dehaze、shadows、highlights、clarity、texture、contrast、whites、blacks、vibrance，结果逐像素等于把 Task 1–3 函数与上一批逐像素函数按 Global Constraints 顺序串起来（容差 0）。
  - `test_spatial_preview_matches_export`、`test_spatial_tiny_frames`、`test_spatial_falls_back_to_reference`、`test_spatial_local_masked`（见 Review Focus）。
- [ ] 实现：`ToneColor` 去掉 `highlights/shadows/whites/blacks/local`，改存 Camera Raw 滑杆；`draw_reference` 对 `spatial_active` 的操作走分段路径；`scalar_tone_operation` 与 `gpu_formula_supported` 在任一空间参数非零时返回 false；GPU 的 `tone1/tone2` uniform 与 `kColor` 里的 legacy lift/局部对比度删除（空间参数为 0 时它们本来就不执行）。
- [ ] 更新钉住旧公式的测试（提交说明里逐条“旧 → 新”）；`ctest --test-dir build-camera-raw` 全部通过（另一会话的 shell 编译失败单独说明）。
- [ ] Commit `feat(render): reference path runs PhotoCraft spatial Camera Raw stages; GPU falls back`。

### Task 5: 文档

- [ ] 矩阵：高光/阴影、白色/黑色、纹理/清晰度、去雾改 `photocraft-reference-only`，注明“executor 已接入预览与导出；GPU 回退 reference”；验收结论更新。
- [ ] 公式登记第 1 节换成移植摘要与新顺序，旧 lift/局部对比度公式移到第 11 节；来源登记；新证据文档（向量规模、最大差、门禁逐条，门禁 3/4 写明未满足）。
- [ ] `bench_reference_render` 增加“全部空间阶段”一行（600 px 与 4000 px），记录 reference 耗时。
- [ ] 链接检查与 `git diff --check`；Commit `docs: PhotoCraft spatial Camera Raw stages are photocraft-reference-only`。

## 不在本计划内

- 这些阶段的 GLSL（多遍滤波、金字塔）——下一份计划，以本计划的 C++ reference 为 oracle。
- 曲线、色阶独立通道（未注册参数）；暗角、锐化/降噪（PixCake 无对应参数时不做）。
- 16 位/浮点、ICC/RAW。
