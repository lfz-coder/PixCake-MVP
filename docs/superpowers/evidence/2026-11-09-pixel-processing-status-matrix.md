# 像素处理来源与执行现状矩阵

日期：2026-11-09  
状态：当前实现的验收基线

这份矩阵把四件事分开记录：公式的来源、CPU reference 是否存在、GPU 是否能执行、预览/导出是否接入。`GPU 已实现` 只表示 GPU 与本项目 reference 的 readback 对比通过，不表示与 PixCake 官方逐像素一致。

## 状态含义

- **来源**：`standard` 表示 ISO/IEC/ICC/W3C 等规范；`project-decision` 表示项目在没有官方公式时的明确选择；`black-box-partial` 表示固定官方版本的局部测量，不能推出内部公式。
- **reference**：CPU reference 是否有可执行公式和像素测试。
- **GPU**：`conditional` 表示满足已注册通道、最多四个蒙版、支持的混合模式和可用 GPU 时走 shader；否则回退 reference。
- **预览/导出**：两者都调用 `RenderExecutor`；GPU 不可用或不满足条件时由 reference 完成。这个状态不等于真实色彩管理已经闭环。
- **官方一致性**：`unverified` 表示没有官方逐像素证明；`partial` 只表示有局部黑盒观察。

## 当前矩阵

| 能力/分组 | 公式来源 | reference | GPU | 预览 | 导出 | 官方一致性 | 主要证据/限制 |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 白平衡：`temperature` / `tint` | `standard` + `project-decision`：ICC Bradford、CIE 白点；滑杆映射是项目选择 | 有 | conditional | 已接入 | 已接入 | unverified | `gpu/render/src/render_executor.cpp`；未证明官方 D50/D65/D75 映射 |
| `exposure` | `standard`：ISO 2720 EV；线性 `2^EV` 是项目采用的 reference 语义 | 有 | conditional | 已接入 | 已接入 | unverified | 真实输入的线性化仍未闭环 |
| `contrast` | `standard`：W3C affine transfer；滑杆映射是项目选择 | 有 | conditional | 已接入 | 已接入 | unverified | 不证明官方滑杆传递一致 |
| `brightness`、`highlights`、`shadows`、`whites`、`blacks` | `project-decision` | 有 | conditional | 已接入 | 已接入 | unverified | 代码中的局部提升/压低系数不是官方公式 |
| `clarity`、`texture`、`dehaze`、`vibrance`、`saturation` | `project-decision` | 有 | conditional | 已接入 | 已接入 | unverified | 目前共用 reference tone 变换，没有官方传递函数 |
| 色阶 | `project-decision`；输入/输出黑白点和 `10^gray` 中间调 | 有 | conditional | 已接入 | 已接入 | unverified | 上排黑白点最小间隔仍未测 |
| HSL | `project-decision`；八色带、线性权重和 HSL 变换 | 有 | conditional | 已接入 | 已接入 | unverified | 公式和逐像素测试在 reference evidence 中登记 |
| 黑白配置文件与黑白混合 | `project-decision`；Rec.709 亮度和色带混合 | 有 | conditional | 已接入 | 已接入 | unverified | 黑白配置文件下 HSL 按参数禁用规则不编译 |
| 可选颜色 | `project-decision`；CMYK 目标权重、相对/绝对模式 | 有 | conditional | 已接入 | 已接入 | unverified | 没有官方 CMYK 形状和滑杆映射来源 |
| 颜色分级 | `project-decision`；阴影/中间调/高光三角权重 | 有 | conditional | 已接入 | 已接入 | unverified | 平衡参数未注册 |
| 颗粒 `detail` | `project-decision`；晶格值噪声和逐像素噪声 | 有 | conditional | 已接入 | 已接入 | unverified | 颗粒效果为 0 时整步跳过 |
| 局部调色 `color.local` | `project-decision`；复用 tone/reference，蒙版最多四个时有 GPU 路径 | 有 | conditional | 已接入 | 已接入 | unverified | 超过四个蒙版回退 reference；颜色/亮度范围和画笔选区已有项目实现，不等于官方公式已证实 |
| 后期调色 `color.post` | `project-decision`；复用 tone/reference | 有 | conditional | 已接入 | 已接入 | unverified | 没有官方后期调色传递函数 |
| AI 追色 `color.match` | `project-decision`；`amount` 是项目简化变换，`precise` 使用写回语义 | 有（项目简化） | conditional | 已接入 | 已接入 | unverified | 不是官方 AI 模型或官方像素算法 |
| AI 色彩风格 `color.style` | `project-decision`；当前是固定 RGB 风格偏移 | 有（项目简化） | conditional | 已接入 | 已接入 | unverified | 不是官方 AI 模型或官方像素算法 |
| 色彩编辑器偏移 | 无目标颜色来源 | **无** | **无** | 不绘制 | 不绘制 | unverified | 没有吸管/目标颜色/范围参数，代码会给出诊断并跳过 |
| 配置文件“质感/基础”和 3D LUT | 资源来源未登记完整 | reference 仅在资源成功解析时可用 | **无 GPU 3D LUT** | 条件性 | 条件性 | unverified | 当前没有可验收的资源版本、色彩空间和许可证登记 |
| 输入色彩管理：sRGB/ICC/RAW | `standard` 来源已登记，但执行链不完整 | **未闭环** | **未闭环** | 条件性 | 条件性 | unverified | macOS 解码器输出 sRGB RGBA8；`RenderExecutor` 的文档契约要求线性工作缓冲区，二者之间没有明确的 sRGB→linear 步骤 |

## 统一执行证据

- 参数和 reference 公式：`gpu/render/src/render_executor.cpp`。
- GPU 支持判定：`PassRegistry::gpuSupported` 只接受已登记能力、通道和最多四个蒙版：`gpu/render/src/render_executor.cpp`。
- 预览：`apps/renderd/src/render_service.cpp` 先尝试 GPU，失败或不满足条件时调用 `renderReference`。
- 导出：`apps/exportd/src/export_service.cpp` 使用同一个 `RenderExecutor`，同样支持 GPU 和 reference fallback。
- 色彩分组逐像素和 GPU readback 证据：`docs/superpowers/evidence/2026-11-06-color-groups-reference-pixels.md`。

## 验收结论

当前可以说：**大部分已注册的项目 reference 算法已经接入预览和导出，并有条件性 GPU 路径。**

当前不能说：**所有官方像素算法都已实现，或所有公式已经与 PixCake 官方逐像素一致。**

在输入色彩管理完成、未实现项补齐、来源与黑盒证据补全前，产品验收应沿用上表的分项状态，不能用单一的“色彩算法已完成”代替。
