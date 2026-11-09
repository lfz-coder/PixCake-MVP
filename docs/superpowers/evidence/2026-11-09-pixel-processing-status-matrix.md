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
| 白平衡 `temperature` / `tint` | `photocraft-target` | A：`algo/src/camera_raw.rs:670` `white_balance_gains`、`camera_raw/per_pixel.rs:13-20` | 有（逐像素） | photocraft-reference-only | 有：PhotoCraft 移植（`gpu/render/src/camera_raw.cpp`，未接入 executor，executor 仍跑旧 Bradford/黑盒曲线） | conditional | 已接入 | 已接入 | partial | 9.3.0 一张棚拍的 +100、-50、-100 测量保留为官方一致性证据；PixCake 与 Camera Raw 单位相同（-100..100），直接传值 |
| `exposure` | `photocraft-target` | A：`per_pixel::gains` 与 `camera_raw.rs` `balance()`（sRGB 解码 → ×2^EV → 编码） | 有（逐像素） | photocraft-reference-only | 有：PhotoCraft 移植（未接入 executor，executor 仍跑旧 EV 路径） | conditional | 已接入 | 已接入 | unverified | 不是 Photoshop“曝光度”调整图层（`compose/src/adjust.rs:138-157`）；输入编码契约未闭环 |
| `contrast` | `photocraft-target` | A：`camera_raw/per_pixel.rs:23-25`、`camera_raw.rs` 阶段 3 S 曲线 | 有（无纹理/清晰度时） | photocraft-reference-only | 有：PhotoCraft 移植（未接入 executor，executor 仍跑旧 affine） | conditional | 已接入 | 已接入 | unverified | 旧 W3C affine 只作回归基线 |
| `brightness` | `photocraft-target`（用户裁决：按 Photoshop） | B：`compose/src/adjust/tone.rs:155` `modern_brightness`，`modern_lut(brightness, 0)` 逐通道作用于编码值 | 有（compose 路径） | legacy-provisional | 有（旧加法偏移） | conditional | 已接入 | 已接入 | unverified | 在 A 管线中的位置是 PixCake-specific 顺序：暂定阶段 1 之后、阶段 2 之前，待黑盒确认 |
| `highlights` / `shadows` | `photocraft-target` | A：`camera_raw/local_tone.rs`（引导滤波基底加权的局部曝光） | 无（空间阶段，仅 CPU） | legacy-provisional | 有（旧 0.45 系数） | conditional | 已接入 | 已接入 | unverified | 依赖整幅图像统计；OpenGL 版以移植后的 C++ reference 为 oracle |
| `whites` / `blacks` | `photocraft-target` | A：`camera_raw/whites_blacks.rs`、`per_pixel::white_black_points` | 有（逐像素，统计在 CPU） | legacy-provisional | 有（旧 0.20 系数） | conditional | 已接入 | 已接入 | unverified | 图像自适应曲线；预览代理与导出必须共用统计 |
| `clarity` / `texture` | `photocraft-target` | A：`camera_raw/local_tone.rs`（分带局部对比度） | 无（空间阶段，仅 CPU） | legacy-provisional | 有（三者共用全局对比度） | conditional | 已接入 | 已接入 | unverified | 旧实现与目标语义差距大 |
| `dehaze` | `photocraft-target` | A：`camera_raw.rs` 阶段 2 暗通道先验 + 引导滤波 | 无（空间阶段，仅 CPU） | legacy-provisional | 有（同上） | conditional | 已接入 | 已接入 | unverified | 同上 |
| `vibrance` / `saturation` | `photocraft-target` | A：`camera_raw.rs` 阶段 4 Presence | 有（逐像素） | photocraft-reference-only | 有：PhotoCraft 移植（未接入 executor，executor 仍跑旧最大通道衰减） | conditional | 已接入 | 已接入 | unverified | 不是 Photoshop“自然饱和度”调整图层 |
| 曲线 | `photocraft-target` | A：`camera_raw.rs` 阶段 5 参数曲线与 master/R/G/B 点曲线、`validate_curve` | 有（逐像素） | not-implemented | 无 | 无 | 不绘制 | 不绘制 | unverified | PixCake 曲线参数尚未注册；不能用旧色阶或占位曲线冒充 |
| HSL 八色带 | `photocraft-target` | A：`camera_raw.rs` 阶段 6 Color Mixer（色带名与 PixCake 一致） | 有（逐像素） | photocraft-reference-only | 有：PhotoCraft 移植（未接入 executor，executor 仍跑旧八色带线性权重） | conditional | 已接入 | 已接入 | unverified | 不是 Photoshop“色相/饱和度”六范围 |
| 色彩分级 | `photocraft-target` | A：`camera_raw.rs` 阶段 7 四色轮 + Blending/Balance | 有（逐像素） | photocraft-reference-only | 有：PhotoCraft 移植（未接入 executor，executor 仍跑旧三角分区） | conditional | 已接入 | 已接入 | unverified | PixCake 未注册 Balance；不是 Photoshop ColorBalance |
| 颗粒 `amount/size/roughness` | `photocraft-target` | A：`camera_raw.rs` 阶段 9 grain | 有（逐像素，全分辨率坐标） | legacy-provisional | 有（旧值噪声） | conditional | 已接入 | 已接入 | unverified | 预览代理与导出须用同一全分辨率坐标 |
| 颗粒 `shadows/midtones/highlights/color` | `PixCake-specific` | Camera Raw 无这些子参数 | — | legacy-provisional | 有 | conditional | 已接入 | 已接入 | unverified | PixCake 扩展，单独记录公式 |
| 色阶（RGB 复合） | `photocraft-target` | B：`compose/src/adjust.rs:159-178`、`compose/src/adjust/tone.rs:299-367` | 有 | legacy-provisional | 有（仅复合 RGB） | conditional | 已接入 | 已接入 | unverified | per-channel/master、位深和 float 规则未移植；PixCake 没有独立 R/G/B 参数 |
| 可选颜色 | `photocraft-target` | B：`compose/src/adjust.rs:274`、`compose/src/adjust/color.rs:271-323` | 有 | legacy-provisional | 有（旧 CMYK 经验权重） | conditional | 已接入 | 已接入 | unverified | 九目标与相对/绝对一一对应 |
| 黑白配置文件与八色带黑白混合 | `PixCake-specific` | A 无黑白；B `BlackWhite`（`compose/src/adjust/color.rs:98-119`）只有六权重，仅作参考 | — | legacy-provisional | 有（旧 Rec.709/色带） | conditional | 已接入 | 已接入 | unverified | 改用六范围或 HSL 权重需先改参数模式 |
| 3D LUT/配置文件资源 | `photocraft-target`（采样） | B：`compose/src/adjust/color.rs:127-185` `lut3d_sample` | 有 | legacy-provisional | 条件性 | 无 GPU 3D LUT | 条件性 | 条件性 | unverified | 资源版本、色彩空间、许可证和真实 3D texture 未闭环 |
| 混合模式与图层合成 | `photocraft-target` | B：`compose/src/psblend.rs`、`compose/src/composite.rs`、`gpu/src/compose.wgsl` | 有 | legacy-provisional | 有（normal/multiply/screen） | conditional | 已接入 | 已接入 | unverified | 透明度与工作色彩空间行为未对照 |
| 局部调色 `color.local` | 滑杆数学 `photocraft-target`（A）；蒙版/选区/顺序 `PixCake-specific` | 同全局调色各行 | 同各行 | legacy-provisional | 有 | conditional | 已接入 | 已接入 | unverified | 超过四个蒙版回退 reference；空间阶段按整幅统计计算后再按蒙版混合 |
| 后期调色 `color.post` | 滑杆数学 `photocraft-target`（A）；操作阶段 `PixCake-specific` | 同全局调色各行 | 同各行 | legacy-provisional | 有 | conditional | 已接入 | 已接入 | unverified | 复用旧 tone/reference |
| AI 追色 `color.match` | `PixCake-specific` | PhotoCraft 没有 PixCake 点数/样片业务流程 | — | legacy-provisional | 有（项目简化） | conditional | 已接入 | 已接入 | unverified | 固定 RGB 偏移/写回语义不是 AI 模型 |
| AI 色彩风格 `color.style` | `PixCake-specific` | PhotoCraft 没有对应业务模型 | — | legacy-provisional | 有（项目简化） | conditional | 已接入 | 已接入 | unverified | 固定 RGB 偏移不是 AI 风格实现 |
| 色彩编辑器偏移 | `no-equivalent` | PhotoCraft 没有 Point Color 等价实现 | — | not-implemented | 无 | 无 | 不绘制 | 不绘制 | unverified | 没有吸管/目标颜色/范围参数，保留诊断 |
| 输入色彩管理：sRGB/ICC/RAW | `PixCake-specific`（I/O 适配） | PhotoCraft transfer/CMS 与 `encoding_gamma` 约定只作参考 | — | legacy-provisional | 未闭环 | 未闭环 | 条件性 | 条件性 | unverified | macOS 解码器输出 sRGB RGBA8；A 管线期望显式输入编码，PixCake 尚未声明 |

## 统一执行证据

- PhotoCraft 来源快照：`third_party/photocraft`，当前提交 `885a6ddf27cc58c07faf1722e5c327cd38953657`；来源和许可证要求见移植规范。
- PixCake 当前参数和旧 reference 公式：`gpu/render/src/render_executor.cpp`；凡是对应 PhotoCraft 的行都视为 `legacy-provisional`，直到完成移植和 parity。
- GPU 支持判定：`PassRegistry::gpuSupported` 只接受已登记能力、通道和最多四个蒙版：`gpu/render/src/render_executor.cpp`。
- 预览：`apps/renderd/src/render_service.cpp` 先尝试 GPU，失败或不满足条件时调用 `renderReference`。
- 导出：`apps/exportd/src/export_service.cpp` 使用同一个 `RenderExecutor`，同样支持 GPU 和 reference fallback。
- PhotoCraft Camera Raw 逐像素阶段的 C++ 移植与对照向量：[逐像素移植证据](2026-11-09-camera-raw-per-pixel-port.md)；`tests/gpu/test_camera_raw_port.cpp`。
- 现有色彩分组逐像素和 GPU readback 证据：`docs/superpowers/evidence/2026-11-06-color-groups-reference-pixels.md`；该证据只能证明 PixCake 旧 reference 与 shader 对齐，不能证明 PhotoCraft parity。

## 验收结论

当前可以说：**已登记的 PixCake 旧 reference 算法大部分接入了预览和导出，并有条件性 GPU 路径；除八色带黑白、颗粒扩展子参数、AI 业务和色彩编辑器外，基础调色各项都已有明确的 PhotoCraft 移植目标（亮度按 Photoshop 调整图层，其余多数在 Camera Raw 管线）。白平衡、曝光、对比度、鲜艳度/饱和度、HSL 八色带和色彩分级六行已到 `photocraft-reference-only`：C++ 移植与 PhotoCraft 的 2044 个对照像素一致（最大差 8.34e-7），但尚未接入 executor、GPU 和预览/导出，见[逐像素移植证据](2026-11-09-camera-raw-per-pixel-port.md)。没有任何一项达到 `photocraft-ported`。**

当前不能说：**PhotoCraft 已实现的能力都已移植到 PixCake，或旧 PixCake 公式、GPU readback 已经与 PhotoCraft/像素蛋糕官方逐像素一致。**

在 PhotoCraft 目标完成、输入色彩管理闭环、未实现项补齐、来源与许可证记录完整前，产品验收必须沿用上表的分项状态，不能用单一的“色彩算法已完成”代替。
