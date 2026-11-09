# Kernel 预览与绘制管线闭环计划

日期：2026-11-05

像素能力来源按[PhotoCraft 能力来源与 PixCake 移植规范](../specs/2026-11-09-photocraft-reference-and-porting-policy.md)执行：PhotoCraft-backed 能力保留 PixCake 的阶段与 RenderDescription 边界并进入 C++/OpenGL 移植；只有尚未注册、`no-equivalent` 或缺少资源的 operation 才能跳过并诊断。本文不把 PhotoCraft 的 Rust/wgpu/egui 架构带进 kernel。

## 范围

这份计划承接 `2026-11-05-pixcake-render-contract.md` 和
`2026-11-05-kernel-write-boundary-closure.md`，只处理内核侧仍缺失、且会被并行的
OpenGL 后端、`gpu/render` 和 UI 消费的内容。实现必须保持 kernel 不依赖 Qt、OpenGL、网络、推理运行时和窗口对象。

本阶段交付的是可验证的内核数据和状态闭环：参数预览请求、最新值合并、稳定管线顺序、图层/蒙版合成输入和跨进程内容协议。OpenGL 着色、纹理上传、共享内存读写和 UI 控件由并行工作包负责。

## 当前缺口

1. `phase_one_pipeline()` 只有顺序数组，不能验证依赖缺失、重复步骤和环路；渲染描述的 `order` 也没有严格跟随架构管线顺序。
2. `EditDocument::set_preview()` 能保存一个预览值，但没有内核侧的请求目标、修订条件、预览模式、最长边和“只保留最新值”协调器。
3. `RenderDescription` 已有参数操作，却没有稳定的图层、蒙版、选择区和不透明度输入，GPU 消费侧无法区分“参数”与“合成输入”。
4. 进程边界缺少无 Qt 的预览请求/响应和帧元数据内容结构，容易让 UI、renderd 和 GPU 各自定义字段。

## 实现任务

### Task 1：管线依赖和唯一顺序 `[x]`

- 扩展 `PipelineStep` 的依赖声明，保留现有能力标识和旧片段兼容行为。
- 为阶段一管线补齐依赖边，启动校验拒绝未知依赖、重复步骤、同一能力的非法重复和环路。
- 提供稳定的拓扑顺序/能力排序 API，`kernel/render` 按它生成 `RenderOperation::order`。
- 修正 `color.match → color.style → color.global → color.local → 人像/图像 → color.post → manual.* → crop` 的顺序；PhotoCraft-backed 能力保留阶段标识并由后续 render pass 消费，尚未注册或 `no-equivalent` 的 operation 才不生成绘制操作并保留诊断。

### Task 2：预览请求和最新值合并 `[x]`

- 新增 `kernel/preview`，定义 `PreviewRequest`、`PreviewSnapshot` 和 `PreviewCoordinator`。
- 请求必须绑定原图编号、编辑实例编号和基础修订号；参数范围/步进来自 `ParameterMode`，最长边限制为 1–4000。
- 拖动期间只覆盖一个待处理请求；新请求替换旧请求，不形成队列。
- `take_latest()` 生成只读 `RenderDescription`，不写盘、不增加正式修订号；极速导出打开时拒绝预览提交。
- 提交、过期修订、目标不匹配、未知参数和非法数值分别返回明确结果码。

### Task 3：渲染描述的图层和蒙版输入 `[x]`

- 在 `kernel/render` 增加稳定的 `RenderLayer`、选择区和蒙版输入结构。
- 增加接受 `LayerDocument` 的编译重载；只读取图层状态，把混合模式、透明度、隐藏、父子关系、蒙版密度/羽化编译成纯数据。
- 未知混合模式、坏父子关系和恢复为空的图层文档进入诊断，不伪造 GPU 状态。
- 参数操作继续只来自 `ParameterMode`；不把色彩公式、纹理句柄或 Qt 类型写入描述。

### Task 4：无 Qt 的预览内容协议 `[x]`

- 新增 `kernel/ipc`，冻结预览请求、取消消息和预览帧头的纯标准库结构。
- 编解码带协议版本、长度上限、修订号、请求序号、宽高、行距和 RGBA 格式；截断、过长、未知版本和不合法尺寸必须拒绝。
- 协议只描述内容，不打开文件、不创建共享内存、不依赖 GPU 后端；UI/renderd/gpu 不能再各自复制字段。

### Task 5：测试、门禁和证据 `[x]`

- 为管线环路、最新值覆盖、旧修订、极速导出、预览只读、图层/蒙版映射和协议恶意输入增加内核测试。
- 将新目标加入 CMake 和架构门禁，确保 kernel 目标不链接 Qt/OpenGL。
- 更新渲染接口规范和证据，明确本阶段完成的是 kernel 内容闭环，不把 GPU 像素执行标成已完成。

## 验收条件

- 同一个文档连续提交 100 个滑杆值时，协调器只交付最后一个值，正式修订号和磁盘字节不变。
- 任何预览描述和导出描述使用同一管线排序；环路或未知依赖无法启动编译。
- 描述中能得到确定的图层顺序、透明度、混合模式和蒙版输入；非法状态只产生诊断。
- 预览协议在独立进程边界下可编解码，并拒绝截断/越界/未知版本数据。
- 全量 CTest、架构依赖门禁和 `git diff --check` 通过。

## 实现证据

- `kernel/preview`：`PreviewCoordinator` 只保留最新预览请求，覆盖旧值并生成只读快照。
- `kernel/ipc`：预览请求、取消消息和 RGBA 帧头的版本化、有长度上限的编解码。
- `kernel/render`：参数操作、图层/蒙版/选择区输入和统一管线顺序。
- 验证目录：`/private/tmp/pixcake-kernel-preview-20261105`。
- 最终结果：35/35 CTest 通过，架构依赖、能力归属和 `git diff --check` 通过。

## 明确不在本阶段

OpenGL/Qt/native 后端、`gpu/render` 的着色和帧缓冲、共享内存实现、UI 页面、真实图片解码、远端 AI、登录/账户、云服务和完整导出进程继续由各自模块计划负责。
