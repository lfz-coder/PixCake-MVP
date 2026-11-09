# PixCake 像素处理来源与共识登记

日期：2026-11-09（修订）
状态：来源规则；执行状态以 [像素处理来源与执行现状矩阵](../evidence/2026-11-09-pixel-processing-status-matrix.md) 为准

这份文档记录像素效果为什么这样实现、证据来自哪里，以及哪些结论仍然不能声称等同于 PixCake 官方内部实现。算法有可靠来源，不等于已经证明与官方产品逐像素一致；两者必须分开记录。

## 证据等级

| 等级 | 含义 | 可以支持的结论 |
| --- | --- | --- |
| `product-contract` | PixCake 官方手册、经过版本固定的官方产品行为 | 用户可见名称、入口、范围和交互行为 |
| `normative-standard` | ISO、IEC、CIE、ICC、W3C 等规范 | 色彩空间、编码、混合或测量方法的标准定义 |
| `peer-reviewed` | 可复现的同行评审论文 | 算法的公开方法和适用条件 |
| `library-reference` | 第三方库官方文档或源码 | 库的实现语义、输入输出和限制 |
| `black-box-product` | 固定官方版本、输入、参数和输出的对照测量 | 对该版本产品行为的实证近似，不能推断内部公式 |
| `project-decision` | 项目在上述证据不足时作出的明确工程选择 | 可复现的项目行为，不能宣称官方一致 |

没有 `normative-standard`、`peer-reviewed` 或 `library-reference` 支撑的通用公式，不得标为“已验证”。没有 `black-box-product` 或官方内部资料时，不得把项目公式写成“官方公式”。

## 统一输入约定与当前差异

设计契约要求 `gpu::RgbaImage` 是渲染器的**归一化线性工作缓冲区**：每个 RGBA8 通道在进入 reference/GPU executor 前应已经被转换到 `[0,1]` 的线性值，alpha 保持线性覆盖率。真正的 JPEG/PNG/TIFF/RAW 解码器在 `apps/renderd`/`apps/exportd` 适配层完成格式解码和色彩配置；不能让 `kernel/` 或 GPU 后端打开用户文件。

如果输入来自 sRGB 编码文件，适配层必须按 IEC 61966-2-1 的 sRGB 传递函数转换到线性值；输出编码时执行相反转换。当前 macOS ImageIO 解码器的接口仍标记输出为 sRGB RGBA8，而 reference/GPU 仍按线性工作缓冲区解释，因此这条链路标记为**未闭环**，不能把真实图片上的曝光/对比度结果称为完成的线性颜色管理。

## 第一批效果登记

### Exposure / 曝光

状态：`adopted-reference`；GPU 最小路径允许执行；尚未证明等同 PixCake 官方效果。

参数 `exposure` 被项目参数模式定义为 EV 偏移，单位为 stop，范围 `-5.00..5.00`，步进 `0.05`，默认 `0.00`。

采用的线性工作值公式：

```text
L_out = clamp(L_in * 2^exposure, 0, 1)
```

采用理由：ISO 2720 将一个曝光值单位定义为曝光量乘以 2；同行评审的自动曝光论文也使用 EV 的 base-2 定义。该来源支持“一个 stop 对应一倍线性曝光”的算法语义，但不规定 PixCake 的滑杆传递、局部高光保护或最终观感。因此项目只采用基础线性 EV 变换，不把它标为官方复刻。

来源：

1. ISO 2720:1974，曝光表规范和曝光值刻度：[ISO official catalogue](https://committee.iso.org/cms/live/live/en/sites/isoorg/contents/data/standard/00/76/7690.html?browse=tc)。标准预览中的曝光值条款说明一个曝光值单位对应两倍曝光量。
2. Hasler et al., *Automatic exposure algorithms for digital photography*, Multimedia Tools and Applications, DOI [10.1007/s11042-019-08318-1](https://doi.org/10.1007/s11042-019-08318-1)。

验证要求：线性灰阶 `0.25` 在 `+1 EV` 后为 `0.5`，在 `-1 EV` 后为 `0.125`；输入为 0 或 1 时结果保持在边界；CPU reference 与 GPU readback 的每通道误差必须在测试声明的容差内。

### sRGB 编码转换

状态：`normative-standard`；适配层待真实图像 I/O 接入时实施。

来源：IEC 61966-2-1:1999 定义 sRGB 默认 RGB 色彩空间和传递函数；ICC 的公开解释文档列出同一转换和 D65 基准：[IEC publication](https://webstore.iec.ch/en/publication/6169)、[ICC sRGB interpretation](https://www.color.org/sRGB.pdf)。

项目不在 GPU effect pass 中重复实现 sRGB 解码；它属于图像 I/O/color-management 适配层。GPU executor 的输入契约保持线性，避免同一图像在预览和导出中发生重复 gamma 转换。

### Temperature / 色温与 Tint / 色调

状态：`adopted-reference`；GPU 最小路径已实现（2026-11-05 readback 对比通过）；尚未证明等同 PixCake 官方效果。

色温的色度学变换采用 CIE D50、D65、D75 白点和 ICC Annex E 的线性 Bradford chromatic adaptation transform。项目映射明确写成：`temperature=-100` 为 D75、`temperature=0` 为 D65、`temperature=100` 为 D50（正值表示更暖）；中间值在线性 `xy` 白点之间插值。这个滑杆到白点的映射是项目共识，不是官方 PixCake 公式。

色调不再使用原来的经验 RGB 加减系数。项目共识是对 Bradford 绿锥响应使用对称半档比例：`tint=-100..100` 对应 `2^(-0.5)..2^(0.5)`，再经 Bradford 逆变换回线性 sRGB。该尺度是项目参数映射；Bradford 矩阵和色适应过程来自 ICC 规范。

来源：

1. ICC.1-2022-05 Annex E，线性 Bradford 变换和白点适应矩阵：[ICC specification](https://www.color.org/specifications/ICC.1-2022-05.pdf)。
2. CIE 015:2018，色度学、标准观察者和标准 illuminant 的推荐：[CIE Colorimetry](https://www.cie.co.at/publications/colorimetry-4th-edition)。
3. sRGB D65 的 primaries、白点和 XYZ 矩阵：[ICC sRGB interpretation](https://www.color.org/sRGB.pdf)。

仍不能声称：这个项目滑杆到 D50/D65/D75 的映射就是 PixCake 官方内部映射。要升级为 `verified-against-product`，还需要固定官方版本的黑盒输入/输出对照。

### Contrast / 对比度

状态：`adopted-reference`；GPU 最小路径已实现（2026-11-05 readback 对比通过）；尚未证明等同 PixCake 官方效果。

采用 W3C Filter Effects `contrast()` 的 affine transfer：

```text
amount = 1 + contrast / 100
C_out = clamp((C_in - 0.5) * amount + 0.5, 0, 1)
```

W3C 规定 `contrast(100%)` 保持输入、`contrast(0%)` 变成中灰；项目把现有 `-100..100` 参数映射到 `amount=0..2`。这个参数范围映射是项目共识，W3C 只提供 transfer 语义，不证明 PixCake 使用同一滑杆映射。[W3C Filter Effects](https://www.w3.org/TR/filter-effects/)

### Brightness、Highlights、Shadows、Whites、Blacks、Texture、Clarity、Dehaze、Vibrance、Saturation

状态：`project-reference`；当前有 reference 和条件性 GPU 路径，但不能标为官方效果完成。

它们仍缺少足以证明官方等价的传递函数。代码中的公式必须继续保持显式、可测试，并在来源不足时标为 `project-decision`，不能把 GPU 与本项目 reference 的一致误写成官方一致。

### LUT 与图层混合

状态：LUT 三线性采样和 `normal`/`multiply`/`screen` reference 合成属于条件性 reference。具体 LUT/profile 资源必须来自已解析资源，不能在代码里编造官方滤镜内容；当前 GPU 没有 3D LUT 路径。输入资源的来源、版本、色彩空间和许可证仍需登记。

## 实现状态与门禁

- `gpu/render` 的 shader 对已登记通道执行与 CPU reference 对应的公式。GPU 支持是条件性的：能力、通道、混合模式和蒙版数量必须通过 `PassRegistry::gpuSupported`；否则由 reference fallback。readback 证明的是 GPU 与本项目 reference 一致，不证明与 PixCake 官方一致。
- reference 结果可以帮助开发和测试，但不能单独证明官方视觉一致。
- 每个新 effect 必须先更新本文件，再添加 pass registry 条目和像素测试。
- 测试必须至少覆盖零值恒等性、正负边界、灰阶向量、饱和裁剪、蒙版 0/50/100% 和 revision 失配。
- 真实图像解码/编码已经存在，但 sRGB/ICC/RAW 到线性工作缓冲区的转换契约尚未闭环；在该项完成前，不得把端到端线性像素链路标为完成。

## 当前状态的唯一来源

逐参数的当前状态以 [2026-11-09 像素处理来源与执行现状矩阵](../evidence/2026-11-09-pixel-processing-status-matrix.md) 为准。本文保留公式来源和证据等级；历史决策记录只说明当时为什么采用某个项目公式，不覆盖矩阵中的当前实现状态。若本文、矩阵、代码注释或测试名称出现冲突，必须在同一变更中修正，不能选择性引用。

## 决策记录

2026-11-06：先采用有明确 EV 依据的 Exposure 作为第一条 GPU/reference 垂直切片；随后将 Temperature/Tint 改为 ICC Bradford 色适应 reference，将 Contrast 改为 W3C affine contrast reference。三者仍不报告为 GPU 已实现或官方等价效果。
2026-11-06：完成 Temperature/Tint/Contrast 的 shader 参数绑定和 CPU/GPU 共用公式，但本机 Qt offscreen 与 native CGL 均无法创建 OpenGL context，因此没有把三者提升为 GPU `Implemented`；待真实 backend readback 成功后再更新门禁。
2026-11-05：在本机桌面会话中，native CGL（`test_gpu_backend_C57_C60`）与 Qt 桌面 QPA（`test_gpu_shader_path`）都能创建 OpenGL context，两者的 readback 对比均通过。上一条记录里的 context 创建失败来自执行环境没有 CoreGraphics 连接，不是代码问题。变异检查：让 shader 分别忽略色温、色调、曝光、对比度，两个测试都失败；恢复后通过。据此把 Temperature/Tint/Contrast 提升为 GPU `Implemented`，`PassRegistry::gpuSupported` 的证据门槛与公式门槛合并。macOS 上 `test_gpu_shader_path` 继续使用桌面 QPA（offscreen 在 macOS 上建不出 context，只会 skip）。
2026-11-06：全局色彩其余参数（亮度、高光、阴影、白色、黑色、纹理、清晰度、祛雾、鲜艳度、饱和度、AI 风格程度，以及色阶、HSL、黑白配置文件与黑白混合、可选颜色、颜色分级）和细节颗粒接入着色器，逐组在原生 CGL 上做读回对比（`test_gpu_backend_C57_C60`，最大差 1），变异检查逐组失败。`pixcake.color.global` 与 `pixcake.detail` 据此登记为 GPU `Implemented`；色彩编辑器偏移两条路径都不画。renderd 改为优先 GPU、失败回退参考。同日，局部调色、后期调色、AI 追色与风格、每个操作最多四个蒙版、正片叠底与滤色混合、图层合成也接入着色器并逐项读回对比（最大差 1），`pixcake.color.local/post/match/style` 一并登记为 GPU `Implemented`；3D LUT 仍只有参考实现。公式本身仍是项目暂定，不代表与像素蛋糕一致（`docs/superpowers/evidence/2026-11-06-color-groups-reference-pixels.md`）。
