# gpu/render 真实像素处理完成计划

日期：2026-11-05

## 目标

在不修改正在由其他进程维护的 kernel 参数模式与 RenderDescription 契约的前提下，让 `gpu/render` 真正消费 `pixcake::render::RenderDescription` 并生成处理后的像素帧。

第一阶段覆盖：

- 白平衡：色温、色调
- 影调：曝光、对比度、亮度、高光、阴影、白色、黑色、纹理、清晰度、祛雾、鲜艳度、饱和度
- 滤镜/配置文件：稳定程序名和滤镜强度；LUT/CUBE 资源通过渲染资源接口注入
- 局部调色与后期调色：依赖 RenderDescription 的能力、参数和蒙版引用
- 图层：顺序、不透明度、混合模式、蒙版引用
- 预览与导出：同一执行器、不同输入尺寸和输出帧元数据

本阶段不实现 Free 分组、AI 校准模型、RAW 解码、文件导入和生成式能力。

## 不可违反的边界

1. 不修改 `kernel/capability`、`kernel/render` 的参数模式或 RenderDescription 字段。
2. 不在 `gpu/render` 定义第二套业务描述、参数范围或能力私有字段。
3. `gpu/render` 只接收经过 kernel 验证的显示值和稳定通道名。
4. GPU 后端不打开用户文件；纹理和 LUT 通过显式资源输入上传。
5. 预览与导出必须调用同一个 executor；不维护第二套效果公式。
6. Free 或未注册能力必须跳过并返回诊断，不能默认为已生效。
7. 渲染失败不得修改参数文档或图层文档。

## 实施阶段

### A. Reference image pipeline

先实现与 GPU 后端无关的 reference executor，使用 RGBA float/8-bit buffer：

- 输入图像、RenderDescription、蒙版 buffer、图层输入。
- 实现白平衡和影调 pass 的确定性公式。
- 实现强度混合、禁用 pass、顺序和诊断传播。
- 作为 GPU 结果的对照实现和测试 oracle。

### B. GPU pass executor

- 增加 pass registry：由稳定 `program` 名称映射到 shader/pass 实现。
- 实现纹理上传、帧缓冲、shader 参数绑定、ping-pong 中间纹理和 fence。
- 对没有可用 GPU backend 的环境返回明确错误；不得假装输出已渲染。
- Qt backend 只负责 OpenGL 对象，业务公式仍由 render 层的 pass registry 驱动。
- native backend 继续保持接口冻结但未实现。

### C. 色彩与图层 pass

按 RenderDescription 的 `stage` 与 `order` 执行：

1. 白平衡。
2. 影调。
3. 滤镜/配置文件 LUT。
4. 局部调色与蒙版混合。
5. 后期调色。
6. 图层树、不透明度、混合模式和蒙版。
7. 输出色彩格式与预览帧元数据。

如果某项字段在当前契约中为空，保持无效果并写诊断，不猜测业务含义。

### D. 统一预览与导出

- 预览使用最长边限制后的输入尺寸。
- 导出使用任务快照和全分辨率。
- 两者共享完全相同的 pass registry、排序和公式。
- 输出包含修订号、宽、高、行距、RGBA 格式和诊断。
- 预览旧修订不能覆盖新修订。

## 测试与验收

必须新增：

- reference executor 的白平衡、曝光、对比度、蒙版和顺序像素测试。
- GPU executor 的资源绑定、pass 顺序、失败传播和无 backend 测试。
- Qt offscreen smoke 只作为后端连通性测试，不作为像素正确性证据。
- 预览与导出相同输入快照得到相同像素结果的测试。
- Free/未注册能力跳过并保留诊断的测试。
- 图层不透明度、混合模式和蒙版的像素测试。
- 旧修订帧被拒绝或丢弃的测试。

完成条件：

- 至少一张固定 RGBA 测试图能验证白平衡、影调、滤镜、蒙版和图层结果。
- reference 与 GPU 结果在约定误差内一致；没有 GPU 时 reference 测试仍可运行。
- RenderExecutor 不再只是 pass callback 骨架，而是能消费 RenderDescription。
- 预览和导出共用同一执行路径。
- CMake、CTest、架构门禁和 `git diff --check` 全部通过。

## 当前执行状态（2026-11-05）

- [x] A：CPU reference executor，覆盖白平衡、影调、滤镜强度、蒙版权重、图层顺序/不透明度/混合和诊断。
- [x] B：Qt OpenGL 资源基础，包括 shader、program、纹理上传、framebuffer、viewport、readback。
- [x] C（代码闭环）：`color.global` 的 temperature/tint/exposure/contrast shader pass，并与 reference 做像素误差测试。
- [x] C（资源闭环）：显式 LUT/profile provider、结构化 3D LUT reference 三线性采样、强度/蒙版/revision 校验；opaque CUBE/XMP 不解析。
- [ ] C（完整 GPU 覆盖）：滤镜/LUT、局部调色、蒙版和图层的独立 GPU pass。当前图层/蒙版在 GPU execute 中明确回退 reference；现有 facade 尚无多纹理/多层编排接口。
- [ ] D：renderd/exportd 的真实进程接入，以及预览帧和导出任务快照的端到端验证。

注意：当前机器的 Qt offscreen 插件不支持创建 OpenGL context，shader 测试会输出明确的 skip 原因；因此本机 CTest 通过不等于已经获得真实 GPU 像素读回证据。

因此本计划尚未全部完成；当前已完成的是可验收的 CPU reference 管线、Qt GPU 资源基础和最小 shader 代码闭环。
