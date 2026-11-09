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

## 5. 壳层接入（2026-11-06）

| 部分 | 位置 | 说明 |
|---|---|---|
| 预览会话 | `services/preview_channel`：`RenderdProcess`、`PreviewSession`（无 Qt） | 启动 renderd 并连接；照片的精修文档放在文档目录，按照片路径取稳定编号，重新打开时接着上次已提交的编辑；拖动走命令入口的 `preview`，松手走 `commit`（架构 6.2），提交后重发预览源；旧帧丢弃 |
| 页面动作 | `apps/shell/page_kit.hpp` 的 `PageActions` | `has_photo`、`parameter_value`、`request_parameter`、`watch_preview`；默认实现什么都不做，没打开照片时页面不变 |
| 控制器 | `ShellController` | 持有会话和 `QSocketNotifier`；帧转成 `QImage`，算直方图，交给画布 |
| 组件 | `PixCakeImageView`（画布，等比缩放）、`PixCakeHistogram`（RGB 三条折线） | 按用户要求先用组件库：QFrame + QPainter 组合，没有用 OpenGL；直方图统计在 `ui/panels::rgb_histogram`（无 Qt，有单测） |
| 精修页 | `refine_page.cpp` | 打开照片时，色彩面板顶层（白平衡、影调）的 14 支滑杆可用，位置取自文档当前值；子分组和其他能力没有像素实现，保持禁用。功能区放进 `QScrollArea`（原先所有面板挤在一列被压扁，看不到滑杆），打开照片时滚到色彩面板 |
| 入口 | `pixcake_app_shell --open <照片>`（仅开发构建，和 `--page` 同属 `PIXCAKE_DEV_PAGES`） | 正式入口（图库导入 → 精修）还没有，接上后复用同一个会话 |

验证：`test_preview_session`（真实 renderd：拖动、提交、第二个参数保留第一个、越界值提交被拒、重新打开读回、进程随会话退出）；`test_shell_app_open_photo`（离屏启动壳层打开 898×1056 PNG：首帧上画布、直方图有数据、只有 14 支顶层色彩滑杆可用、曝光 +1 EV 后红色均值 133 → 229）。

实现中修掉的问题：

- 对 `CapabilityRegistry::phase_one().descriptors()` 直接写范围 for，遍历的是已析构临时对象的成员，版本上限变成空表，提交一律状态冲突。先存进局部变量。
- `EditDocument` 的编辑实例编号不在编码字节里（加载后是文档路径，解码后退回图片编号），renderd 改为以预览源消息为准，只核对文档里的图片编号和修订号。
- 显示值要按参数模式的步进格式（曝光步进 0.05 → `1.00`）；壳层由滑杆规格的小数位格式化。

### 5.1 正式入口：项目流程（用户 2026-11-06 选择“开发登录 + 完整流程”）

登录要账号服务的后端接口，仓库里没有；项目、导入命令在未登录时一律拒绝（S-25）。因此只在开发构建（`PIXCAKE_DEV_PAGES`，与 `--page` 同一开关）加 `--dev-sign-in`，让会话视为已登录；发布构建不认这个参数（已验证：`Unknown option 'dev-sign-in'`）。

| 部分 | 位置 | 说明 |
|---|---|---|
| 项目会话 | `services/project_session`：`ProjectSession`（无 Qt） | 打开 `<数据目录>/projects.sqlite`；`import_new_project` 按需求 5.2 命名（第一个文件夹名，否则第一张图片去扩展名），文件夹展开为其中的 jpg/jpeg/png/tif/tiff/bmp（不递归、跳过隐藏文件、按名排序），每个文件经 `MacSourceAccess::create_bookmark` 取书签，经 `BusinessCommands` 创建项目和导入；逐文件失败原因（O-14）；`resolve_photo` 经书签取原图路径，挪走或删除时按项目库规则标“原文件缺失”且不打开；`mark_edited` |
| 预览会话 | `PreviewSessionOptions::document_id`、`on_committed` | 项目里的照片文档名为 `photo-<编号>.pxedit`，放在 `<数据目录>/documents`；提交成功后把照片标成“已编辑” |
| 控制器 | `ShellController` | 导入：主页新建项目并进入精修打开第一张（需求 5.3）；在项目内追加。主页项目列表单击选中、双击进入项目；图库双击照片进入图库单视图；回主页关闭照片（renderd 退出）。导入和打开失败的原因作为页面提示 |
| 主页 | `home_page.cpp` | 导入区（`PixCakeDropPanel`）点击选图片、拖入文件或文件夹；“导入文件夹”按钮；项目列表来自项目库 |
| 图库 | `library_page.cpp` | `QListWidget` 图标模式列出当前项目照片（文件名、异常原图状态、已编辑），双击进精修；“＋ 导入照片”追加 |
| 主题 | `pixcake_theme.hpp` | 照片列表与滚动条的深色样式 |
| 开发参数 | `--dev-sign-in`、`--data-dir <目录>`、`--import <路径>`（可多次） | 冒烟检查只用 `--data-dir` 给的目录，不碰用户的项目库 |

验证：`test_project_session`（命名、展开、坏文件原因、未登录不建项目、追加、书签解析、挪走后标缺失、已编辑、重开数据库）；`test_shell_app_import_flow`（离屏：开发登录 → 主页导入文件夹 “Shoot 01” → 精修出帧 → 曝光 +1 EV 提交 → 照片标“已编辑” → 图库列出 → 回主页看到项目 → 再次打开项目，曝光仍在（红色均值 229）；数据库和 `photo-*.pxedit` 写在 `--data-dir` 里）。

已知不足（缩略图见 §5.2、拖到应用图标见 §5.3，均已补上）：`resolve_photo` 遇到异常时刷新整个项目库的原图状态，照片很多时会慢。

### 5.2 缩略图（2026-11-06）

| 部分 | 位置 | 说明 |
|---|---|---|
| 消息 10/11 | `kernel/ipc`：`ThumbnailRequestMessage`、`encode/decode_thumbnail_request`、`encode/decode_thumbnail_result` | 请求带请求号、图片编号、原图路径、最长边（≤ 1024）和可选的文档字节（空 = 原图）；结果字段同预览结果，种类不同，二者不能互相解码 |
| renderd | `PreviewServer` 缩略图队列；`RenderService::render_now` | 预览仍是“最新请求优先”，缩略图排队且每个都回结果；每一轮先出预览帧，再出至多一张缩略图，所以批量缩略图不会挡住滑杆预览；队列不空时循环不阻塞等待 |
| 服务 | `services/preview_channel`：`ThumbnailService` | 进入项目时单独启动一个 renderd（与精修预览的 renderd 并行）；同一照片重新请求时旧结果作废；离开项目时随连接退出 |
| 文档字节 | `ProjectSession::document_bytes` | 经 `EditDocument::load`（含备份恢复）读出已提交文档再编码；从未编辑过的照片发原图 |
| 版本上限表 | `kernel/editdoc`：`phase_one_version_limits()` | 预览会话和项目会话共用，不各自拼 |
| 壳层 | `ShellController`、`page_kit` 的 `photoList`/`thumbnailStrip` | 缓存每张照片的缩略图（320 像素），提交编辑后重新生成；图库网格（`QListWidget` 图标模式）和精修、图库底部的缩略图条显示，生成后自动补图；缩略图条单击切换照片，“＋”可导入 |

验证：`test_preview_protocol`（10/11 往返、上限、不能互相解码）；`test_preview_server`（三张缩略图排队时来了预览请求：先回预览，再逐张回缩略图，全部回完后空闲）；`test_thumbnail_service`（真实 renderd：原图与带 +1 EV 文档的缩略图、同一照片重新请求后旧结果不出现、原图缺失返回 `SourceMissing`）；`test_shell_app_import_flow`（两张照片都有缩略图，编辑过的那张红色均值 229、原图 133，精修底部缩略图条列出两张）。截图确认图库网格、状态行与底部缩略图条。

另外修了 `tests/apps/check_shell_single_instance.sh`：去掉 `--page home`（发布构建按 S-25 拒绝它，导致 T21 在 Release 下必然失败）；开发与发布构建都通过。

### 5.3 拖到应用图标与进入项目（2026-11-06）

| 部分 | 位置 | 说明 |
|---|---|---|
| 规则 | `ui/shell`：`icon_drop_action` | 需求 3.2：模态层开着不导入（B-71）；未登录不导入；在项目里（精修，以及同属项目的图库、创意）追加；主页新建项目 |
| 事件 | `ShellWindow` 的应用级事件过滤 | macOS 把拖到程序坞图标、访达里的 `.app` 和“打开方式”的文件都作为 `QFileOpenEvent` 交给应用；一次拖多个文件是多个事件，收集 150 毫秒后作为一批导入（一个项目，按第一个文件命名）；拖入那一刻有模态窗口就丢弃 |
| 文档类型 | `apps/shell/Info.plist.in` | CMake 默认模板加 `CFBundleDocumentTypes`：`public.image` 与 `public.folder`，`LSHandlerRank` 为 `Alternate`（接受拖入，但不成为图片的默认打开程序）；没有它程序坞不接受拖放 |
| 进入项目 | `ShellDisplay::open_project(landing)`、`ShellController::request_open_project` | 按像素蛋糕-test 实测（见 2026-11-06-project-entry-observation.md）：主页单击选中、双击打开，回到上次所在的工作区；图库双击照片是图库单视图。导入新建的项目进精修（需求 5.3） |

验证：`test_shell_display`（四种去向、进入项目只能落在图库或精修）；`test_shell_app_import_flow`（进入项目的流程见实测证据；三次 `QFileOpenEvent`：精修追加 1 张不新建项目，模态窗口下不导入且页面不变，主页新建项目 “Drop Shoot” 并进精修）。真实系统路径：`open -a pixcake_app_shell.app "Dock One.png" --args --dev-sign-in …` 冷启动后新建项目 “Dock One”；应用停在精修时再 `open -a … dock-two.png`，系统把文件交给正在运行的实例，追加进同一项目（项目库里 1 个项目 2 张照片），截图确认底部缩略图条两张。拖到程序坞图标本身需要真实指针拖放，仍按测试清单第 34 行作人工步骤，还没有记录。

顺带修了 T21 脚本的泄漏：`third=$(start_primary third)` 在子 shell 里记录进程号，清理时杀不到第三个主实例，每跑一次留下一个离屏壳层进程（此前累积 82 个，已清掉）；现在三种构建全量跑完不留进程。

## 6. 不在本次范围

- 壳层画布显示帧、打开照片流程、局部调色的蒙版选择（能力片段写入 `semantic:` 编号要改能力注册，等另一个会话的 Free 分组参数注册结束再做）。
- renderd 崩溃后的自动重启与超时：壳层进程管理负责，另行实现。
