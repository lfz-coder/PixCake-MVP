# gpu/render 剩余实现计划

日期：2026-11-05
依据：项目架构合同、RenderDescription 接口、现有 gpu/render checkpoint、像素蛋糕官方《色彩调节》说明。
算法来源：必须先遵守[PhotoCraft 能力来源与 PixCake 移植规范](../specs/2026-11-09-photocraft-reference-and-porting-policy.md)；本文记录的旧 PixCake reference 只作迁移基线。
本文是 2026-11-05 的阶段计划，文中的“当前”只描述当时 checkpoint；今天的能力来源和完成状态以 2026-11-09 状态矩阵为准。

## 结论边界

当前不是完整实现。已经有 CPU reference 管线、Qt GPU 资源基础和 `color.global` 的最小 shader 路径；完整滤镜/LUT、局部调色、蒙版/图层 GPU 合成以及 renderd/exportd 端到端链路仍缺失。

截至当前 checkpoint，阶段 1 的资源诊断已提交（`7f57722`、`beca2f4`），结构化 3D LUT 的 reference 三线性采样已提交（`3cfe271`），图层/蒙版的 reference 合成和 GPU fallback 标记已提交（`d518c0e`）。这些提交没有把 LUT、图层或蒙版宣称为 GPU 已实现；当前 GPU 仍只覆盖最小的 `color.global` shader 路径。

官方手册只说明用户行为，没有提供产品内部 shader、色彩矩阵、LUT 内容或各滑杆的精确传递函数；PhotoCraft 已有对应实现的能力，应优先采用 PhotoCraft CPU/GPU/test 作为 PixCake C++/OpenGL 的移植目标。PhotoCraft 没有对应能力时才登记为 PixCake-specific 或 no-equivalent。当前 CPU reference 中的旧公式只作为迁移基线，不能继续作为第二个行为标准，也不能直接宣称与官方产品视觉一致。

## 不可违反的项目约束

1. 不修改其他进程维护的 `kernel/capability` 参数模式和 `kernel/render` RenderDescription 字段。
2. 不在 `gpu/render` 复制参数范围、业务字段、图层文档或蒙版枚举。
3. GPU 后端只处理显式上传的像素、蒙版和 LUT 资源，不打开用户文件。
4. 空字段、未知 program、尚未注册/尚未完成来源移植的 operation 和缺少资源必须诊断并跳过/回退，不能猜测默认效果；Free 标记本身不是排除条件。
5. 预览与导出只能共用同一 executor 和 pass registry；尺寸是输入选项，不是第二套参数。
6. 没有真实 renderd/exportd 协议时，不伪造进程端到端完成。

## 阶段 1：契约审计与资源接口

- [x] 对每个 `RenderOperation.program` 建立 pass registry 条目和能力状态：已实现、reference-only、缺资源、未注册。
- [x] 明确 RGBA 输入、蒙版 alpha、图层像素、LUT 资源的尺寸、格式、行距、生命周期和 revision 校验。
- [x] 增加缺失资源、尺寸错误、旧 revision、未知混合模式和未注册 operation 的诊断测试；Free operation 只有在尚未注册或尚未完成来源移植时才走这条路径。
- [x] 不为 `.cube` 或 `.xmp` 猜解析规则；先接收已解析的 LUT/profile 资源。

## 阶段 2：可证实的 GPU pass

- 保留已经通过代码审查的 temperature/tint/exposure/contrast shader path。
- 先检查 PhotoCraft 是否有对应调整和 parity 证据；有则按其行为移植到 GPU pass，没有才依据项目合同、标准或官方/运行中产品证据实现 PixCake-specific pass。
- GPU pass 与 reference 共享参数绑定和顺序，但不复制另一套范围校验。
- 有 GPU 时做 readback 误差测试；无 GPU 时测试必须明确 skip，不能把 skip 当成像素通过。

## 阶段 3：滤镜、配置文件、局部调色

- [x] 增加 LUT 资源注入和 3D LUT 采样的 reference 接口。
- [x] 滤镜强度只按 RenderDescription 的稳定值和已解析 LUT 做线性混合；滤镜内容来自资源，不在代码中编造官方滤镜。
- 黑白/标准/质感/基础只有在 profile 资源或明确的 kernel pass 描述存在时才注册为 GPU pass。
- 局部调色必须使用 operation.mask_ids 指向的 alpha 资源；缺失或尺寸不匹配时跳过并诊断。

## 阶段 4：图层与蒙版 GPU 合成

- [x] reference executor 按输入图层的 order、hidden/visible、opacity、blend_mode 执行，并明确标记 GPU fallback。
- 仅实现合同已登记的混合模式；未知模式保留诊断并跳过图层。
- 父子组、选择区羽化和蒙版密度只有在资源接口提供对应像素数据时执行；没有数据不猜测。
- [x] 增加 normal/multiply/screen、透明度、隐藏图层、蒙版和 revision 的 reference 像素测试。
- [ ] GPU layer/mask shader 仍被现有 facade 阻塞：当前公共接口没有多纹理绑定和多层编排能力，因此保留显式 reference fallback，不添加伪实现。

## 阶段 5：预览/导出接入

- [ ] 等 `apps/renderd`/`apps/exportd` 和 IPC/任务快照接口存在后，再接入同一个 executor。当前仓库只有 `apps/shell`，因此这一阶段暂不能实现，也不能以测试桩冒充端到端链路。
- 预览使用最长边限制；导出使用任务快照和全分辨率。
- 旧 revision 不得覆盖新帧；导出不读取中间预览帧。
- 端到端测试必须覆盖进程崩溃、取消、快照不随滑杆变化和输出帧元数据。

## 完成条件

- 每个声称“已实现”的 program 都有明确 PhotoCraft/PixCake-specific 来源、pass、资源依赖、reference/GPU 测试和失败诊断。
- PhotoCraft-backed 能力仍跑旧公式时标为 `legacy-provisional`，只有 PhotoCraft 语义的 C++ reference 完成后才标为 `photocraft-reference-only`；无来源的视觉公式标为 `no-equivalent` 或 `legacy-provisional`，不标记为官方效果完成。
- 滤镜/LUT、局部调色、蒙版、图层分别有像素级断言。
- 有 GPU 的环境完成真实 readback；无 GPU 环境只通过 reference 和明确 skip 测试。
- renderd/exportd 接入前不宣称端到端预览/导出完成。
