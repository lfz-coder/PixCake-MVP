# renderd 解码库与进程通信方案

日期：2026-11-06  
对应计划：`docs/superpowers/plans/2026-11-05-renderd-exportd-next-stage.md` 的 Task 0（依赖证据）与 Task 2（跨进程协议）。这两项是该计划“启动条件”，本文件写完即解除对真实解码和跨进程传输的阻塞。

## 1. 裁决（用户，2026-11-06）

| 问题 | 裁决 | 原因 |
|---|---|---|
| 解码库 | macOS ImageIO（系统框架），不用 libvips | 项目只需 macOS；系统框架没有新依赖、无需锁定版本与许可证审计；测试已用 ImageIO 读样张 |
| 进程通信 | 本机 Unix 套接字传消息，像素走共享内存 | 消息沿用 `kernel/ipc` 记录编码；预览帧最大 4000×4000×4 = 64 MB，不经套接字拷贝 |

以后需要其他平台时，在同一个 `ImageDecoder` 适配层后面增加实现，接口不变。

## 2. 解码（Task 0）

- 位置：系统框架只能出现在 `platform/`（架构规则 19）。`platform/macos` 新增 `pixcake_platform_macos_image`，只暴露项目类型；`apps/renderd` 的 `MacImageDecoder` 实现已冻结的 `ImageDecoder`。
- API：`CGImageSourceCreateWithURL` → 先读 `kCGImagePropertyPixelWidth/Height`，按 `DecodeLimits` 在分配前拒绝 → `CGImageSourceCreateThumbnailAtIndex`（`FromImageAlways`、`WithTransform` 应用 EXIF 方向、`ThumbnailMaxPixelSize` = 预览最长边）→ 画进 sRGB、8 位 RGBA 位图。
- 预览缩放由 ImageIO 在解码时完成，因此“预览帧超过 4000 时缩放后端未注册”的限制由解码路径解除；`DecodeRequest` 增加 `max_edge`（0 表示全分辨率，导出用）。
- 像素格式：CoreGraphics 的 8 位 RGBA 位图只支持预乘 alpha。照片不透明时与非预乘相同；带透明的 PNG 得到预乘值，并在结果里标明。
- 格式识别：按 ImageIO 报告的类型标识（`public.jpeg`、`public.png`、`public.tiff`、`com.microsoft.bmp`、RAW 类）映射到 `SourceFormat`；其他类型返回 `UnsupportedFormat`，打不开的文件返回 `ImageCorrupt`，文件不存在返回 `SourceMissing`。
- 所有权：返回的缓冲区归调用方所有；CF 对象在调用内全部释放，不保留路径。

## 3. 协议（Task 2）

### 3.1 连接

- 壳层启动 `pixcake_app_renderd --socket <路径>`；renderd 在该路径监听，只接受同一用户（`getpeereid`）的连接，一次一个连接。
- 每条消息在套接字上是 `u32 小端长度 + 消息字节`，消息字节就是 `kernel/ipc` 的记录编码（信封里有消息种类和协议版本）。长度超过该种类的上限时断开连接，不尝试读一半。
- 帧像素：renderd 用 `shm_open` 建一块共享内存，写入后立刻 `shm_unlink`，把文件描述符随结果消息用 `SCM_RIGHTS` 传给壳层。名字不留在系统里，任一方崩溃都不会泄漏。

### 3.2 消息（种类编号与已有的 1–7 共用一个空间）

| 种类 | 方向 | 内容 | 上限 |
|---|---|---|---|
| 1 预览请求（已有） | 壳层 → renderd | 序号、图片/编辑实例、基础修订、能力/参数/显示值、模式、最长边 | 16 KB |
| 2 预览取消（已有） | 壳层 → renderd | 序号与身份 | 16 KB |
| 8 预览源 | 壳层 → renderd | 图片编号、编辑实例、文档修订、原图路径（壳层已解析书签）、已提交文档字节（`EditDocument::encode`） | 8 MB |
| 9 预览结果 | renderd → 壳层 | 序号、结果码、帧头（种类 3 的字段）、诊断文字；成功时附带共享内存描述符 | 64 KB |

- renderd 收到预览源后解码一次并保留文档；之后的预览请求只带滑杆值，renderd 在文档副本上 `set_preview` 再 `render::compile`。修订号或身份对不上返回状态冲突。
- 最新请求优先：renderd 每次出图前先把套接字里已到达的消息全部读完，只渲染最后一个请求；被替换的请求不回结果（壳层按序号丢弃旧帧）。
- 渲染进行中不能被打断；取消只作用于尚未开始的请求。
- 版本：读方遇到更高协议版本返回 `CapabilityTooNew`；未知字段忽略（沿用 `message_codec`）。

## 4. 实现与验证（2026-11-06）

| 部分 | 位置 | 测试 |
|---|---|---|
| ImageIO 解码 | `platform/macos`：`mac_decode_image`（目标 `pixcake_platform_macos_image`） | `test_mac_image_decoder`：JPEG/PNG/TIFF、EXIF 方向 6（300×200 存储 → 200×300 显示）、预览缩到最长边且不放大、带透明 PNG 预乘、分配前按存储尺寸拒绝、文件缺失、损坏、非图片、截断 |
| `ImageDecoder` 实现 | `apps/renderd`：`make_system_image_decoder()`；`DecodeRequest` 增加 `max_edge` | `test_renderd_image_io`：手写 BMP 解码、缩放、内容与期望格式不符、超限、文件缺失 |
| 消息 8/9 | `kernel/ipc`：`encode/decode_preview_source`、`encode/decode_preview_result`、`peek_preview_message_kind`；编码器增加二进制字段 | `test_preview_protocol`：往返、含 NUL 的文档、1 MB 文档、超限、结果头与序号一致、诊断不得含换行、失败结果不带帧头 |
| 传输 | `services/preview_channel`：`Channel`（分帧 + `SCM_RIGHTS`）、`SharedFrame`、`listen_at`/`accept_same_user`/`connect_to`；`PreviewClient` | `test_preview_channel`：往返、3 MB 消息、描述符与共享内存、超限断开、超时、对端消失（不触发 SIGPIPE）、陈旧套接字文件、拒绝替换普通文件 |
| renderd 循环 | `apps/renderd`：`PreviewServer`；`pixcake_app_renderd --socket <路径>` | `test_preview_server`（同进程套接字对）：先请求后源、源 + 请求出帧、三次滑动只渲染最后一次、取消、同源只解码一次、改最长边重新解码、修订号冲突、文档损坏、身份不符、原图被删；`test_renderd_process`：真实启动进程、跨进程收帧（+1 EV 提亮）、断开后退出码 0 且套接字文件删除 |

实现中确认的两个平台细节：

- macOS arm64 内存页是 16 KB，共享内存对象的大小按整页向上取整；接收方只检查“不小于帧头所需字节”。
- 对端关闭后 macOS 拒绝 `setsockopt`，所以 `SO_NOSIGPIPE` 必须在建立 `Channel` 时设置，否则向已关闭的对端写入会让进程收到 SIGPIPE 退出。

预览源在到达时记录原图的大小和修改时间；同一路径、大小和时间不变时复用已解码像素，否则重新解码；文件不在时该源的请求返回 `SourceMissing`。

renderd 每个进程只服务一个连接：30 秒内没有壳层连接退出码 4，套接字打不开退出码 5，通道损坏退出码 6，正常断开退出码 0。

## 5. 不在本次范围

- 壳层画布显示帧、打开照片流程、局部调色的蒙版选择（能力片段写入 `semantic:` 编号要改能力注册，等另一个会话的 Free 分组参数注册结束再做）。
- renderd 崩溃后的自动重启与超时：壳层进程管理负责，另行实现。
