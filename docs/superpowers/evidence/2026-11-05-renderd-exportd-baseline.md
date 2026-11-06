# renderd / exportd 实施基线证据

日期：2026-11-05

## 代码基线

- 分支：`codex/desktop-official-layout`
- `apps/renderd`：已提供独立服务库和健康检查入口；本阶段补齐待处理请求的身份/序号取消语义。
- `apps/exportd`：已提供独立服务库和健康检查入口；本阶段补齐与 `ExportQueue` 的准备、渲染、失败和重试语义。
- 已有 `kernel/preview`、`kernel/render`、`kernel/ipc`、`kernel/exportq`
- `gpu/render` 属于并行工作范围，本阶段没有修改或扩展其实现。

## 官方手册核对

官方工作区页 `<https://www.pixcakeai.com/guide/32445>`（更新日期 2026-09-17）说明：

- 精修页有画布、缩略图区、工具栏、功能区和导出入口；预览支持单视图、对比视图和参考视图。
- 创意页有手动工具栏、画布、缩略图区、AI 工具箱、历史/帮助/反馈侧栏和图层面板。
- 导出页支持快速导出、自定义设置、排版印刷和极速导出；极速导出会禁用编辑和预览。

核对的子页：

- `<https://www.pixcakeai.com/guide/32448>` 创建项目
- `<https://www.pixcakeai.com/guide/32450>` 预览视图模式
- `<https://www.pixcakeai.com/guide/158252>` 手动工具
- `<https://www.pixcakeai.com/guide/104947>` 创意模块
- `<https://i.pixcakeai.com/guide/32483>` 导出图片设置

补充核对的桌面手册行为：

- `<https://www.pixcakeai.com/guide/230984>` AI 挑图：项目达到 150 张时可提示进入，支持场景/曝光/模糊/重复检测和分组结果；检测结果属于挑图/标记模块，不由渲染进程伪造。
- `<https://www.pixcakeai.com/guide/MlI1Jk6qm6>` AI 点修：先建立智能或快速选区，再把基础调色/美化映射到现有能力或蒙版；不新增一套像素参数协议。
- `<https://www.pixcakeai.com/guide/124480>` 预设：预设可以跨端保存/应用，类型包括色彩、AI 色彩、人像和组合；未注册能力必须按渲染契约保留并诊断。
- `<https://wiki.pixcakeai.com/guide/9418.html>` 历史记录：进程退出后历史折叠为“上一次保存”，不能把进程内撤销链当作导出快照。
- `<https://www.pixcakeai.com/guide/104947>` 创意模块：图层/蒙版/生成结果由创意文档管理，应用至精修或导出至本地是后续任务；创意页导出仍复用精修导出设置。
- `<https://i.pixcakeai.com/guide/32483>` 导出设置：快速导出、自定义格式/品质/色彩空间/水印、排版印刷和极速导出；极速导出期间编辑与预览必须停用。

本证据不把手册中的示例照片、账号数据、会员点数或截图标注线复制为运行时数据。

## Jarvis 核对

- 目标：`/Applications/pixcake-test.app`
- Jarvis app key：`Pixcake_mac`
- 版本：`9.3.0`
- executor：`0.1.586`，无更新
- Accessibility：可用；Screen Recording：可用；AXBridge：不可用
- 精修页 AX 可读到：`图库`、`精修`、`导出`、`预设`、`色彩调节`、`缩放`、`抓手`、`参考线`、`液化`、`污点修复`、`修补`、`仿制图章`、`智能消除`、`预览，水印仅在预览显示`、`单视图`、缩略图筛选/排序/去挑图和功能区入口。

Jarvis knowledge connector 在本会话未提供，因此没有把知识库内容当作事实来源；本阶段只使用 AX 观察和官方手册页面。
- 2026-11-06 复核：Jarvis executor `0.1.586` 仍为最新，目标应用为 Pixcake_mac 9.3.0；当前 AX 窗口停留在“本地项目”页，未载入具体项目，因此没有从空状态推断导出控件或编码行为。

## 明确未完成

- 真实图片文件解码和 RAW 支持
- Qt/native GPU 着色、纹理、蒙版采样和共享内存帧
- 语义分割模型
- 全分辨率编码、排版和系统打印
- 真实跨进程 IPC 传输和主进程启动/重启编排

## 第二阶段执行记录

- `renderd` 的取消接口要求客户端序号、图片身份、编辑实例和文档修订全部匹配；旧请求的取消不会删除替代它的新请求。
- `exportd` 接收 `ExportQueue` 时推进 `排队 → 准备 → 渲染`；因为解码器/编码器尚未接入，处理结果明确为 `UnsupportedFormat` 并记录失败状态，待处理工作保留到显式 `retry` 后再排队。
- 服务不写目标文件、不提交预览帧、不生成伪造像素结果。
- 验证：独立 Debug 构建成功；CTest 47/47 通过；架构依赖检查、能力归属检查和 `git diff --check` 通过。

## 下一阶段入口

- `kernel/import` 目前只负责来源格式/尺寸探测；代码树和 CMake 没有已注册的 JPEG/PNG/TIFF 解码或编码库。
- 真实解码、缩放、编码和跨进程字节协议已单独写入
  `docs/superpowers/plans/2026-11-05-renderd-exportd-next-stage.md`，先完成依赖与协议证据，再实现适配器；没有证据时不猜测库或 ABI。
- 全局依赖规则已写入
  `docs/superpowers/specs/2026-11-06-pixcake-third-party-dependency-policy.md`：通用图像 I/O 优先采用维护中的第三方库，项目只保留契约适配、边界校验和事务语义。
- 官方候选初查：OpenImageIO 官方文档说明其 ImageInput/ImageOutput 面向多格式、使用 C++17/CMake 并支持 macOS/Windows；libvips 官方项目说明其低内存、按需处理和 LGPL-2.1-or-later；LibRaw 官方文档定位为 RAW 读取库并提供 LGPL-2.1/CDDL 1.0 双许可证。三者仍需完成传递依赖、插件部署和平台验收，当前没有写入生产 CMake。
