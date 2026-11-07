# renderd / exportd 下一阶段计划：图像 I/O 与进程协议证据门

日期：2026-11-05
状态：Task 0 选型证据完成；Task 1 适配器契约初版完成；Task 2–4 待协议和真实库验证
范围：仅 `apps/renderd`、`apps/exportd` 及其明确的公共进程协议；不修改并行工作的 `gpu/render`。

## 当前证据

- `renderd` 当前服务输入是调用方已解码的 RGBA；无操作且不超过 4000 边时可返回合法预览帧，否则明确拒绝未实现的缩放或像素执行。
- `exportd` 当前服务接收不可变 `ExportJob` 快照，并通过 `ExportQueue` 管理状态；未有解码器或编码器时返回 `UnsupportedFormat`，不写目标文件。
- `kernel/import` 目前只做 PNG/JPEG/TIFF 等来源探测，没有像素解码实现；代码树和 CMake 中没有已注册的 JPEG/PNG/TIFF 解码或编码库。
- 官方手册确认预览最长边为 4000；导出使用原图分辨率，并提供格式、品质 0–14、色彩空间、水印、排版印刷等设置（<https://i.pixcakeai.com/guide/32483>）。官方页面没有规定本地进程的 IPC 字节布局或第三方库选择。
- 全局第三方依赖规范已写入 `docs/superpowers/specs/2026-11-06-pixcake-third-party-dependency-policy.md`：通用能力优先使用维护中的第三方库，手写实现必须有例外证据；库的来源、版本、许可证、平台和可复现构建必须可追溯。

Task 0 结果：项目顶层只冻结 C++17 和现有 CMake 目标；`kernel/import` 的实现是来源探测，架构门禁明确禁止内核直接包含图像库头。设计文档以 macOS 第一阶段验收、Windows/鸿蒙复用项目结构为目标。候选比较后，先锁定 libvips 8.18.7 作为 raster/RAW 适配的主候选：官方发布页标记为最新稳定版本，签名提交为 `24ad4d0`；官方构建使用 Meson 0.56+，核心依赖为 GLib/GObject/Expat，JPEG/PNG/TIFF/RAW 由可选依赖启用。它的许可证为 LGPL-2.1-or-later，必须以共享库/模块部署和许可证文本审计为前置条件。OpenImageIO 保留为备选：其当前文档要求 CMake 3.23，与本项目 3.20 基线和更重的传递依赖不匹配；LibRaw 只覆盖 RAW，不能独立满足 JPEG/PNG/TIFF 和缩略图编码。

锁定记录见 `third_party/libvips.lock.cmake`。官方源码包下载地址和版本已记录，SHA-256 由官方发布包的归档索引交叉核对；当前受限环境无法用 shell DNS 下载源码，因此在本地依赖缓存可用前不把 libvips 写入生产链接目标。

## 分步任务

### Task 0 — 依赖与平台证据（先做）

- 盘点项目支持的平台、现有构建工具链和可接受的系统图像 API。
- 按全局依赖规范比较维护中的第三方库；优先复用库，不手写通用解码/编码算法。
- 记录候选的官方来源、锁定版本、许可证、传递依赖、插件部署、平台和资源上限；没有证据时不引入新的依赖。
- 用小型 fixture 验证来源探测结果与实际解码目标之间的边界，不能把探测成功当成解码成功。

### Task 1 — 图像 I/O 适配器契约

- 冻结第三方库适配层 `ImageDecoder`、预览缩放和 `ExportEncoder` 的输入/输出、错误码、最大尺寸和资源释放语义；适配层不复制库的通用算法。契约先落在 `apps/renderd`、`apps/exportd`，不向 `kernel/` 暴露图像库头。
- 解码结果必须进入现有 `RgbaInput`；缩略图和预览沿用 4000 最长边限制；导出不得复用预览尺寸。
- 编码器输出必须通过 `ExportQueue` 的临时文件和提交事务，禁止服务直接写目标文件。

执行结果：`apps/renderd/image_io.hpp` 和 `apps/exportd/image_io.hpp` 已冻结纯项目接口；实现了输入/输出缓冲区校验、尺寸/像素数/字节数限制、0–14 质量范围、`UnsupportedFormat`/`ImageCorrupt`/`IoFailed` 映射和第三方对象不得跨调用保留的所有权要求。新增 `test_renderd_image_io`、`test_exportd_image_io` 已通过。真实 libvips 对接仍留在 Task 3，不能把契约测试当作真实解码/编码完成。

### Task 2 — 跨进程协议契约

- 在已有 `kernel/ipc` 帧头和取消消息基础上，明确版本、长度上限、截断/未知字段错误和进程退出语义。
- `renderd` 保持最新请求替换；`exportd` 保持任务不丢、失败可恢复。
- 协议未冻结前不实现自定义二进制布局，也不把内存对象直接当作跨进程 ABI。

### Task 3 — 真实适配器与集成

- 先实现已被依赖证据覆盖的格式和平台路径，再逐项接入 JPEG/PNG/TIFF/RAW 等官方行为范围。
- 为解码失败、尺寸超限、编码失败、磁盘失败和取消分别添加可重复测试。
- 每一步都保留明确的 `UnsupportedFormat` 或 `ImageCorrupt`，不能用空帧、原图伪装效果或假导出文件。

### Task 4 — 验证与发布门禁

- 每个适配器阶段运行独立构建、目标测试、完整 CTest、架构依赖和能力归属门禁。
- 使用 Jarvis/官方手册复核 UI 可见行为（预览限制、导出设置、失败/重试反馈）后，再把行为写入服务契约。

## 进展（2026-11-06）

- Task 0：用户裁决解码用 macOS ImageIO，不用 libvips；证据 `docs/superpowers/evidence/2026-11-06-renderd-transport-and-decoder.md` §2。
- Task 2：用户裁决 Unix 套接字 + 共享内存；协议见同一证据 §3。
- Task 3（renderd 部分）：ImageIO 解码、预览源/结果消息、传输层、`PreviewServer` 与 `--socket` 已实现并测试（同一证据 §4）。exportd 的全分辨率解码与编码、壳层启动 renderd 和显示帧仍未做。
- 后续（2026-11-06）：壳层启动 renderd 并显示帧已在壳层任务里完成；renderd 预览改为 GPU 优先。
- Task 3（exportd 部分，2026-11-06）：ImageIO 全分辨率解码（含原图 ICC）、GPU 分块绘制（失败回退参考）、ImageIO 编码 JPG/TIFF（品质 0–14、色彩空间原始/sRGB）、经 `ExportQueue` 提交产品已实现并测试；证据 `docs/superpowers/evidence/2026-11-06-exportd-full-resolution.md`。壳层导出弹窗与到 exportd 的进程通信、蒙版、水印、原始格式仍未做。
- 壳层导出（2026-11-07）：导出弹窗（快速导出 / 自定设置；未接入项置灰带原因）、壳层↔exportd 进程通信（`ExportStartMessage`、进度带原因）、导出列表（状态、失败原因、重试、取消）、原始格式与 TIFF16 已实现并测试；证据 `docs/superpowers/evidence/2026-11-07-shell-export-flow.md`。仍未做：蒙版、水印、文件大小限制、排版印刷、扣张数、.app 打包 exportd。跨卷（外接盘）导出已于同日修复，见同一证据末节。

## 启动条件

Task 0 和 Task 2 的依赖/协议证据未完成前，不开始真实文件解码、编码或跨进程传输实现。这样可以避免根据手册截图猜测库、格式或 IPC 字节布局。
