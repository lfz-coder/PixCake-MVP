# PixCake 像素处理来源与共识登记

总裁决：[PhotoCraft 能力来源与 PixCake 移植规范](2026-11-09-photocraft-reference-and-porting-policy.md)

日期：2026-11-09（修订）
状态：来源规则；执行状态以 [像素处理来源与执行现状矩阵](../evidence/2026-11-09-pixel-processing-status-matrix.md) 为准；逐参数公式以[像素公式与来源逐参数登记](../evidence/2026-11-09-pixel-formula-registry.md)为准

这份文档记录像素效果为什么这样实现、证据来自哪里，以及哪些结论仍然不能声称等同于 PixCake 官方内部实现。算法有可靠来源，不等于已经证明与官方产品逐像素一致；两者必须分开记录。

## 证据等级

| 等级 | 含义 | 可以支持的结论 |
| --- | --- | --- |
| `product-contract` | PixCake 官方手册、经过版本固定的官方产品行为 | 用户可见名称、入口、范围和交互行为 |
| `normative-standard` | ISO、IEC、CIE、ICC、W3C 等规范 | 色彩空间、编码、混合或测量方法的标准定义 |
| `peer-reviewed` | 可复现的同行评审论文 | 算法的公开方法和适用条件 |
| `library-reference` | 第三方库官方文档或源码 | 库的实现语义、输入输出和限制 |
| `photocraft-reference` | 固定 PhotoCraft 提交中的 CPU 实现（Camera Raw 滤镜管线或 compose 调整/合成）、GPU shader、lowering 和 parity 测试 | PhotoCraft 已有能力的 PixCake 移植目标；不是 Adobe 官方源码或 PixCake 当前实现的证明 |
| `black-box-product` | 固定官方版本、输入、参数和输出的对照测量 | 对该版本产品行为的实证近似，不能推断内部公式 |
| `project-decision` | 项目在上述证据不足时作出的明确工程选择 | 可复现的项目行为，不能宣称官方一致 |

没有 `normative-standard`、`peer-reviewed`、`library-reference` 或 `photocraft-reference` 支撑的通用公式，不得标为“已验证”。PhotoCraft 已有对应实现时，PixCake 旧公式必须标为 `legacy-provisional`，不得继续写成当前行为来源；没有 `black-box-product` 或官方内部资料时，也不得把 PhotoCraft 或项目公式写成“官方公式”。

## 统一输入约定与当前差异

设计契约要求 `gpu::RgbaImage` 是渲染器的**归一化线性工作缓冲区**：每个 RGBA8 通道在进入 reference/GPU executor 前应已经被转换到 `[0,1]` 的线性值，alpha 保持线性覆盖率。真正的 JPEG/PNG/TIFF/RAW 解码器在 `apps/renderd`/`apps/exportd` 适配层完成格式解码和色彩配置；不能让 `kernel/` 或 GPU 后端打开用户文件。

如果输入来自 sRGB 编码文件，适配层必须按 IEC 61966-2-1 的 sRGB 传递函数转换到线性值；输出编码时执行相反转换。当前 macOS ImageIO 解码器的接口仍标记输出为 sRGB RGBA8，这条链路标记为**未闭环**。

**2026-11-09 接入 PhotoCraft Camera Raw 后的实际约定**（[接入计划](../plans/2026-11-09-camera-raw-executor-wiring.md) Global Constraints）：executor 读到的 RGBA8 就是解码器输出的 sRGB 编码值，直接作为 Camera Raw 的显示编码输入（`encoding_gamma = 0`），不在 executor 内线性化；Camera Raw 各阶段在内部按 PhotoCraft 的域显式做 sRGB 传递（例如阶段 1 解码到线性光乘增益再编码）。上面的“线性工作缓冲区”契约因此对这些阶段不成立，在 ICC/RAW 色彩管理闭环前，已移植的结果只对 8 位 sRGB 输入成立。

## 第一批效果登记

### Exposure / 曝光

状态：`photocraft-target`，2026-11-09 起 `photocraft-ported`（8 位 sRGB 输入）：executor 执行 Camera Raw 阶段 1，与白平衡合并为一次线性光增益，见[逐像素移植证据](../evidence/2026-11-09-camera-raw-per-pixel-port.md)。下面的简单 EV 公式是被替换的旧路径。

参数 `exposure` 被项目参数模式定义为 EV 偏移，单位为 stop，范围 `-5.00..5.00`，步进 `0.05`，默认 `0.00`。

PixCake 旧 reference 使用的线性工作值公式（已被替换，保留作迁移基线）：

```text
L_out = clamp(L_in * 2^exposure, 0, 1)
```

ISO 2720 支持 EV 的 base-2 基础语义，这条简单公式本身与 PhotoCraft 目标的核心一致（线性光乘 `2^EV`），差别在于输入编码、与白平衡增益的合并和裁剪位置。PhotoCraft 的目标是 Camera Raw 管线阶段 1：`third_party/photocraft/crates/algo/src/camera_raw/per_pixel.rs:13-20` 的 `gains()` 与 `crates/algo/src/camera_raw.rs` 的 `balance()`——文档值按 sRGB 解码到线性光，乘以按亮度归一的白平衡增益和 `2^exposure`，非负裁剪后重新编码。Photoshop“曝光度”调整图层（`crates/compose/src/adjust.rs:138-157`，含 offset/gamma 和 gamma 2.2 transfer）不是 PixCake 曝光滑杆的目标。移植时以 PhotoCraft 提交 `885a6ddf27cc58c07faf1722e5c327cd38953657` 为准。

来源：

1. ISO 2720:1974，曝光表规范和曝光值刻度：[ISO official catalogue](https://committee.iso.org/cms/live/live/en/sites/isoorg/contents/data/standard/00/76/7690.html?browse=tc)。标准预览中的曝光值条款说明一个曝光值单位对应两倍曝光量。
2. Hasler et al., *Automatic exposure algorithms for digital photography*, Multimedia Tools and Applications, DOI [10.1007/s11042-019-08318-1](https://doi.org/10.1007/s11042-019-08318-1)。

已完成：输入编码（sRGB，`encoding_gamma=0`）、白平衡合并和 8 位 CPU/GPU parity（readback 差 0）。仍未覆盖：16 位与 F32 深度、ICC/RAW 输入。

### sRGB 编码转换

状态：`normative-standard`；适配层待真实图像 I/O 接入时实施。

来源：IEC 61966-2-1:1999 定义 sRGB 默认 RGB 色彩空间和传递函数；ICC 的公开解释文档列出同一转换和 D65 基准：[IEC publication](https://webstore.iec.ch/en/publication/6169)、[ICC sRGB interpretation](https://www.color.org/sRGB.pdf)。

项目不在 GPU effect pass 中做输入文件的色彩管理；它属于图像 I/O/color-management 适配层。PhotoCraft Camera Raw 管线接收编码值，并在各阶段内部切换线性光与 sRGB 编码域；PixCake 的移植（`gpu/render/src/camera_raw.cpp` 与 `kColor`）按同样方式把帧值当作 sRGB 编码输入（`encoding_gamma=0`），阶段内的 sRGB 传递是公式的一部分，不是输入解码。预览和导出走同一 executor，因此不会重复 gamma 转换。

### Temperature / 色温与 Tint / 色调

状态：`photocraft-target`（Camera Raw 来源族），2026-11-09 起 `photocraft-ported`（8 位 sRGB 输入），见[逐像素移植证据](../evidence/2026-11-09-camera-raw-per-pixel-port.md)。下面的 Bradford 白点路径（`PP-WB-001`）**已被替换**，不再执行。PhotoCraft 公式是 `third_party/photocraft/crates/algo/src/camera_raw.rs:670` 的 `white_balance_gains(temperature, tint)`：得到三通道增益、按亮度归一后与曝光一起在线性光中相乘（`camera_raw/per_pixel.rs:13-20`）。注意方向：Camera Raw 的 tint 正值降低绿色（偏品红），旧 Bradford 映射的 tint 正值偏绿；像素蛋糕的方向待黑盒确认。下面的 Bradford 记录保留为旧路径的来源说明。

色温的色度学变换采用 CIE D50、D65、D75 白点和 ICC Annex E 的线性 Bradford chromatic adaptation transform。项目映射明确写成：`temperature=-100` 为 D75、`temperature=0` 为 D65、`temperature=100` 为 D50（正值表示更暖）；中间值在线性 `xy` 白点之间插值。这个滑杆到白点的映射是项目共识，不是官方 PixCake 公式。

色调不再使用原来的经验 RGB 加减系数。项目共识是对 Bradford 绿锥响应使用对称半档比例：`tint=-100..100` 对应 `2^(-0.5)..2^(0.5)`，再经 Bradford 逆变换回线性 sRGB。该尺度是项目参数映射；Bradford 矩阵和色适应过程来自 ICC 规范。

来源：

1. ICC.1-2022-05 Annex E，线性 Bradford 变换和白点适应矩阵：[ICC specification](https://www.color.org/specifications/ICC.1-2022-05.pdf)。
2. CIE 015:2018，色度学、标准观察者和标准 illuminant 的推荐：[CIE Colorimetry](https://www.cie.co.at/publications/colorimetry-4th-edition)。
3. sRGB D65 的 primaries、白点和 XYZ 矩阵：[ICC sRGB interpretation](https://www.color.org/sRGB.pdf)。

仍不能声称：这个项目滑杆到 D50/D65/D75 的映射就是 PixCake 官方内部映射。要升级为 `verified-against-product`，还需要固定官方版本的黑盒输入/输出对照。

### Contrast / 对比度

状态：`photocraft-target`（Camera Raw 来源族），2026-11-09 起 `photocraft-ported`（8 位 sRGB 输入；仅 Contrast，纹理/清晰度另计），见[逐像素移植证据](../evidence/2026-11-09-camera-raw-per-pixel-port.md)。下面的旧 W3C affine 公式（`PP-CONTRAST-001`）已被替换。

PixCake 旧 reference 采用 W3C Filter Effects 的 affine transfer：

```text
amount = 1 + contrast / 100
C_out = clamp((C_in - 0.5) * amount + 0.5, 0, 1)
```

W3C 规定 `contrast(100%)` 保持输入、`contrast(0%)` 变成中灰；这只保留为旧路径的标准语义。PhotoCraft 的目标是 Camera Raw 阶段 3 的对比度：在亮度上围绕中灰的 S 曲线、保持颜色比例，强度 `contrast/100·0.6`（`crates/algo/src/camera_raw/per_pixel.rs:23-25`）。Photoshop 亮度/对比度调整图层的 modern contrast（`crates/compose/src/adjust/tone.rs:228-270`）不是本滑杆的目标。旧 affine 实现已由该 S 曲线替换，矩阵和测试已同步。[W3C Filter Effects](https://www.w3.org/TR/filter-effects/)

### Brightness、Highlights、Shadows、Whites、Blacks、Texture、Clarity、Dehaze、Vibrance、Saturation

状态：均为 `photocraft-target`。Brightness 按 2026-11-09 用户裁决以 Photoshop 现代亮度为目标（调整图层来源族，`crates/compose/src/adjust/tone.rs:155` `modern_brightness`），其余属于 Camera Raw 来源族。2026-11-09 起 Brightness 与 Vibrance/Saturation 为 `photocraft-ported`（8 位 sRGB 输入，见[逐像素移植证据](../evidence/2026-11-09-camera-raw-per-pixel-port.md)）；Highlights、Shadows、Whites、Blacks、Texture、Clarity、Dehaze 仍是 `legacy-provisional` 系数，虽有 reference 和条件性 GPU 路径，也不能作为 PhotoCraft 或官方效果完成。

Highlights/Shadows、Texture/Clarity 见 `crates/algo/src/camera_raw/local_tone.rs`，Whites/Blacks 见 `crates/algo/src/camera_raw/whites_blacks.rs`，Dehaze 与 Vibrance/Saturation 见 `crates/algo/src/camera_raw.rs` 阶段 2 与阶段 4。Camera Raw 没有 Brightness，它在 Camera Raw 阶段顺序中的位置是 PixCake-specific，见公式登记。Photoshop 调整图层族（`BrightnessContrast`、`Levels`、`Curves`、`HueSaturation`、`Vibrance`、`BlackWhite`、`ColorBalance`、`SelectiveColor`）只在 PixCake 语义确实相同时才作为目标，目前确认的是 Brightness（仅亮度部分）、Levels 与 SelectiveColor；不能按名字匹配。没有对应能力的参数才标为 `PixCake-specific`。当前代码公式必须保持显式、可测试，并标明 `legacy-provisional` 或 `PixCake-specific`，不能把 GPU 与本项目 reference 的一致误写成 PhotoCraft 一致。

### LUT 与图层混合

状态：PhotoCraft 对应的 LUT/混合/图层实现是 `photocraft-target`；2026-11-09 起 LUT 三线性采样（`lut3d_sample`）、操作回混（`blend_back`）和 `normal`/`multiply`/`screen` 图层合成（`psblend::composite`）为 `photocraft-ported`（8 位 sRGB 输入，见[调整图层与颗粒移植证据](../evidence/2026-11-09-photocraft-adjustments-and-grain-port.md)），具体资源必须来自已解析资源，不能在代码里编造官方滤镜内容。输入资源的来源、版本、色彩空间和许可证仍需登记；当前 GPU 3D LUT 路径仍未闭环。

## 实现状态与门禁

- `gpu/render` 的 shader 对已登记通道执行与 CPU reference 对应的公式。GPU 支持是条件性的：能力、通道、混合模式和蒙版数量必须通过 `PassRegistry::gpuSupported`；否则由 reference fallback。readback 证明的是 GPU 与本项目 reference 一致，不证明与 PixCake 官方一致。
- reference 结果可以帮助开发和测试，但不能单独证明官方视觉一致。
- 每个新 effect 必须先按 PhotoCraft 移植规范判断来源，更新本文件和状态矩阵，再添加 pass registry 条目和像素测试。
- 测试必须至少覆盖零值恒等性、正负边界、灰阶向量、饱和裁剪、蒙版 0/50/100% 和 revision 失配。
- 真实图像解码/编码已经存在；已移植的 Camera Raw 阶段把帧值当作 sRGB 编码输入，ICC/RAW 与非 sRGB 来源的转换尚未闭环；在该项完成前，不得把端到端色彩管理标为完成。

## 当前状态的唯一来源

逐参数的当前状态以 [2026-11-09 像素处理来源与执行现状矩阵](../evidence/2026-11-09-pixel-processing-status-matrix.md) 和 [PhotoCraft 移植规范](2026-11-09-photocraft-reference-and-porting-policy.md) 为准。本文保留公式来源和证据等级；历史决策记录只说明当时为什么采用某个项目公式，不覆盖矩阵中的当前实现状态。若本文、矩阵、代码注释或测试名称出现冲突，必须在同一变更中修正，不能选择性引用旧 PixCake 公式。

## 历史决策记录（不覆盖当前矩阵）

2026-11-06：先采用有明确 EV 依据的 Exposure 作为第一条 GPU/reference 垂直切片；随后将 Temperature/Tint 改为 ICC Bradford 色适应 reference，将 Contrast 改为 W3C affine contrast reference。三者仍不报告为 GPU 已实现或官方等价效果。
2026-11-06：完成 Temperature/Tint/Contrast 的 shader 参数绑定和 CPU/GPU 共用公式，但本机 Qt offscreen 与 native CGL 均无法创建 OpenGL context，因此没有把三者提升为 GPU `Implemented`；待真实 backend readback 成功后再更新门禁。
2026-11-05：在本机桌面会话中，native CGL（`test_gpu_backend_C57_C60`）与 Qt 桌面 QPA（`test_gpu_shader_path`）都能创建 OpenGL context，两者的 readback 对比均通过。上一条记录里的 context 创建失败来自执行环境没有 CoreGraphics 连接，不是代码问题。变异检查：让 shader 分别忽略色温、色调、曝光、对比度，两个测试都失败；恢复后通过。据此把 Temperature/Tint/Contrast 提升为 GPU `Implemented`，`PassRegistry::gpuSupported` 的证据门槛与公式门槛合并。macOS 上 `test_gpu_shader_path` 继续使用桌面 QPA（offscreen 在 macOS 上建不出 context，只会 skip）。
2026-11-06：全局色彩其余参数（亮度、高光、阴影、白色、黑色、纹理、清晰度、祛雾、鲜艳度、饱和度、AI 风格程度，以及色阶、HSL、黑白配置文件与黑白混合、可选颜色、颜色分级）和细节颗粒接入着色器，逐组在原生 CGL 上做读回对比（`test_gpu_backend_C57_C60`，最大差 1），变异检查逐组失败。`pixcake.color.global` 与 `pixcake.detail` 据此登记为 GPU `Implemented`；色彩编辑器偏移两条路径都不画。renderd 改为优先 GPU、失败回退参考。同日，局部调色、后期调色、AI 追色与风格、每个操作最多四个蒙版、正片叠底与滤色混合、图层合成也接入着色器并逐项读回对比（最大差 1），`pixcake.color.local/post/match/style` 一并登记为 GPU `Implemented`；3D LUT 仍只有参考实现。这里的“Implemented”只表示当时 PixCake CPU/GPU 对齐；那些旧公式现在按 `legacy-provisional` 处理，PhotoCraft-backed 能力必须重新做移植和 parity（`docs/superpowers/evidence/2026-11-06-color-groups-reference-pixels.md`）。
2026-11-09：白平衡、曝光、对比度、亮度、鲜艳度/饱和度、HSL 八色带和色彩分级接入 PhotoCraft 移植公式（`camera_raw.cpp`、`brightness.cpp`、`kColor`），替换 `PP-WB-001` Bradford、`PP-EXP-001` 线性 EV 和 `PP-CONTRAST-001` affine 以及旧 HSL/分级/亮度/鲜艳度公式；CPU 与 PhotoCraft 对照向量一致，GPU readback 与 reference 差 ≤1；状态 `photocraft-ported`（8 位 sRGB 输入）。见[逐像素移植证据](../evidence/2026-11-09-camera-raw-per-pixel-port.md)。
2026-11-09（第二批）：色阶（Photoshop master，8 位整级量化与暗部 toe）、可选颜色、3D LUT 采样、操作回混与图层合成、Camera Raw 颗粒（全分辨率坐标）接入 PhotoCraft 移植（`gpu/render/src/adjust.cpp`、`camera_raw.cpp`、`kColor`/`kGrain`/`kPrelude`），CPU 与 PhotoCraft 对照向量一致，GPU readback 差 ≤1；状态 `photocraft-ported`（8 位 sRGB 输入）。渲染库以 `-ffp-contract=off` 编译，与 Rust 一样不融合乘加。见[调整图层与颗粒移植证据](../evidence/2026-11-09-photocraft-adjustments-and-grain-port.md)。
2026-11-09（第三批）：去雾、高光/阴影、纹理/清晰度（与对比度同一阶段）、白色/黑色接入 PhotoCraft Camera Raw 整帧阶段（`gpu/render/src/camera_raw_spatial.cpp`、`whites_blacks_tables.inc`），替换旧 lift（0.45/0.20）与“局部对比度”占位；CPU 与 PhotoCraft 对照向量逐位一致，预览和导出按 PhotoCraft `develop` 顺序执行；没有 GLSL，GPU 回退 reference，状态 `photocraft-reference-only`。见[整帧阶段移植证据](../evidence/2026-11-09-photocraft-spatial-stages-port.md)。
