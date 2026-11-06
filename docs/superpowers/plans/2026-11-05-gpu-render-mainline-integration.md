# gpu/render 主线接口对齐与逐步合并计划

日期：2026-11-05
基线：`/Users/lifangzai/PixCake-MVP` 当前工作区
来源：项目架构约束、RenderDescription 接口冻结证据、renderd/exportd 基线证据、已有色彩调节像素管线计划、官方 PixCake 色彩调节说明

## 当前判断

主项目当前工作区已经包含参数模式、`kernel/render`、预览/IPC、`apps/renderd` 和 `apps/exportd` 的较新实现；渲染 worktree 基于较早快照，因此不能直接把 worktree 的整个目录覆盖到主项目。

渲染 worktree 已完成并提交：

- `7f57722`：pass registry、reference-only/缺资源/未注册状态与输入诊断
- `beca2f4`：显式资源 provider 和资源句柄诊断
- `3cfe271`：结构化 3D LUT 的 reference 三线性采样、amount、蒙版和 revision 校验
- `d518c0e`：图层/蒙版 reference 合成和 GPU fallback 诊断
- `81a63c6`：图层 normal/multiply/screen、透明度和顺序测试

主项目当前 GPU skeleton 与这些提交的 API 不一致：主项目服务仍调用 `RenderExecutor::validate()`，渲染 worktree 的 executor 已转为 `renderReference()` / `execute()`。合并必须做 API 适配，不能只 cherry-pick 文件。

## 不可违反的约束

1. 主项目当前未提交的 kernel、services、UI 和文档改动视为外部工作区状态；迁移 GPU 时不得覆盖或丢弃。
2. 不修改 `kernel/capability`、`kernel/render` 的参数范围、渲染字段和排序规则来迁就 GPU 实现。
3. 不在 `gpu/render` 复制参数模式、业务范围、滤镜名称、CUBE/XMP 解析规则或 UI 字段。
4. 预览和导出必须共用同一个 executor/pass registry；没有真实解码器、编码器、IPC 或 GPU 资源时必须返回明确诊断。
5. 官方手册只作为用户可见行为证据；内部公式、LUT 内容和第三方库选择没有来源时保持 reference-only/UnsupportedFormat。
6. 每一阶段先建立独立构建和测试证据，再进入下一阶段；失败时保留失败原因，不用桩结果冒充像素完成。

## 阶段 0：主线基线冻结（先执行）

- 记录主项目当前 `git status`、HEAD、未跟踪目录和 CMake 目标清单。
- 在临时构建目录编译 `kernel/render`、`kernel/preview`、`kernel/ipc`、`apps/renderd`、`apps/exportd` 及其测试。
- 记录主线现有 GPU skeleton 的公共 API和服务调用点。
- 只读检查通过后，建立迁移文件清单；不改主项目工作区。

通过条件：主线接口可以独立编译，或者明确列出当前阻塞的目标和原因。

初查结果：主项目当前工作区的 `test_capability_registry`、`test_render_description`、`test_preview_coordinator`、`test_preview_protocol`、`test_renderd`、`test_exportd`、`test_gpu_facade`、`test_gpu_backend_qt` 均已在 `/private/tmp/pixcake-main-integration-base` 构建并通过。主线接口本身可作为迁移基线；下一步是文件级 GPU 迁移和服务 API 适配。

## 阶段 1：GPU facade/backend 迁移

- 仅迁移渲染 worktree 中已验证的 facade、Qt backend 和 GPU render 文件。
- 保留主项目 CMake 目标名称、Qt 链接边界和架构门禁；不把 Qt 头带入 `gpu/render` 调用点。
- 合并 `RenderResult`、`PassRegistry`、输入帧校验和显式 resource provider。
- 对主项目已有 `RenderExecutor::validate()` 调用，增加与新 executor 兼容的结构校验入口，或逐个适配服务；选择以现有接口证据和测试结果为准，不重复参数校验。

验证：GPU facade/backend/reference 定向测试、`git diff --check`、架构依赖检查。

## 阶段 2：renderd 预览接入

- 将 `RgbaInput` 映射为 executor 的 `RgbaImage`，保留输入行距、尺寸、图片身份、编辑实例和 revision 校验。
- `render_latest()` 使用同一个 executor；GPU context 可用时执行 GPU，不能用时只能走明确的 reference fallback 或返回可诊断错误。
- 保留最新请求替换、取消身份匹配、最长边 4000 限制和旧 revision 丢弃规则。
- 预览结果头必须来自实际输出帧，不复制 UI 参数或另建效果公式。

验证：无操作帧、白平衡/影调 reference 帧、蒙版/图层 fallback、旧请求取消、revision 冲突、GPU 不可用诊断。

当前进度：已在 staging 中完成 `RgbaInput → gpu::RgbaImage → RenderExecutor::renderReference → PreviewFrame` 的最小闭环；`test_renderd` 已通过。颜色模式 `ColorOnly` 的业务语义尚未在接口证据中定义，暂不自行改变操作集合。

阶段 0–2 staging 验证：Debug 全量构建成功，CTest **48/48 通过**；健康检查入口 `pixcake_app_renderd --health` 与 `pixcake_app_exportd --health` 均成功。迁移文件暂存包：`/private/tmp/pixcake-gpu-mainline-stage1-2.tar.gz`。

## 阶段 3：exportd 任务接入

- 将任务快照中的原图 RGBA 输入接入同一个 executor；导出不读取预览帧、不接受滑杆对象。
- 保留 `ExportQueue` 的任务身份、状态、取消、重试、临时文件和提交边界。
- 在没有已证实的图像解码/编码适配器前，继续返回 `UnsupportedFormat`，不生成空文件或原图伪装成处理结果。
- 只有在输入/输出适配器契约和依赖证据成立后，才实现真实全分辨率编码。

验证：快照 revision、旧任务拒绝、取消/重试、失败状态持久化，以及同一快照与 preview 使用相同 executor 的测试。

当前进度：主线 `exportd` 已能通过兼容的 `RenderExecutor::validate()` 编译，但 `ExportWork` 尚未携带已解码 RGBA 像素，项目也没有已注册的编码器。按照现有 `renderd/exportd` 下一阶段计划，本阶段停在 I/O 与协议证据门之前，不生成伪造导出文件。

## 阶段 4：真实 GPU 像素覆盖

- 保留已验证的 `color.global` temperature/tint/exposure/contrast shader。
- 结构化 LUT 先保持 reference-only；只有 facade 提供 3D texture/多纹理绑定和真实 readback 证据后，才加入 GPU LUT pass。
- 局部调色、蒙版和图层必须使用显式像素资源；没有多纹理/多层编排接口时保持 reference fallback。
- 不把官方滤镜、profile、RAW 预设或创意色彩内容写死到代码中。

验证：有 GPU 的环境使用 readback 像素断言；无 GPU 的环境只通过 reference 测试和明确 skip。

## 阶段 5：全量验收

- 完整 CMake 构建、CTest、架构依赖门禁、能力归属门禁和 `git diff --check`。
- 检查预览/导出共用 executor、旧 revision 不覆盖新帧、失败不写目标文件。
- 复核官方行为证据：色彩调节能力来自官方说明，进程协议和内部公式不从页面截图推断。
- 只有所有阶段的证据齐全，才把 `gpu/render` 标记为阶段完成；否则保留未完成清单和阻塞原因。

## 当前明确未完成

- 主项目 API 与渲染 worktree API 的迁移适配。
- renderd/exportd 实际调用 executor 的像素路径。
- 真实 GPU LUT、局部调色、蒙版/图层 shader 合成。
- 真实图像解码、缩放、编码和跨进程字节传输。

