# 调整图层与颗粒移植证据

日期：2026-11-09
总裁决：[PhotoCraft 能力来源与 PixCake 移植规范](../specs/2026-11-09-photocraft-reference-and-porting-policy.md)
计划：[PhotoCraft 调整图层与颗粒移植](../plans/2026-11-09-photocraft-adjustments-and-grain.md)
上一批：[Camera Raw 逐像素阶段移植证据](2026-11-09-camera-raw-per-pixel-port.md)

## 结论

以下能力已执行 PhotoCraft 公式，状态 `photocraft-ported`（范围：8 位 sRGB 输入，executor 输入未线性化）：

| 能力 | PhotoCraft 来源 | PixCake 移植 |
| --- | --- | --- |
| 色阶（RGB 复合 = master） | `crates/compose/src/adjust/tone.rs` `levels_q`、`levels_gamma`、`tone_luts_q`、`lut` | `gpu/render/src/adjust.cpp` `levelsQ/levelsGamma/levelsLut/sampleLut`；GLSL `levels()` |
| 可选颜色 | `adjust/color.rs` `selective_color_weights`、`selective_color` | `selectiveWeights/selectiveColor`；GLSL `selective()` |
| 3D LUT 采样 | `adjust/color.rs` `lut3d_sample` | `lut3dSample`（三线性；四面体已移植未接参数）；GPU 无 3D LUT |
| 操作回混 | `compose/src/composite.rs` `blend_back` | `blendBack`；GLSL `blend_back()` |
| 图层合成 normal/multiply/screen | `compose/src/psblend.rs` `composite`、`normal_rgb`；`crates/color/src/blend.rs` | `composite/blendRgb`；GLSL `composite_layer()` |
| 颗粒 amount/size/roughness | `crates/algo/src/camera_raw.rs` 阶段 9、`camera_raw/per_pixel.rs` `Grain::new`、`photo_util.rs` `hash01` | `camera_raw.cpp` `hash01/valueNoise/grainOf/grainNoise/applyGrain`；GLSL `kGrain`（uvec2 模拟 64 位，照 `camera_raw.wgsl`） |

颗粒的 shadows/midtones/highlights/colour 仍是 `PixCake-specific` 扩展，叠加在 PhotoCraft 颗粒上；默认分区且 colour = 0 时与 PhotoCraft 逐位一致。

## 对照向量

- 生成：`tools/photocraft_vectors adjust`、`tools/photocraft_vectors grain`（Rust 1.99.1，PhotoCraft `885a6ddf27cc58c07faf1722e5c327cd38953657`）。
- `tests/gpu/data/adjust_vectors.inc`：
  - 色阶 7 组 channel（恒等、gamma 0.5/1.3/2.0/9.99、44..214 输出 48..255、全部组合），4096 项表每 8 项一个样本；
  - 可选颜色 9 目标 × 4 墨 × ±100 × 两种模式 × 25 像素；
  - 3D LUT 17³ 确定性表，三线性与四面体 × 73 像素；
  - 合成 3 模式 × 底图 alpha {0, 0.5, 1} × 源 alpha {0.5, 1} × 不透明度 {0.3, 1} × 16 色对。
- `tests/gpu/data/grain_vectors.inc`：PhotoCraft `develop` 只开颗粒，amount 20/100 × size 0/25/100 × roughness 0/50/100 × （pixel_scale 1、0.25，seed 12345），12×12 图块。
- 结果：`test_adjust_port` 最大差 **1.19e-7**，`test_grain_matches_photocraft` 最大差 **0**（`-ffp-contract=off` 之前为 5.96e-8）（容差 2e-6）。
- 变异检查：色阶 toe 指数 2.2 → 2.3、颗粒第二倍频种子 +7 → +8，测试各自失败；已恢复。

## 门禁逐条（移植规范第 8 节）

| 门禁 | 证据 |
| --- | --- |
| 1 参数、单位、顺序 | 色阶 0..255 / 255、`gamma = 10^input_gray`；可选颜色百分比直传、九目标顺序一致；颗粒 Camera Raw 单位直传；逐像素顺序不变（色阶在 Color Mixer 前、可选颜色在分级前，颗粒在最后） |
| 2 CPU reference 覆盖 | 上述向量；executor 层 `test_reference_adjustments`：色阶 256 级逐级、可选颜色 9 目标 × 两模式 × 64 像素、半透明像素按覆盖率回混、半透明底图三种图层模式、默认扩展值时颗粒逐像素等于 `applyGrain`、半分辨率帧与全分辨率帧同位置颗粒相同、扩展子参数仍起作用 |
| 3 OpenGL readback | `test_gpu_backend_C57_C60` 原生 CGL |
| 4 CPU/GPU 容差 | 新用例 `levels_pc`、`selective_abs_pc`、`grain_pc`、`grain_ext` 整帧与 32 px 分块差 **0**；`layers` 差 1（除法舍入）；`test_exportd` 4200 px 宽 GPU 分块与 reference 差 1 |
| 5 预览与导出同一路径 | renderd/exportd 共用 `RenderExecutor`；renderd 由 `RgbaInput::source_long_edge` 计算 `RenderInputs::pixel_scale`，`test_renderd` 验证半尺寸预览与全尺寸同位置颗粒相同 |
| 6 来源、许可证、文件头 | `adjust.cpp/.hpp` 与 `camera_raw.cpp/.hpp` 文件头带 MIT 声明与来源提交，`check_photocraft_license` 检查；矩阵、公式登记、来源登记同步 |

## 行为变化

- 色阶按 Photoshop 8 位整级量化，gamma > 1 时暗部有 toe（旧公式是连续幂函数）。
- 可选颜色 absolute 模式：红 cyan +100 作用于 (200,50,50) 从 82 变为 50（按墨量余量缩放）。
- 带蒙版或非 normal 的操作按覆盖率（不透明度 × 蒙版）回混，alpha 不变；像素自身的 alpha 不再乘进调整强度。
- 颗粒按全分辨率坐标采样：没有裁剪/旋转/翻转时，预览像素与导出在同一全分辨率位置的颗粒值相同（`test_renderd`）；有几何时两种分辨率各自计算裁剪原点，会错开约 1/pixel_scale 个全分辨率像素，统计特性不变。预览每个像素取一次全强度噪声、不做平均，所以缩小显示的预览比同尺寸查看的导出看起来颗粒更重（PhotoCraft 同样如此）。每张图的颗粒图案相同（seed 0）。
- 渲染库以 `-ffp-contract=off` 编译（Rust 不融合乘加），移植公式与 PhotoCraft 逐位一致。

## 仍未覆盖

1. 高光/阴影（局部拉普拉斯）、白色/黑色（整图统计）、纹理/清晰度、去雾——下一份计划。
2. 曲线与色阶独立 R/G/B 通道（PixCake 未注册参数）。
3. 3D LUT 的 GPU 路径与资源来源/色彩空间登记。
4. 16 位/浮点、ICC/RAW。
5. 与像素蛋糕官方的一致性：未验证。
