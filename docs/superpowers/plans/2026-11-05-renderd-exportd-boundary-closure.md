# renderd / exportd 边界闭环实施计划

日期：2026-11-05
状态：第一阶段已完成（边界闭环；像素执行仍延期）
依据：`docs/superpowers/specs/2026-11-05-pixcake-render-contract.md`、
`docs/superpowers/specs/2026-11-02-pixcake-kernel-architecture-design.md`、
`docs/superpowers/plans/2026-11-05-kernel-preview-pipeline-closure.md`

## 当前事实

- `apps/renderd` 和 `apps/exportd` 在当前源码树中不存在。
- `kernel/preview`、`kernel/render`、`kernel/ipc` 已提供预览请求合并、只读绘制描述和预览帧头协议。
- `kernel/exportq` 已提供导出任务快照、极速导出门闩和事件状态模型，但没有导出进程入口。
- `gpu/render` 当前只有接受回调 pass 的骨架，尚未消费 `RenderDescription`，没有像素着色、纹理上传、帧缓冲或编码实现。
- `gpu/backend/qt` 只冻结了离屏 Qt OpenGL 上下文创建入口；测试允许后端不可用，不能把上下文创建当成像素执行完成。
- 当前工作区包含其他 worktree/并行模块的未提交修改，本计划只新增/修改 renderd、exportd、gpu/render、构建门禁和对应证据/测试，不重置或覆盖已有修改。

## 官方行为边界

官方桌面版手册 `<https://www.pixcakeai.com/guide/32445>` 及其桌面版子页明确：

- 精修页画布用于预览效果，缩略图区支持筛选、排序、去挑图和单视图/对比视图/参考视图。
- 创意页画布显示图片与图层，支持拖入图片作为图层；AI 工具、历史记录和图层面板属于创意工作区。
- 导出页提供快速导出、自定义设置和排版印刷；自定义设置包含格式、品质 0–14、色彩空间、水印等；极速导出会禁用编辑和预览。
- 预览不计费；全分辨率导出使用导出任务的快照。预览最长边为 4000，导出使用原图分辨率。

本阶段只实现进程边界、请求替换、尺寸/协议/描述验证和可验证的已解码 RGBA 原图帧路径。图片文件解码、真实 OpenGL pass、语义分割、全分辨率编码、排版和打印必须在后续阶段按冻结接口接入；不能用占位像素或静态页面声称完成。

## 分阶段任务

### Task 0 — 证据和基线（完成）

- 记录分支、工作区状态、Jarvis executor/AX 状态和官方手册事实。
- 用 Jarvis 读取 `/Applications/pixcake-test.app`（Pixcake_mac 9.3.0）精修页 AX，确认工具栏、图库/精修切换、导出入口、预览水印提示、缩略图区和功能面板。
- 记录官方页面和子页链接，标明本阶段没有从截图猜测色彩公式、编码格式或未冻结控件行为。

### Task 1 — `gpu/render` 描述消费契约

- 增加只读 `RenderDescription` 校验：能力顺序单调、阻塞诊断拒绝、参数值有限、图层透明度/蒙版范围和选择区羽化合法。
- 保留现有回调 pass API，增加消费描述的明确入口；不在 `gpu/render` 中包含 Qt、能力私有头、文件读取或业务公式。
- 返回 `ResultCode`/错误文本，避免空帧被当成成功。

### Task 2 — `apps/renderd` 预览/缩略图/画布入口

- 新增独立可执行目标和无 Qt 的服务类。
- 请求绑定 `image_id`、`editing_instance_id`、`document_revision`、序号和最长边；新请求替换旧请求。
- 接收调用方已解码的 RGBA 缓冲，验证宽高、行距、字节数和最长边，生成带协议帧头的原图/缩略图结果。
- 仅当绘制描述没有未实现的绘制操作时允许原图帧；存在尚未注册的像素程序时返回 `UnsupportedFormat` 和诊断，不返回伪造效果。
- 进程入口只提供健康检查/自测模式，不接收主进程用户命令，不写项目库。

### Task 3 — `apps/exportd` 全分辨率导出入口

- 新增独立可执行目标和无 Qt 的服务类。
- 接收 `ExportJob` 的不可变文档快照、原图身份、目标尺寸和导出设置，校验任务快照与描述身份一致。
- 复用 `gpu/render` 的描述校验和同一管线顺序；不读取预览帧，不把预览最长边带入导出。
- 未接入真实解码/编码器、排版和打印时返回明确 `UnsupportedFormat`/`StateConflict`，保留队列任务可重试，不写半成品。
- 不绕过 `kernel/exportq` 的极速导出门闩和任务状态。

### Task 4 — 构建、门禁和测试

- 将两个进程加入 CMake，确保 `gpu/render`、`apps/renderd`、`apps/exportd` 不链接 Qt Widgets。
- 增加描述校验、最新请求替换、RGBA 尺寸边界、旧修订拒绝、导出快照身份和未实现 pass 拒绝测试。
- 更新架构门禁扫描 renderd/exportd 的 Qt/文件写入边界。
- 运行独立构建、CTest、架构门禁、能力归属检查和 `git diff --check`。

## 完成定义

本阶段完成的含义是：两个独立进程入口和服务边界可构建、可测试、能拒绝非法/未实现的像素操作，并能对已解码无操作 RGBA 输入生成合法预览帧元数据；不是完整的真实图片渲染或导出。下一阶段必须补上真实图片解码、Qt/native GPU pass、共享帧传输和全分辨率编码，且继续复用本阶段接口。

## 证据

- 官方手册：<https://www.pixcakeai.com/guide/32445>
- 创建项目：<https://www.pixcakeai.com/guide/32448>
- 预览视图：<https://www.pixcakeai.com/guide/32450>
- 手动工具：<https://www.pixcakeai.com/guide/158252>
- 创意模块：<https://www.pixcakeai.com/guide/104947>
- 导出设置：<https://i.pixcakeai.com/guide/32483>
- Jarvis：`/Applications/pixcake-test.app`，`Pixcake_mac`，版本 `9.3.0`，executor `0.1.586`，Accessibility/Screen Recording 可用，AXBridge 不可用。

## 第一阶段完成记录

- `gpu/render` 已增加 `RenderDescription` 校验和描述驱动入口；校验管线顺序、阻塞诊断、参数有限值、图层父子/混合/范围及选择区羽化。
- `apps/renderd` 已提供独立服务库与 `pixcake_app_renderd --health`；只保留最新预览请求，能够对已解码且不超过 4000 边的无操作 RGBA 输入生成合法帧头和像素副本；有未注册像素操作或需要缩放时明确返回 `UnsupportedFormat`。
- `apps/exportd` 已提供独立服务库与 `pixcake_app_exportd --health`；校验导出任务快照、原图身份、修订号和全分辨率几何，复用同一绘制描述校验；未接入解码/编码器时保持任务可重试并返回 `UnsupportedFormat`，不写半成品。
- CMake 已加入两个独立进程和服务库；架构门禁新增 renderd/exportd 的 Qt/OpenGL 依赖扫描。
- 验证：独立 Debug 构建成功，`ctest` 46/46 通过，`arch_dependencies`、`capability_ownership` 和 `git diff --check` 通过；两个进程的 `--health` 自检成功。

## 下一阶段入口

1. 把真实图片解码器输出接入 `RgbaInput`，先覆盖 JPEG/PNG，再按官方 RAW 支持范围补格式探针。
2. 在 `gpu/backend/qt` 后面实现同名纹理/帧缓冲/程序对象，并为已冻结的 `program` 注册真实 pass；`gpu/render` 不新增业务公式。
3. 设计并测试跨进程的绘制描述/帧传输和共享帧生命周期；保持“最新预览替换、导出任务不丢”的并发语义。
4. 在 `exportd` 接入全分辨率编码、质量 0–14、色彩空间、水印、排版和打印提交，所有写入继续经过 `kernel/exportq` 的临时文件/事务恢复路径。
