# 账号与云项目文件同步验收

日期：2026-11-10。环境：macOS arm64、Apple SDK 27.2、C++17/Qt、Python 3.12；Debug 启用云 HTTP/开发页面，Release 关闭这两项。用户明确授权随机开发验证码自动填入、注册/登录/现代密码存储，并将文件同步、版本冲突与断网恢复纳入本轮。

本轮完成开发 SDK 与单机服务实现，账号真实 TLS 桌面闭环和文件同步真实后端管道集成已通过。文件同步的完整 HTTPS + renderd 脚本在提交前授权恢复后也已执行成功；公网部署和真实短信没有完成。这里分别记录，不把管道测试当成网络/TLS 验收。

## 设计与实现

遵守[云空间契约](../specs/2026-11-09-cloud-space-contract.md)、[AUTH-01–07](../plans/2026-11-10-cloud-auth.md)、[CSY-01–08](../plans/2026-11-10-cloud-file-sync.md)以及现有 C-19/C-22、S-06/S-18/S-22/S-25、Q-05/Q-11/Q-12、P-02、T-01–07。协议与清单归 services/独立后端，UI 只发 PageActions；内核仅增加通用文档复制、资源重定位、项目来源标记和导入回执接口。

| 能力 | 实现及边界 |
| --- | --- |
| 开发短信 | `SmsSender` 投递端口、开发 mock 和 `SdkSmsSender` 回调适配。随机六位码只在显式开发服务中返回，开发桌面核对手机号/用途/挑战后自动填入。生产拒绝 mock，Release 不接受自动码能力。真实供应商需配置客户端、模板、凭证与最多 3 秒超时。 |
| 账号 | 显式注册原子创建账号、个人空间和凭证；短信/密码登录、短信重置、退出。验证码 300 秒/60 秒重发/5 次尝试；HMAC 摘要、单次使用、用途绑定及持久限流。 |
| 密码 | NFC、15–128 字符、最多 512 UTF-8 字节，允许粘贴和空格，基础弱密码检查。Argon2id 随机盐、64 MiB/t=3/p=4、验证后按需升级；最多两个哈希任务，未知账号使用虚拟哈希，坏哈希参数先拒绝。 |
| 会话 | 访问令牌 1 小时，刷新凭证 30 天绝对到期；摘要入库，轮换与重放撤销会话族，重置密码撤销全部旧会话。可选系统钥匙串仅保存刷新凭证，恢复须由服务器确认。 |
| 桌面隔离 | 主线程拥有账号与显示状态；工作线程拿不可变请求副本并拥有自己的项目会话。账号代数拒绝旧结果，切换账号暂停旧传输。项目、绑定及断点按服务端账号/HTTPS 来源隔离。 |
| 文件 | 原图、已提交参数、虚拟副本、图层和生成像素；不传本机绝对路径、书签、令牌、预览临时值或撤销历史。1 MiB SHA-256 分块，清单最多 2 MiB、2000 原图/8000 资源、8 GiB 项目、16 MiB 单文档。 |
| 版本 | 开始/提交幂等、任务使用独立安全随机 nonce；SQLite 比较基础修订后原子发布。冲突不覆盖，保留两份创建新云项目；下载创建新本地项目。 |
| 恢复 | 持久任务日志/分块缓存/已确认绑定，最多 3 次间隔 30 秒自动重试，随后手动继续；暂停等待当前有界请求结束。导入事务与持久回执防重复，崩溃遗留文档仅允许按一致字节接纳。 |
| 清理 | 清理意图先落盘，幂等 DELETE 只清除任务所属账号的未提交块；响应丢失或重启不会误变回上传。空间撤权后仍可清理自己未提交的任务，其他账号无权清理；已发布版本始终保留。 |
| 后端存储 | 专属 SQLite schema 3，显式事务迁移 1/2；文件在私密 `.objects`。单账号最多 4 个待传任务、50 GiB 全版本与暂存防御上限、暂存 7 天；整体验证并发上限 2。单机实现未声称支持多实例对象存储。 |

## 实际验证

### Python 后端

在 `backend/cloud` 执行 `.venv/bin/pytest -q`：**49 passed**，包含原目录/操作 29 项、账号 13 项、同步 7 项。随后撤权清理逻辑调整，对 `tests/test_sync.py` 重新执行 7 项。覆盖：

- 原子注册、明文不入库、OTP 绑定/失效/重复使用、重发、SDK 异常、生产 mock 拒绝和请求类型/大小/重复键校验。
- Argon2id 参数边界与升级、Unicode 密码、刷新轮换与重放、访问令牌过期后的退出、重置撤销所有会话、限流跨重启。
- schema 迁移不丢失旧账号；分块精确长度/SHA-256、路径与重复键拒绝、布尔格式版本拒绝、损坏块不发布。
- 两个并发提交只有一个胜出、重复提交稳定返回、读写权限分离、撤权、任务到期、容量与并发上限、幂等清理及已发布版本保护。

存在一条测试客户端弃用提示：当前 Starlette 的 HTTPX 兼容入口以后需迁移。当前锁定环境测试通过；未把测试客户端加入服务生产路径。

### 真实 TLS 账号桌面流程

已执行 `python -m tools.smoke_auth_https <Debug 应用>`。临时 CA、真实 Uvicorn/FastAPI、实际 Qt 控件点击与验证包含：随机验证码自动填入、注册、密码登录、短信登录、重置、旧密码拒绝、新密码登录、云目录、退出。脚本最后核对测试会话已撤销。

输出：`Desktop HTTPS auth passed: random OTP auto-fill, registration, password/SMS login, reset, cloud and logout.`

截图 `build-cloud-space/auth-register.png` 已检查；仅使用临时测试账号。测试不写用户钥匙串、不操作用户照片。钥匙串适配已编译，客户端恢复/失败逻辑使用内存 CredentialStore 替身验证，未宣称真实用户钥匙串端到端验收。

### 真实后端双目录文件集成（无网络监听）

已在仓库根目录运行：

```sh
build-cloud-space/tests/test_cloud_sync --asgi "$PWD/backend/cloud/.venv/bin/python" "$PWD/backend/cloud/tools/sync_asgi_bridge.py"
```

C++ `SyncEngine` 通过测试管道适配器发送实际请求，子进程运行真实 FastAPI/SQLite/文件服务。该适配器仅在测试目录；生产桌面仍使用固定来源的 curl HTTPS。测试使用两份独立临时项目库和约 2.3 MiB BMP：

- A 上传原图、曝光参数、虚拟副本、星级/旗标/色标、生成图层；中途断块、进程重建续传，提交响应丢失后不重复创建修订。
- B 下载遇到损坏块后重新接收；已有本地项目保持不变，图片 ID 重定位，系统 ImageIO 实际解码出 768×1024 原图，参数/副本/图层与像素文件恢复。
- 未登录导入拒绝；第二张照片写入阶段失败时回滚索引和新文档；模拟文档已持久但 SQLite 未提交的崩溃，重复恢复按回执只创建一次项目。
- A 发布新版本后 B 的旧基础修订上传返回冲突；保留两份生成另一云项目。
- 清理任务遇到断网、重启及 DELETE 响应丢失后仍只执行清理；原已发布版本可继续读取。

输出：`Pipe/ASGI two-library sync passed: originals, edits, copies, layers, restart, corruption, conflict.`

这项测试验证图片解码和文档恢复，未启动网络监听或 renderd；完整 renderd 打开路径在提交前的独立 TLS 模式中另行执行通过。

### C++/Qt 与发布边界

- Debug `pixcake_app_bundle` 和相关测试目标构建成功；18 项定向检查在分批执行及修复后通过：编辑/图层文档、账号、目录服务、网关、目录后台客户端、账号后台客户端、传输后台客户端、账号/目录 codec、curl 请求校验、同步快照、项目路径/会话、云架构、UI 源码、登录与云页面冒烟。
- `test_cloud_transfer_client` 验证请求不会阻塞 UI、完成回调在拥有者线程、快照枚举在后台、暂停状态、切换目录后旧 pending 不泄露、重新进入原账号目录可恢复、退出隐藏任务。
- Release/HTTP 关闭构建成功；8 项相关检查通过，包括拒绝 `--page`、`--dev-cloud-session-file`、`--smoke-auth`，以及登录页、账号客户端、项目会话、源码和架构边界。
- `architecture_gate`、`third_party_locks_T22`、`git diff --check` 通过。云门禁增加同步服务正负例：拒绝 Qt、直接 SQLite/具体 curl/CommonCrypto、公开 JSON/平台头和不支持的平台适配；kernel 禁止依赖 cloud_sync。
- 曾有云门禁负例未能识别含数字的 `sha256.hpp`，已修正匹配规则并重跑通过。此前全仓 UI 源码 22 项问题已由后续工作修复，本轮 Debug/Release `test_shell_sources` 均通过；未运行全仓全部功能测试。
- `build-cloud-space/cloud-transfer.png` 已检查，空选择框有明确提示，操作按钮和表格无溢出。未配置后端时显示真实未配置状态，不填假项目。

## 依赖与可复现入口

Argon2 使用锁定的 `argon2-cffi 25.1.0`、bindings 26.1.0、CFFI 2.1.1、pycparser 3.12，官方 PyPI 发行包及 SHA-256 记录在 `backend/cloud/uv.lock`。许可证全文已补入 `backend/cloud/licenses` 和 [THIRD_PARTY.md](../../../backend/cloud/THIRD_PARTY.md)。Argon2 reference implementation 采用 CC0-1.0 许可，保留上游同时提供的完整许可文本。

比较 Argon2id、scrypt 与 bcrypt 后采用 Argon2id 高层验证/升级 API；参数依据 [RFC 9106](https://www.rfc-editor.org/rfc/rfc9106.html) 与 [OWASP 密码存储指南](https://cheatsheetseries.owasp.org/cheatsheets/Password_Storage_Cheat_Sheet.html)。桌面 SHA-256 使用 SDK CommonCrypto，随机任务 nonce 使用 Security/SecRandomCopyBytes，后端使用 hashlib/secrets。未新增密码学实现，未引入压缩/解压依赖；JSON/curl/SQLite 复用既有锁定依赖。

运行步骤及真实 SMS SDK 替换方式见 [backend/cloud/README.md](../../../backend/cloud/README.md)。新增持久开发启动器 `tools.run_desktop` 已通过 Python 语法编译、`--help`，以及提交前使用临时状态目录的完整启动/账号/关闭验收；独立临时脚本的真实 TLS 账号流程也重新通过。

## 提交前独立快照复验与部署边界

首次 `tools.smoke_sync_https` 提权请求因自动审批服务额度耗尽而未执行，当时是审批检查无法完成，不是安全性判定。用户随后要求提交上传；授权执行恢复后，已完成以下复验，没有绕过原审批：

- 从 `df478e8` 提取本轮账号/云模块及必需的通用文档、项目和导航接口，在独立源码快照构建成功；AI 助手、生成管线与首页改版的其他并行修改留在原工作区。
- 独立快照后端 **49/49**、C++/Qt 定向检查 **18/18** 通过，包括登录/主页/本地项目/云页面、账号/目录/传输客户端、codec、同步、项目会话、源码和依赖门禁。
- `tools.smoke_sync_https` 输出 `HTTPS two-library sync passed: originals, edits, copies, layers, restart, corruption, conflict.`；真实 curl HTTPS、双项目库和 renderd 打开通过。
- `tools.smoke_auth_https` 在独立 Qt 应用上重新通过随机码填入、注册、密码/短信登录、重置、云目录和退出。
- `tools.run_desktop --smoke` 使用临时私密状态目录、临时本机端口通过启动、账号交互与后端关闭；测试结束清除临时数据。

独立分支保留当前工作区与其他任务的暂存内容。测试没有接入真实短信、操作用户照片或写用户钥匙串。

本轮未选择短信供应商、创建公网服务或付费云资源；真实短信、生产 HTTPS/密钥管理、多实例数据库/对象存储、备份恢复及真实磁盘耗尽/大容量性能演练仍需部署环境。备份必须同时包含专用 SQLite 和相邻 `.objects` 文件，不能只保存项目目录元数据。
