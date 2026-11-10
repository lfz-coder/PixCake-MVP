# 云空间目录服务契约

日期：2026-11-09
范围：阶段 1 的账号绑定、空间目录与远程项目摘要，阶段 2A 的 HTTP 网关编排，以及阶段 2B1 的 API v1 codec / HTTPS 实现。属于 PixCake-specific 业务模块。后续账号扩展遵守 [AUTH-01–07](../plans/2026-11-10-cloud-auth.md)，文件同步扩展遵守 [CSY-01–08](../plans/2026-11-10-cloud-file-sync.md)；本文件的目录服务约束继续有效。

## 1. 依据和范围

用户本次要求开始按设计实现云空间。以[桌面需求 §2.1、§5.2](2026-11-02-pixcake-desktop-phase1-design.md)、[架构 §4、§5、§6.2、§13、§17](2026-11-02-pixcake-kernel-architecture-design.md)和[审核清单](review/2026-11-02-pixcake-review-index.md)为约束。本契约补齐新增模块的内部行为，不宣称它是官方云 API 或团队角色规则。

已有本地索引 `ProjectLibrary`、`SqliteProjectStore` 和精修文档 `EditDocument` 继续由各自模块拥有。`ProjectSnapshot` 已在后续 `services/project_session` 中实现，云端清单及分块属于 `services/cloud_sync`。只有服务器提交确认/本地导入事务成功才能更新云标识；既有 `origin` / `cloud_marked` 字段自身不能证明文件曾上传。

目录阶段独立实现网关与 macOS HTTPS 接入；后续已增加账号、上传下载、存储迁移与冲突保留副本，团队成员继续由可信 CLI 管理。协同编辑和参数自动合并未实现。架构 §17 禁止将云协同深层协议写进内核的约束保持有效。

## 2. 模块主人

| 模块 | 本轮职责 | 允许的依赖 |
| --- | --- | --- |
| `services/account` | 既有登录会话；补充登录代数 `generation()` | `kernel/support` |
| `services/cloud` | `CloudSession`、只读 `CloudService`、类型化 `CloudGateway` 端口 | `services/account`、`kernel/support` |
| `services/cloud_transport` | 分页、资源/超时预算、HTTP 状态映射及 API v1 codec | `services/cloud`、`services/net`、`kernel/support`；JSON 库仅在私有实现 |
| `services/net/curl` | 固定来源的 HTTPS、TLS 校验、超时及接收上限 | `services/net`、`kernel/support`；系统 curl 仅在私有实现 |
| `apps/shell` / `ui/shell` | 只读目录页面、后台请求和用户状态；CBR-01–06 见[接入计划](../plans/2026-11-10-cloud-browser.md) | 服务的普通 C++ 只读模型；具体网络调用限后台客户端 |

新增 `services/cloud_sync` 只依赖普通 C++ 项目会话/文档接口、网络抽象和私有哈希适配器；nlohmann 与平台 SHA-256 不进入公开头。它不直接操作 SQLite，不依赖 Qt、图像库或具体 curl。系统书签/钥匙串及线程切换分别在平台适配器和壳层编排。内核新增的文档复制、资源重定位、项目来源标记与导入回执接口均不包含云协议。

`services/cloud` 不直接依赖 `services/net` 或 `services/cloud_transport`，不链接 Qt、SQLite、HTTP 库、图像库或 GPU。`kernel` 不反向依赖云模块。目录查询没有本地项目写入副作用；后续下载落库和文档提交必须走现有命令入口，不能由页面或网关直接写数据库。

## 3. 内部契约

| 编号 | 入口/数据 | 行为和失败 |
| --- | --- | --- |
| CLD-01 | `CloudSession(account)`、`open()`、`close()` | 绑定活的 `AccountSession`，不能传一份孤立账号快照就获得云权限。`open` 清空旧目录后绑定当前登录代数；未登录/非法云身份返回 `StateConflict`。关闭只影响云会话，不退出桌面账号、不删除本地项目。 |
| CLD-02 | 个人/团队空间与 `Read`、`Write`、`Manage` | 权限是网关显式提供的独立能力；管理不自动包含读写，写也不包含读。客户端只是预检查，服务端仍须独立鉴权。`PermissionDenied` 返回后清空当前空间权限，必须刷新才能再次查询。 |
| CLD-03 | `refresh_spaces(timeout)` | 未登录或超时参数非法不调用网关。完整验证成功后一次替换目录；失败保留上一份有效目录，明确鉴权拒绝除外。成功空目录表示确实无空间，不能与网络失败混用。 |
| CLD-04 | `list_projects(space_id, timeout)` | 空/过长/含控制字节的 ID 返回 `StateConflict`，未知或不可读空间返回 `PermissionDenied`，均不调用网关。返回仅有 ID、所属空间、名称、非负修订/更新时间/字节数的元数据；整批验证，任一坏项使整批无效，不暴露部分结果。 |
| CLD-05 | 旧响应 | 请求前复制身份并记目录代数；返回后验证登录代数和目录代数。登出、同凭证重登、换账号、云会话关闭重开、更新的目录刷新均使旧响应返回 `StateConflict`，不暴露旧账号项目、不覆盖新目录。 |
| CLD-06 | 输入和资源界限 | 见下面表格；重复空间/项目 ID、非法枚举、重复权限、空名称、负数和超限目录拒绝。远端坏返回遵循 C-19，用 `NetworkFailed`；本地调用状态错误遵循 C-22，用 `StateConflict`。 |
| CLD-07 | 日志 | 使用已有 `LogSink` 注入，事件为 `cloud.spaces.refresh` / `cloud.projects.list`，模块为 `cloud`，携带最终结果码。不得记录身份、会话句柄、项目名或任意网关文本。正式入口接现有进程日志以补时间、进程信息。 |
| CLD-08 | 依赖 | 专用构建门禁检查云层依赖，并禁止 `kernel` 包含云模块头或链接云目标；正负样例验证门禁会拒绝违规。 |

`AccountSession::generation()` 在成功 `sign_in`、`load` 和每次显式 `sign_out` 时递增，包括相同账号/凭证重新登录和清空缓存。已经退出时 `sign_out` 仍返回 `StateConflict`，但必须推进代数，使尚在后台验证的恢复请求失效，不能把已退出的账号重新登录。失败的登录/缓存装载以及工具栏/收藏色修改不递增。它是内存生命周期编号，不是服务端会话 ID，不持久化、不作为认证凭证。

账号会话和云会话是不可复制的生命周期主人，禁止通过复制/赋值回滚登录代数；现有调用方通过引用使用账号会话，账号内容变更继续走 `sign_in` / `load`。

`CloudSession` 不复制保存凭证，查询前从账号服务取一次请求副本。账号服务必须比云会话活得更久。目录对外返回值副本，失效账号不再暴露旧目录。账号变化后需显式 `open()` 和刷新，不自动恢复旧权限。

## 4. 资源和调用边界

以下是客户端防御上限，非官方产品配额。类型定义在 `cloud_types.hpp` 中只有一份。

| 项目 | 上限/规则 |
| --- | --- |
| 空间、项目、账号 ID | 非空，最多 512 字节 |
| 空间、项目名称 | 非空，最多 1024 字节；允许中文等非 ASCII 字节 |
| 云请求中的会话句柄 | 非空，最多 8192 字节 |
| 上述字符串 | 不含 ASCII 控制字节（包括 NUL 和 DEL） |
| 一次规范化空间目录 | 最多 1024 项 |
| 一次规范化项目目录 | 最多 10000 项；只读元数据，无原图/参数正文 |
| 请求超时 | 调用方显式传入，范围 1–60000 毫秒 |

重复 ID 校验用有序集合，复杂度为 O(n log n)，不做逐项全量扫描。限额针对已经解码的目录；将来的 transport/codec 还必须在分配前限制原始响应字节，不能把此校验当作网络层内存防护。

`CloudGateway` 是同步、可注入的端口。具体网关必须遵守整体 timeout（包含分页）并把超时/断网映射为 `NetworkFailed`。阶段 1 测试预算传递，阶段 2A 使用可注入时钟测试整体预算计算，2B1 由实际 libcurl I/O 超时及本地 TLS 联调验证。端口不能中止任意不守约的实现。未来壳层须在 UI 线程之外执行请求，并把状态变更串行交还拥有者；当前类不支持多个线程同时读写。旧响应测试验证串行回调顺序，不能视作线程安全测试。

## 5. 验收对照

| 既有规范 | 本轮对应 |
| --- | --- |
| C-19 / B-50 | 网络失败及坏远端返回不会变成成功或清空有效目录；超时预算透传 |
| C-22 / S-25 | 未登录拒绝、会话失效与旧响应拒绝 |
| S-06 / T-01–T-03 | 字符串、数值、枚举、批量上限及坏包校验 |
| S-18 / S-22 | 云会话不持久化凭证，诊断不含秘密 |
| P-02 | 只读元数据，不加载原图或编辑正文 |
| Q-01 / Q-05 / Q-12 / Q-29 | 独立服务目标、统一结果码、无 UI 和内核云协议 |
| T-06 / T-07 | 镜像测试目录和可失败的依赖门禁 |
| O-01 / O-02 | 服务出口记录最终结果，使用既有日志接口；正式进程日志接线属于壳层阶段 |

目录页面按 CBR-01–06 实现；文件同步页面、持久化恢复和双目录真实后端管道集成已按 CSY 实现。真实网络超时见阶段 2B1；新文件同步的完整 TLS + renderd 验收已在提交前独立运行通过，未用管道测试代替。

## 6. 阶段 2A：HTTP 网关编排契约

本节是后端无关的适配骨架。新增 `services/cloud_transport`，公开 `CloudWireCodec` 和 `HttpCloudGateway`；实现 `CloudGateway`，允许依赖 cloud/net/support。原目录层的依赖限制不变。编码器负责受信任端点、鉴权与供应商字段；2A 只使用测试编码器，2B1 根据本项目已冻结协议补实现。

| 编号 | 契约 |
| --- | --- |
| CLD-09 | 空间与项目分别编码/解码，使用不透明游标。空游标表示首/末页；游标只交给 codec，不当成 URL 直接跟随。默认最多 128 页，游标最多 4096 字节；出现循环或超限，整次查询失败，不返回部分目录。 |
| CLD-10 | 整次查询共用一份单调时钟预算，包含编码、传输、解码和分页。每次发送传入剩余毫秒数；超时、时钟倒退或最后一页超时返回 NetworkFailed。同步端口仍依赖 transport 执行实际 I/O 超时，不能中断不守约的实现。 |
| CLD-11 | 默认单页响应正文 2 MiB、累计正文 64 MiB、响应头内容 32 KiB/128 项、请求正文 64 KiB；可配置为更低限额，不可超过这些防御上限。原始长度在解码前检查，类型化条目累计不超过 CLD-06。每个 HTTP 请求携带剩余正文/头部上限，未来生产 transport 必须在接收时执行（包含解压后限制）。 |
| CLD-12 | transport 的失败先返回，不解码；HTTP 401/403 -> PermissionDenied，其余非 2xx（含 3xx）-> NetworkFailed。2xx 必须经过 codec 成功解码，不能把未知/空包直接当空目录；codec 返回的明确业务错误透传，但丢弃其任何部分输出。 |
| CLD-13 | 编码失败不发送。编码器只能产生有界 HTTPS 的 GET/POST 目录查询，禁止控制字节；完整 URL/请求头语法、受信任来源与 TLS 校验由生产 codec/transport 负责。本适配器强制关闭重定向，避免凭证被发送到响应指定的其他地址；不做自动重试、不记录请求/响应或游标。 |

`HttpRequest` 追加可选响应限额（0 表示旧调用方未指定）和默认关闭的重定向开关；保留既有 `HttpTransport::send(request, timeout)` 签名。新增字段不能被未来生产实现静默忽略。网关同时做返回后检查，防止不守约的测试/第三方 transport 将超限内容送入 codec，但该检查不能撤销 transport 已经发生的分配。

空间/项目摘要的语义校验仍由 `CloudService` 负责，网关只聚合规范化页和检查资源/HTTP 边界；跨页重复 ID、错误空间等由现有服务统一拒绝。测试必须贯穿 CloudService -> HttpCloudGateway -> fake transport/codec，证明失败保留旧目录、鉴权拒绝撤销权限、迟到结果不被接纳。

本阶段完成只标记 2A，真实 TLS/HTTP 实现、生产 codec、分页快照一致性与沙盒联调属于 2B，不能用替身证明已完成。

## 7. 阶段 2B1：本项目 API v1 与真实 HTTPS

依据[后端 API v1](2026-11-09-cloud-backend-api.md)实现 `BackendV1Codec` 和独立 `CurlHttpTransport`；依赖选择见[记录](../evidence/2026-11-09-cloud-desktop-dependencies.md)。本阶段验证桌面服务链，不把开发令牌接成用户登录。

| 编号 | 契约 |
| --- | --- |
| CLD-14 | codec 只编码根 HTTPS 来源下的 `/v1/spaces` 与 `/v1/spaces/{space_id}/projects`，GET、Bearer、JSON Accept；页大小 1–100。账号/项目/空间 ID 与会话令牌遵守后端 v1 形状，游标只允许其已定义的 ASCII URL-safe 编码，不当作 URL。页大小、ID、令牌、游标或根来源形状无效时返回 StateConflict 并清空 request；完整 URL 语法及同源判定由 CLD-16 的 transport 负责。 |
| CLD-15 | 只解码 HTTP 200 的 application/json、api_version=1、确定的 envelope/字段；单页最多 100 项、正文 2 MiB、嵌套深度 8、JSON 节点 4096、对象字段 32、字符串 4096 字节。通过成熟 SAX 解析器限制结构分配并拒绝重复 key；字段复制前验证 ID、名称、权限、非负 int64 和游标。任何错误返回 NetworkFailed 并清空 page；已识别认证拒绝仍为 PermissionDenied。CloudService 保留最终跨页语义校验。 |
| CLD-16 | transport 固定一个 HTTPS 来源，使用 libcurl URL API 检查 URL，无 userinfo/fragment；禁止跨来源请求。只支持 GET/POST/DELETE，拒绝自报 Host/连接/传输长度/代理认证/压缩控制头及重定向请求。证书链与主机名必须验证；CA 文件只改变受信任根，不关闭校验。 |
| CLD-17 | timeout 为 1–60000ms，包含 setup 和 I/O；curl 必须支持异步 DNS。缺省响应正文 8 MiB、头 32 KiB/128 项；可显式要求正文最多 64 MiB、头最多 64 KiB/256 项，请求正文最多 8 MiB、URL 最多 16 KiB。云网关继续使用更低 CLD-11 限额。头部累计包含中间响应/尾部，正文计解压后字节；超限中止并丢弃内容。库内部的协议缓冲受 libcurl 自身限制，不宣称零内部分配。 |
| CLD-18 | 本地 TLS 集成必须运行真实 C++ CloudService -> HttpCloudGateway -> codec -> transport -> 后端，验证空间/项目多页、401/403、CA/主机名错误、重定向不跟随、真实超时和解压后大小限制。临时 CA 仅由测试显式注入，不写系统钥匙串；测试端点只放测试进程，生产 API 不增加测试后门。 |

底层仍为同步服务，不能直接放入 UI 事件线程。CBR-01–06 已用后台客户端完成账号 ID/到期验证与只读页面；后续账号和上传下载的具体实现及测试见[验收记录](../evidence/2026-11-10-cloud-auth-and-sync.md)。真实短信、完整账号权益与生产部署仍须独立接入。
