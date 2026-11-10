# 云后端账号、目录与文件同步 API v1

日期：2026-11-09
范围：独立后端的账号、会话、个人/团队目录及文件同步。2026-11-10 按用户授权追加 AUTH-01–07、CSY-01–08。此契约由本项目定义，不是外部供应商协议。

## 部署与边界

代码位于 `backend/cloud`，独立 Python 3.12 子项目，使用锁定依赖运行；不进入桌面 CMake 或应用包，不被 `kernel` 引用。第一步使用独立服务端 SQLite 文件，仅支持单机实例，不读取桌面 `projects.sqlite`。后续规模化部署再落实数据库迁移和对象存储。

默认运行入口仅监听 `127.0.0.1`，用于本地联调。目录、账号桌面流程及文件同步双目录均已通过临时 CA 的本地 TLS 验证，文件同步同时通过无监听管道集成。公网部署与真实短信供应商仍待配置。桌面网关的 HTTPS 要求不因本地验证而放宽。

用户已授权注册、密码登录、验证码登录/重置密码及可替换开发短信 SDK。开发码每次随机、只在明确的开发模式返回，不存在万能码。可信本地管理命令仍可管理账号、团队成员和版本 0 元数据；Manage 不自动包含读写，团队管理尚无公开 HTTP/网页管理入口。

## 账号扩展（AUTH-01–07）

请求均为严格 JSON 对象、最多 4 KiB；拒绝重复键、额外字段、非字符串输入及无效 Unicode。手机号为大陆 11 位。密码经 NFC 规范化，15–128 字符且不超过 512 UTF-8 字节；Argon2id 随机盐、64 MiB/t=3/p=4，最多 2 个并行哈希任务，旧参数成功登录后升级。完整行为见[账号计划](../plans/2026-11-10-cloud-auth.md)。

| 路径 | 请求/结果 |
| --- | --- |
| `GET /v1/auth/config` | `api_version,sms_enabled,development,password_min,password_max` |
| `POST /v1/auth/code` | `phone,purpose`（register/login/reset）；返回 `challenge_id,expires_in,resend_after,development_code`；生产 code 为空 |
| `POST /v1/auth/register` | `phone,challenge_id,code,password`；事务创建账号、个人空间、凭证并登录 |
| `POST /v1/auth/sms/login` | `phone,challenge_id,code`；只登录已有账号 |
| `POST /v1/auth/password/login` | `phone,password`；未知账号和错误密码返回相同 401 |
| `POST /v1/auth/password/reset` | `phone,challenge_id,code,password`；撤销旧会话并登录 |
| `POST /v1/auth/refresh` | `refresh_token`；轮换 access/refresh，旧刷新凭证重放撤销整族 |
| `POST /v1/auth/logout` | `refresh_token`；幂等撤销会话族，204；访问令牌过期仍可退出 |

令牌响应包括 `api_version,account_id,access_token,refresh_token,session_expires_at,refresh_expires_at`。访问有效期 1 小时、刷新绝对有效期 30 天；数据库只保存摘要。六位验证码用安全随机源生成，HMAC 持久化、5 分钟、最多错 5 次、60 秒重发冷却，手机号/IP 限流持久化；重发使旧码失效。错误只返回已定义通用消息，不回显密码、手机号、验证码或令牌。

## 文件同步扩展（CSY-01–08）

所有同步请求使用 Bearer 并重新校验账号。发布与读取检查独立 write/read 能力；清理仅要求任务账号所有权，不授予发布或读取权限。禁止 query 参数和重定向。JSON 清单最多 2 MiB，二进制块最多 1 MiB，读取请求正文共用 5 秒预算。桌面普通请求 5 秒，开始/提交的磁盘校验请求最多 60 秒；两个并发校验槽，超额 429。详见[分层与状态计划](../plans/2026-11-10-cloud-file-sync.md)。

| 路径 | 契约 |
| --- | --- |
| `POST /v1/sync/uploads` | `operation_id,space_id,project_id,base_revision,manifest`；新项目传空 project_id/版本 0。返回 `api_version,upload_id,project_id,revision,missing`（SHA-256 数组）。同账号幂等键绑定完整请求。 |
| `POST /v1/sync/uploads/{id}/chunks/{sha256}` | octet-stream，仅接收清单声明的摘要/精确长度；204。块写临时文件、fsync 后原子替换；已发布版本不可改写。 |
| `POST /v1/sync/uploads/{id}/commit` | 空 JSON 对象；全块校验、再次鉴权、SQLite 比较基础修订后提交。返回 `api_version,project_id,revision`；重复提交返回同一版本。冲突 409，不覆盖。 |
| `DELETE /v1/sync/uploads/{id}` | 有效会话及任务账号所有权验证后，幂等清理未提交上传和分块，204；撤回空间写权限后仍可清理自己任务。已提交版本保留，缺失任务 204，其他账号访问已有任务 403。 |
| `GET /v1/sync/projects/{id}/revisions/{revision}` | 固定版本；返回 `api_version,project_id,space_id,revision,manifest`。版本 0 无文件可下载。 |
| `GET /v1/sync/projects/{id}/revisions/{revision}/chunks/{sha256}` | 仅可读该版本声明的对象；返回精确长度 octet-stream，响应前校验摘要。 |

`manifest` 固定 `format=1,name,files,photos`。资源项为 `key`（f 加 8 位数字）、`kind`（original/edit/layer/pixel）、`size`、`chunks`。照片项为 `source,name,rating,flag,color,items`，items 为原图及其虚拟副本的 `edit/layer` 资源键，未编辑时为空。禁止绝对路径、分隔符、重复资源键、错角色/缺失引用、布尔整数和长度不符。图层生成资源由文档公开重定位接口改写为资源键，下载后再绑定本机路径。书签和原本机路径只留在本地，不编码入清单。

最多 2000 张原图、8000 个资源/编辑实例、8 GiB 项目、16 MiB 单参数/图层文件；原图按块读写，不一次读入全部正文。服务器每账号最多 4 个未完成任务、50 GiB 版本及暂存总量；暂存 7 天到期，在该账号后续创建上传时清理。当前对象目录是专用数据库相邻 `.objects`，没有公开任意文件路径或对象直链。

桌面每账号、每 HTTPS origin 保存单个活动任务及绑定；任务含随机 128 位编号，幂等键同时绑定此编号和上传内容，避免“另存相同内容”错误复用旧云项目。令牌不写任务文件。下载逐块校验、重建文件后经 ProjectSession/BusinessCommands 和索引事务恢复为新本地项目；导入收据与项目一起提交，处理提交后响应丢失；事务回滚前已落盘的完全相同文档可恢复接管，绝不覆盖不同内容。

schema 3 在事务内兼容迁移 1/2；备份同时包含 SQLite 和对象目录。公网部署、多实例对象存储、团队成员网页管理、自动参数合并不由本接口假装实现。

## 接口

所有目录接口仅接受 `Authorization: Bearer <opaque-token>`，不从 URL、Cookie 或客户端自报账号取得身份。请求不带正文。响应 UTF-8 JSON，成功目录封装为 `{"api_version":1,"items":[],"next_cursor":""}`。机器可读定义保存为 `backend/cloud/openapi.json`，与应用 schema 进行一致性测试。

| 方法/路径 | 成功响应 | 说明 |
| --- | --- | --- |
| `GET /healthz` | 200，`status=ok`、`api_version=1` | 存活检查，不含配置、路径或身份 |
| `GET /v1/account` | 200，`api_version`、`account_id`、`session_expires_at` | 当前服务端身份；不伪造余额或订阅 |
| `DELETE /v1/session` | 204，无正文 | 撤销当前令牌；再次使用返回 401 |
| `GET /v1/spaces` | 200，空间页 | 当前账号所有成员空间；权限为显式能力 |
| `GET /v1/spaces/{space_id}/projects` | 200，项目页 | 每页重新验证 Read；未知空间和无权限同为 403 |

空间项：`id`、`name`、`kind`（`personal` / `team`）、`permissions`（独立的 `read` / `write` / `manage`）。个人空间只允许其拥有者成为成员；团队可由本地管理员分配三项独立能力。Manage 或 Write 不隐含 Read。

项目项：`id`、`space_id`、`name`、`revision`、`updated_at`、`size_bytes`；后三项为非负 64 位整数，时间使用 Unix 秒。首期创建的是版本 0、大小 0 的项目元数据，不代表文件已经上传。

v1 编码形状：账号 ID 为 `acc_` + 32 位小写十六进制，空间为 `sp_` + 32 位，项目为 `prj_` + 32 位；会话令牌为无 padding 的 43 字符 base64url（`A-Z a-z 0-9 _ -`）。当前签名游标仅使用这些 URL-safe 字符及 `.`，不能传入 URL 或 query 片段。字段、枚举或形状的变更必须更新版本/客户端兼容契约，不能静默扩展严格 v1 响应。

## 会话、分页与一致性

| 编号 | 规则 |
| --- | --- |
| BCL-01 | 可信本地管理命令签发的会话令牌使用标准库 CSPRNG 生成 32 随机字节，只将 SHA-256 摘要保存至数据库；原令牌只写入创建者指定的私密文件，默认 1 小时、最长 24 小时。公共账号 API 的访问/刷新凭证另遵守 AUTH。无令牌、坏令牌、到期、已退出、禁用账号均返回 401。会话验证和目录读取位于同一读事务。 |
| BCL-02 | 账号与空间成员关系由服务端读取；每页检查权限。客户端无权通过 Header、query 或 cursor 扩大权限。个人空间不得加入其他账号。成员撤销后，新项目请求立即被拒绝。 |
| BCL-03 | `limit` 默认 100、范围 1–100；`cursor` 首次省略或为空，末页返回空串。ID 升序 keyset 分页，游标不作为 URL。使用 ItsDangerous 签名，绑定会话摘要、账号、目录类型、空间、页大小、修订和初始 5 分钟有效期。拒绝篡改、跨账号/会话/空间/接口/页大小使用；后续页不延长有效期。 |
| BCL-04 | 账号空间目录、空间项目目录维护单调修订。后续页在同一事务中校验修订和读取数据；期间数据/相关权限变化返回 409 `directory_changed`，调用方应从首页重试，不混合不同快照。已撤销读取权优先返回 403。 |
| BCL-05 | 目录 API 为有界读查询及当前会话撤销。名称非空、UTF-8 最多 1024 字节且无 ASCII 控制字符；ID 使用服务器生成的短 ASCII 标识。query 最多 8192 字节、cursor 最多 4096 字节（服务端生成 ASCII，拒绝非 ASCII 游标）、请求头内容最多 32 KiB/128 项；目录/会话端点拒绝未知/重复 query 参数及请求正文，所有端点拒绝重复认证头。响应页最多 100 项，符合客户端单页 2 MiB 限额；AUTH/CSY 写请求使用各自正文边界。 |
| BCL-06 | 所有错误仅返回稳定错误码和通用文案，不包含原输入、令牌、游标、数据库路径或异常文本。所有响应 `Cache-Control: no-store`。本地运行关闭带 URL 的访问日志；安全诊断只记录事件、状态和错误类别。 |
| BCL-07 | 显式初始化并检查专属 SQLite application_id（PCCD）和 schema 版本；外键、约束与事务保证账号/空间/成员/项目一致性。未知数据库即使版本号相同也拒绝使用，不覆盖桌面项目库。存储故障返回 503，不能返回伪造空目录。重启保留会话摘要、成员与目录，退出状态持久化。 |

成功读取的线性化点是读事务中的授权检查；此前已启动的读取可在并发撤权后返回，撤权提交后启动的新请求必须拒绝。不是长期持有数据库快照的无限游标。

## 错误契约

错误封装：`{"api_version":1,"error":{"code":"unauthorized","message":"Authentication required."}}`。文案只帮助诊断，客户端按状态/错误码处理。

| HTTP | 业务码 | 客户端现有网关映射 |
| --- | --- | --- |
| 400 | `invalid_request` / `invalid_cursor` | NetworkFailed；不暴露部分目录 |
| 401 | `unauthorized` | PermissionDenied，撤销旧目录权限 |
| 403 | `forbidden` | PermissionDenied，撤销旧目录权限 |
| 409 | `directory_changed` | NetworkFailed；保留旧目录，显式从首页重新查询 |
| 410 | `cursor_expired` | NetworkFailed；需从首页重新查询 |
| 413 / 431 | `request_too_large` | NetworkFailed |
| 404 / 405 | `not_found` / `method_not_allowed` | NetworkFailed |
| 503 | `storage_unavailable` | NetworkFailed |
| 500 | `internal_error` | NetworkFailed；不返回异常详情 |

上述 BCL 为原目录契约；公共账号和文件同步已按本文件 AUTH/CSY 扩展实现。成员管理 HTTP/网页入口、账单及完整权益接口尚未实现，不能用本地管理 CLI 或本机点数模拟这些云能力。
