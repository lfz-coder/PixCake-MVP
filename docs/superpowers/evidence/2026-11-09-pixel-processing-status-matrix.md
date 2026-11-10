# 像素处理来源与执行现状矩阵

日期：2026-11-09
状态：当前实现与 PhotoCraft 移植的验收基线
总裁决：[PhotoCraft 能力来源与 PixCake 移植规范](../specs/2026-11-09-photocraft-reference-and-porting-policy.md)

这份矩阵把这些事分开记录：**行为来源、来源族、PixCake 移植状态、CPU reference、OpenGL 执行、预览/导出接入**。逐参数的当前公式、数值映射和来源路径见[像素公式与来源逐参数登记](2026-11-09-pixel-formula-registry.md)。

PhotoCraft 已有对应能力时，`photocraft-target` 是 PixCake 的目标行为；它不表示 PixCake 已经完成移植。`GPU=conditional` 只表示 GPU 在满足门槛时运行当前 PixCake pass，并不表示与 PhotoCraft 或像素蛋糕官方逐像素一致。

## 状态含义

行为来源和移植状态是两条独立的轴（移植规范第 3 节）：

- **行为来源**：`photocraft-target` 表示 PhotoCraft 有语义等价实现，PixCake 应移植；`PixCake-specific` 表示 PhotoCraft 没有等价能力或产品语义必须不同；`no-equivalent` 表示暂时没有可采用的实现；“待裁决”表示需要先定产品语义。
- **来源族**：A = Camera Raw 滤镜管线（`crates/algo/src/camera_raw*`、`crates/gpu/src/camera_raw.wgsl`）；B = Photoshop 调整图层与合成（`crates/compose/src/*`、`crates/gpu/src/compose.wgsl`）。按语义选，不按名字选。
- **移植状态**：`legacy-provisional` 表示 PixCake 跑的仍是旧公式；`photocraft-reference-only` 表示 PhotoCraft 语义已有 C++ reference，OpenGL/端到端未完成；`photocraft-ported` 表示满足全部门禁；`not-implemented` 表示没有可执行实现。
- **CPU reference**：PixCake 是否有可执行公式和像素测试。它存在不代表来源正确。
- **GPU**：`conditional` 表示能力、通道、混合模式、蒙版数量和 GPU context 均通过门槛；失败时回退 reference 或给出诊断。“PhotoCraft GPU”列说明 PhotoCraft 自己有没有 GPU 实现可对照：空间阶段只有 CPU。
- **预览/导出**：两者是否已调用同一 PixCake executor。这个状态不等于输入色彩管理或真实文件 I/O 已闭环。
- **PixCake 官方一致性**：`unverified` 表示没有官方逐像素证明；`partial` 只表示固定 9.3.0 的局部黑盒测量，不能推出内部公式。

## 当前矩阵

| 能力/分组 | 行为来源 | 来源族与 PhotoCraft 目标 | PhotoCraft GPU | 移植状态 | CPU reference | GPU | 预览 | 导出 | 官方一致性 | 主要证据/限制 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 白平衡 `temperature` / `tint` | `photocraft-target` | A：`algo/src/camera_raw.rs:670` `white_balance_gains`、`camera_raw/per_pixel.rs:13-20` | 有（逐像素） | photocraft-ported（范围：8 位 sRGB 输入，executor 输入未线性化（解码器 sRGB 字节直接作为 Camera Raw 显示编码输入）） | 有：`camera_raw::balanceGains/applyBalance`，executor 阶段 1 | conditional：`kColor` `cr_balance`，readback 与 reference 差 0 | 已接入 | 已接入 | partial | 9.3.0 的 +100、-50、-100 测量保留为官方一致性证据；单位相同（-100..100）直接传值。**tint 符号**：Camera Raw 正值偏品红，旧 Bradford 实现正值偏绿，按 PhotoCraft 语义执行，像素蛋糕方向待黑盒确认；[逐像素移植证据](2026-11-09-camera-raw-per-pixel-port.md) |
| `exposure` | `photocraft-target` | A：`per_pixel::gains` 与 `camera_raw.rs` `balance()`（sRGB 解码 → ×2^EV → 编码） | 有（逐像素） | photocraft-ported（范围：8 位 sRGB 输入，executor 输入未线性化（解码器 sRGB 字节直接作为 Camera Raw 显示编码输入）） | 有：与白平衡合并为一次线性光增益 | conditional：差 0 | 已接入 | 已接入 | unverified | 不是 Photoshop“曝光度”调整图层；+1 EV 在 sRGB 中段约 ×1.4（旧字节翻倍为 ×2）；标量快速路径保留 |
| `contrast` | `photocraft-target` | A：`camera_raw/per_pixel.rs:23-25`、`camera_raw.rs` 阶段 3 S 曲线 | 有（无纹理/清晰度时） | photocraft-ported（范围：8 位 sRGB 输入，executor 输入未线性化（解码器 sRGB 字节直接作为 Camera Raw 显示编码输入）；逐像素 Contrast；与纹理/清晰度同开时由整帧阶段 3 执行，见 clarity/texture 行） | 有：`camera_raw::applyContrast`，在现代亮度之后 | conditional：`cr_contrast`，差 0 | 已接入 | 已接入 | unverified | 作用于亮度、保持颜色比例，因此退出逐通道标量快速路径 |
| `brightness` | `photocraft-target`（用户裁决：按 Photoshop） | B：`compose/src/adjust/tone.rs:155` `modern_brightness`，`modern_lut(brightness, 0)` 逐通道作用于编码值 | 有（compose 路径） | photocraft-ported（范围：8 位 sRGB 输入，executor 输入未线性化（解码器 sRGB 字节直接作为 Camera Raw 显示编码输入）） | 有：`brightness::modernLut` 4096 项表，与 PhotoCraft 650 个采样最大差 1.19e-7 | conditional：GPU 直接求曲线，降低分支用 32 步二分求逆；差 0 | 已接入 | 已接入 | unverified | 在 A 管线中的位置是 PixCake-specific 顺序：阶段 1 之后、旧 lift 之前（2026-11-09 用户裁决），待黑盒确认 |
| `highlights` / `shadows` | `photocraft-target` | A：`camera_raw/local_tone.rs` `apply`（log2 亮度上的快速局部拉普拉斯，>1600 px 缩小后引导上采样） | 无（空间阶段，仅 CPU） | photocraft-reference-only（executor 已接入预览与导出；GPU 回退 reference） | 有：`camera_raw::localTone`，与 PhotoCraft 向量差 0 | 回退 reference | 已接入 | 已接入 | unverified | 预览与导出接近但不完全相同（600 px 预览对 2400 px 导出平均 ≤0.33 级）；reference 与 PhotoCraft 同速，600 px 高光+阴影约 0.13 s；[整帧阶段移植证据](2026-11-09-photocraft-spatial-stages-port.md) |
| `whites` / `blacks` | `photocraft-target` | A：`camera_raw/whites_blacks.rs`（实测曲线与 `AW`/`AB` 自适应表）、`camera_raw.rs` `luminance_points` | 无（统计在 CPU） | photocraft-reference-only（executor 已接入预览与导出；GPU 回退 reference） | 有：`camera_raw::whitesBlacks`/`luminancePoints`，差 0 | 回退 reference | 已接入 | 已接入 | unverified | 黑白点在当前帧上统计（≤256 px 副本）；[整帧阶段移植证据](2026-11-09-photocraft-spatial-stages-port.md) |
| `clarity` / `texture` | `photocraft-target` | A：`camera_raw.rs` 阶段 3（清晰度 `guided_fast`、纹理 `gauss`）与 `tone()` | 无（空间阶段，仅 CPU） | photocraft-reference-only（executor 已接入预览与导出；GPU 回退 reference） | 有：`camera_raw::toneDetail`（与 Contrast 同一阶段），差 0 | 回退 reference | 已接入 | 已接入 | unverified | 纹理 σ 用 `pixel_scale`；[整帧阶段移植证据](2026-11-09-photocraft-spatial-stages-port.md) |
| `dehaze` | `photocraft-target` | A：`camera_raw.rs` 阶段 2 `dehaze`（≤768 px 代理上的暗通道先验 + 引导滤波） | 无（空间阶段，仅 CPU） | photocraft-reference-only（executor 已接入预览与导出；GPU 回退 reference） | 有：`camera_raw::dehaze`，差 0 | 回退 reference | 已接入 | 已接入 | unverified | [整帧阶段移植证据](2026-11-09-photocraft-spatial-stages-port.md) |
| `vibrance` / `saturation` | `photocraft-target` | A：`camera_raw.rs` 阶段 4 Presence | 有（逐像素） | photocraft-ported（范围：8 位 sRGB 输入，executor 输入未线性化（解码器 sRGB 字节直接作为 Camera Raw 显示编码输入）） | 有：`camera_raw::applyPresence` | conditional：`cr_presence`，差 0 | 已接入 | 已接入 | unverified | 不是 Photoshop“自然饱和度”调整图层 |
| 曲线 | `photocraft-target` | A：`camera_raw.rs` 阶段 5 参数曲线与 master/R/G/B 点曲线、`validate_curve` | 有（逐像素） | not-implemented | 无 | 无 | 不绘制 | 不绘制 | unverified | PixCake 曲线参数尚未注册；不能用旧色阶或占位曲线冒充 |
| HSL 八色带 | `photocraft-target` | A：`camera_raw.rs` 阶段 6 Color Mixer（色带名与 PixCake 一致） | 有（逐像素） | photocraft-ported（范围：8 位 sRGB 输入，executor 输入未线性化（解码器 sRGB 字节直接作为 Camera Raw 显示编码输入）） | 有：`camera_raw::applyColorMixer`；黑白配置文件下不运行 | conditional：差 0 | 已接入 | 已接入 | unverified | 不是 Photoshop“色相/饱和度”六范围；CPU reference 比旧公式慢约一倍（见证据） |
| 色彩分级 | `photocraft-target` | A：`camera_raw.rs` 阶段 7 四色轮 + Blending/Balance | 有（逐像素） | photocraft-ported（范围：8 位 sRGB 输入，executor 输入未线性化（解码器 sRGB 字节直接作为 Camera Raw 显示编码输入）） | 有：`camera_raw::resolveGrade/applyColorGrading`，Balance 固定 0 | conditional：差 ≤1（一个通道） | 已接入 | 已接入 | unverified | PixCake 未注册 Balance；不是 Photoshop ColorBalance |
| 颗粒 `amount/size/roughness` | `photocraft-target` | A：`camera_raw.rs` 阶段 9 grain | 有（逐像素，全分辨率坐标） | photocraft-ported（范围：8 位 sRGB 输入） | 有：`camera_raw::grainOf/grainNoise/applyGrain`，与 PhotoCraft 54×144 像素最大差 5.96e-8 | conditional：`kGrain`（64 位哈希用 uvec2 模拟），差 0 | 已接入（renderd 按原图长边给 `pixel_scale`） | 已接入 | unverified | seed 固定 0；[调整图层与颗粒移植证据](2026-11-09-photocraft-adjustments-and-grain-port.md) |
| 颗粒 `shadows/midtones/highlights/color` | `PixCake-specific` | Camera Raw 无这些子参数 | — | legacy-provisional（叠加在 PhotoCraft 颗粒上） | 有：分区按默认 30/50/15 归一后乘强度；colour 混入逐通道噪声（种子 1..3） | conditional：差 0 | 已接入 | 已接入 | unverified | 默认分区且 colour=0 时与 PhotoCraft 完全一致；colour 默认 10，所以默认参数下并不完全一致 |
| 色阶（RGB 复合） | `photocraft-target` | B：`compose/src/adjust/tone.rs:299-353` `levels_q`、`levels_gamma`、`tone_luts_q` | 有 | photocraft-ported（范围：8 位 sRGB 输入）；仅复合 RGB = Photoshop master | 有：`adjust::levelsLut`（quantum 255，4096 项）+ `sampleLut`，与 PhotoCraft 表最大差 1.19e-7 | conditional：GPU 直接算表的相邻两项，差 0 | 已接入 | 已接入 | unverified | PixCake 没有独立 R/G/B 参数；`gamma = 10^input_gray` 为 PixCake 参数映射 |
| 可选颜色 | `photocraft-target` | B：`compose/src/adjust/color.rs:271-323` | 有 | photocraft-ported（范围：8 位 sRGB 输入） | 有：`adjust::selectiveColor`，九目标 × CMYK × 两种模式与 PhotoCraft 一致 | conditional：差 ≤1 | 已接入 | 已接入 | unverified | absolute 模式按墨量余量 `min(1, 2·room)` 缩放 |
| 黑白配置文件与八色带黑白混合 | `PixCake-specific` | A 无黑白；B `BlackWhite`（`compose/src/adjust/color.rs:98-119`）只有六权重，仅作参考 | — | legacy-provisional | 有（旧 Rec.709/色带） | conditional | 已接入 | 已接入 | unverified | 改用六范围或 HSL 权重需先改参数模式 |
| 3D LUT/配置文件资源 | `photocraft-target`（采样） | B：`compose/src/adjust/color.rs:127-185` `lut3d_sample` | 有 | 采样 photocraft-ported（三线性）；资源 legacy-provisional | 条件性：`adjust::lut3dSample` | 无 GPU 3D LUT | 条件性 | 条件性 | unverified | 资源版本、色彩空间、许可证和真实 3D texture 未闭环；`amount` 与输入混合是 PixCake-specific |
| 混合模式与图层合成 | `photocraft-target` | B：`compose/src/psblend.rs` `composite`、`composite.rs` `blend_back` | 有 | photocraft-ported（范围：8 位 sRGB 输入）（normal/multiply/screen） | 有：操作按覆盖率（不透明度 × 蒙版）回混、alpha 不变；图层用 W3C 一般公式 | conditional：操作差 0–1，图层差 1 | 已接入 | 已接入 | unverified | 像素自身 alpha 不再乘进调整强度；其他混合模式未注册 |
| 局部调色 `color.local` | 滑杆数学 `photocraft-target`（A）；蒙版/选区/顺序 `PixCake-specific` | 同全局调色各行 | 同各行 | 分项：白平衡/曝光/亮度/对比度/鲜艳度/饱和度为 photocraft-ported，去雾/高光/阴影/纹理/清晰度/白色/黑色为 photocraft-reference-only | 有 | conditional | 已接入 | 已接入 | unverified | 超过四个蒙版或含整帧参数时回退 reference；整帧阶段在整幅图像上计算后再按蒙版混合（`test_spatial_local_masked`） |
| 后期调色 `color.post` | 滑杆数学 `photocraft-target`（A）；操作阶段 `PixCake-specific` | 同全局调色各行 | 同各行 | 分项：白平衡/曝光/亮度/对比度/鲜艳度/饱和度为 photocraft-ported，去雾/高光/阴影/纹理/清晰度/白色/黑色为 photocraft-reference-only | 有 | conditional | 已接入 | 已接入 | unverified | 与全局调色同一 tone/reference；含整帧参数时回退 reference |
| AI 追色 `color.match` | `PixCake-specific` | PhotoCraft 没有 PixCake 点数/样片业务流程 | — | legacy-provisional | 有（项目简化） | conditional | 已接入 | 已接入 | unverified | 固定 RGB 偏移/写回语义不是 AI 模型 |
| AI 色彩风格 `color.style` | `PixCake-specific` | PhotoCraft 没有对应业务模型 | — | legacy-provisional | 有（项目简化） | conditional | 已接入 | 已接入 | unverified | 固定 RGB 偏移不是 AI 风格实现 |
| 色彩编辑器偏移 | `no-equivalent` | PhotoCraft 没有 Point Color 等价实现 | — | not-implemented | 无 | 无 | 不绘制 | 不绘制 | unverified | 没有吸管/目标颜色/范围参数，保留诊断 |
| 输入色彩管理：sRGB/ICC/RAW | `PixCake-specific`（I/O 适配） | PhotoCraft transfer/CMS 与 `encoding_gamma` 约定只作参考 | — | legacy-provisional | 未闭环 | 未闭环 | 条件性 | 条件性 | unverified | macOS 解码器输出 sRGB RGBA8；executor 已声明把这些字节作为 Camera Raw 的 sRGB 显示编码输入（`encoding_gamma = 0`），ICC/RAW 与非 sRGB 来源仍未闭环 |

## 统一执行证据

- PhotoCraft 来源快照：`third_party/photocraft`，当前提交 `885a6ddf27cc58c07faf1722e5c327cd38953657`；来源和许可证要求见移植规范。
- PixCake 当前参数与公式：`gpu/render/src/render_executor.cpp`（逐像素顺序见公式登记）；移植公式在 `gpu/render/src/camera_raw.cpp`、`gpu/render/src/brightness.cpp`、`gpu/render/src/adjust.cpp`、`gpu/render/src/camera_raw_spatial.cpp`（整帧阶段），GLSL 在 `gpu/render/src/shader_sources.hpp` `kColor`。仍为 `legacy-provisional` 的行跑的是旧公式。
- GPU 支持判定：`PassRegistry::gpuSupported` 只接受已登记能力、通道和最多四个蒙版，任一整帧参数非零时返回 false：`gpu/render/src/render_executor.cpp`。
- 预览：`apps/renderd/src/render_service.cpp` 先尝试 GPU，失败或不满足条件时调用 `renderReference`。
- 导出：`apps/exportd/src/export_service.cpp` 使用同一个 `RenderExecutor`，同样支持 GPU 和 reference fallback。
- PhotoCraft Camera Raw 逐像素阶段与现代亮度的 C++ 移植、对照向量、executor 接入和 GPU readback：[逐像素移植证据](2026-11-09-camera-raw-per-pixel-port.md)；`tests/gpu/test_camera_raw_port.cpp`、`tests/gpu/test_brightness_port.cpp`、`tests/gpu/test_reference_color_groups.cpp`、`tests/gpu/test_gpu_backend_native.cpp`。
- PhotoCraft Camera Raw 整帧阶段（去雾、高光/阴影、纹理/清晰度、白色/黑色）的 C++ 移植、对照向量、executor 分段接入与 GPU 回退：[整帧阶段移植证据](2026-11-09-photocraft-spatial-stages-port.md)；`tests/gpu/test_camera_raw_spatial.cpp`、`tests/gpu/test_reference_spatial.cpp`。
- 现有色彩分组逐像素和 GPU readback 证据：`docs/superpowers/evidence/2026-11-06-color-groups-reference-pixels.md`；该证据记录的是迁移前的旧 reference 与 shader 对齐，不能证明 PhotoCraft parity。

## 验收结论

当前可以说：**白平衡、曝光、对比度、亮度、鲜艳度/饱和度、HSL 八色带、色彩分级（Camera Raw 逐像素阶段与现代亮度，见[逐像素移植证据](2026-11-09-camera-raw-per-pixel-port.md)），以及色阶、可选颜色、normal/multiply/screen 混合、3D LUT 采样和颗粒基础三项（见[调整图层与颗粒移植证据](2026-11-09-photocraft-adjustments-and-grain-port.md)）已到 `photocraft-ported`，范围限于 8 位 sRGB 输入：CPU reference 与 PhotoCraft 对照向量一致，预览和导出执行这些公式，OpenGL readback 与 reference 差 ≤1（整帧与 32 px 分块）。高光/阴影、白色/黑色、纹理/清晰度、去雾（整帧阶段）为 `photocraft-reference-only`：C++ 与 PhotoCraft 对照向量逐位一致，预览和导出按 PhotoCraft 顺序执行，但没有 GLSL，GPU 回退 reference（见[整帧阶段移植证据](2026-11-09-photocraft-spatial-stages-port.md)）；曲线未实现。**

当前不能说：**PhotoCraft 已实现的能力都已移植到 PixCake；已移植的七行在 ICC/RAW、16 位或浮点输入下与 PhotoCraft 一致（executor 输入尚未线性化/色彩管理）；或任何一项已经与像素蛋糕官方逐像素一致。**

在 PhotoCraft 目标完成、输入色彩管理闭环、未实现项补齐、来源与许可证记录完整前，产品验收必须沿用上表的分项状态，不能用单一的“色彩算法已完成”代替。
