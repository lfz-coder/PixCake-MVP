# GPU 色彩基础通道闭环计划

日期：2026-11-06
范围：Temperature、Tint、Exposure、Contrast 的 CPU reference 与 GPU 最小路径
前置记录：[像素处理来源与共识登记](../specs/2026-11-06-pixel-processing-provenance.md)
当前来源裁决：[PhotoCraft 能力来源与 PixCake 移植规范](../specs/2026-11-09-photocraft-reference-and-porting-policy.md)。本计划的 ICC/affine 公式记录是历史基线；PhotoCraft 有对应实现时必须按其目标重新移植和验收。

这份计划把“公式有来源”和“GPU 已实现”拆成两个门槛。只有 CPU 向量、GPU 参数绑定、CPU/GPU 同公式以及 readback 对比都通过后，才能把对应通道登记为 GPU `Implemented`。这不等价于已经证明与 PixCake 官方内部实现逐像素一致。

## 顺序门禁

- [x] **1. 固定 CPU golden vectors**：为白平衡、曝光和对比度固定零值、边界和灰阶向量，明确线性工作缓冲区和取整容差。已落在 `tests/gpu/test_gpu_facade.cpp`。
- [x] **2. 补齐 GPU shader 参数绑定**：完成历史 ICC Bradford/EV/affine 绑定；后续 PhotoCraft-backed 通道必须按固定 PhotoCraft 提交重新绑定，非支持通道继续回退 reference。
- [x] **3. 统一 CPU/GPU 公式**：CPU 和 GPU 使用同一套 PixCake 移植后的语义；历史白平点、Bradford 矩阵、EV 与 contrast 映射不再自动成为 PhotoCraft 目标。
- [x] **4. GPU readback 对比**：`test_gpu_shader_path`（Qt 桌面 QPA）和 `test_gpu_backend_C57_C60`（native CGL）在本机桌面会话中真实执行（输出 `active`，不是 skip），同一输入的 reference/readback 每通道误差 ≤3。变异检查：分别让 shader 忽略四个通道中的任意一个，两个测试都失败。
- [x] **5. 更新状态与文档**：Temperature/Tint/Contrast 已登记为 GPU `Implemented`（`test_gpu_facade` 先 RED 后 GREEN）；`PassRegistry::gpuSupported` 的证据门槛与公式门槛合并；来源登记同步更新。

## 实现边界

- 输入仍是渲染器约定的归一化线性 RGBA 缓冲区；真实 JPEG/PNG/TIFF/RAW 解码和 sRGB/配置文件转换不在本次计划内。
- 温度到 D75/D65/D50、色调半档尺度以及 `-100..100` 到 contrast slope 的参数映射是历史项目决策，不得写成 PhotoCraft 或 PixCake 官方公式；exposure/brightness/contrast 等已有 PhotoCraft 对应实现的通道必须迁移到 PhotoCraft 目标。
- 本次只覆盖单个 `color.global`、normal blend、无 mask/layer 的最小 GPU pass。其他效果和多层 GPU 合成继续回退 reference。

## 验证记录

每一步完成后在本文件勾选，并追加实际命令与结果。最终至少运行相关 GPU facade/backend/shader 测试、`git diff --check` 和完整 CTest；与像素通道无关的环境失败必须单独记录。

## 当前执行记录

- CPU golden vectors：已通过 `test_gpu_facade`。
- Shader 参数、矩阵和公式：已编译并由 `test_gpu_shader_path` 覆盖；该测试在无 OpenGL context 时输出 skip 并返回成功，不能当作 readback 证据。
- 本机 readback 阻塞原因：Qt offscreen 报告 `failed to create OpenGL context`；native CGL 报告 `CGLChoosePixelFormat failed: invalid CoreGraphics connection`。
- 相关回归：GPU facade/backend、renderd/exportd 和 shader 测试共 10/10 通过；`git diff --check` 通过。
- 完整 CTest：56/58 通过；两个失败均为现有 single-instance 临时目录权限/脚本清理问题，与本次像素通道变更无关。

## 收口记录（2026-11-05，Claude 接手）

- Codex 报告的 context 创建失败来自它的执行环境（没有 CoreGraphics 连接）；在本机桌面会话里 native CGL 和 Qt 桌面 QPA 都能建 context。
- 还原了 `tests/CMakeLists.txt` 里“macOS 也强制 offscreen”的改动：那会让 macOS 上的 shader 测试永远 skip，丢掉 readback 证据。
- 无头环境（CI、沙箱）里两个 GPU 测试仍走显式 skip 并返回成功；那种环境下的通过不能当作 GPU 证据。
- 完整 CTest 58/58，single-instance 两个测试在本机通过（Codex 报告的失败同样来自其环境的临时目录权限）。
