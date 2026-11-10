# PixCake 账号与云项目后端

这是独立运行的服务端，不随桌面应用打包。现已实现注册、短信/密码登录、密码重置、会话刷新与撤销、个人/团队目录，以及分块文件同步和版本冲突处理。开发短信由显式 mock SDK 生成随机验证码；正式短信供应商和公网部署尚未配置。验证范围见[本轮记录](../../docs/superpowers/evidence/2026-11-10-cloud-auth-and-sync.md)。

## 启动可体验的开发版

先在仓库根目录构建（模拟验证码自动填入仅允许开发构建）：

```sh
cmake -S . -B build-cloud-space -DCMAKE_BUILD_TYPE=Debug -DPIXCAKE_DEV_PAGES=ON -DPIXCAKE_ENABLE_CLOUD_HTTP=ON -DPIXCAKE_ENABLE_INFERENCE=OFF
cmake --build build-cloud-space --target pixcake_app_bundle -j 4
```

然后在 `backend/cloud` 中执行：

```sh
uv sync --locked
uv run --locked python -m tools.run_desktop ../../build-cloud-space/apps/shell/pixcake_app_shell.app/Contents/MacOS/pixcake_app_shell
```

启动器创建专用 `.state/development/cloud.sqlite` 和本机 TLS 证书，只监听 `127.0.0.1:8448`，显式启用开发短信；关闭应用后停止后端，数据保留。请先退出已有桌面实例再启动，避免单实例机制激活仍使用旧配置的进程。启动器不修改系统信任库，每次启动更新开发证书及验证码签名密钥，重启后重新获取验证码。不得将此入口用于生产。提交前已在独立源码快照上使用临时状态目录，验证启动器完整启动、账号流程及后端关闭；独立 HTTPS 账号冒烟也通过。

登录页下拉选择注册、短信登录、密码登录或重置密码。点击获取验证码后随机六位码自动填入，5 分钟有效；密码为 15–128 字符，服务器使用 Argon2id 随机盐（64 MiB、3 次、4 路）。选择记住登录时仅将刷新凭证保存到系统钥匙串；用户项目按服务端账号分目录保存。

在云空间中选择本地项目和可写空间后上传；选择版本大于 0 的云项目可下载为新的本地项目。同步包含原图、已提交参数、虚拟副本、图层与生成像素，保留原本地项目。断网保留分块，每 30 秒自动重试，最多 3 次，然后可手动继续；重启后点击继续传输。版本冲突可保留两份，或清除待传任务后下载云端副本。清除任务不会删除已发布版本或已有本地项目。

## 替换真实短信 SDK

`pixcake_cloud/sms.py` 的 `SmsSender.send(phone, code, purpose, challenge_id)` 是唯一投递端口。把供应商客户端、模板和凭证封装到 `SdkSmsSender` 回调，在应用工厂中调用 `create_app(settings, sms_sender=sender)`；供应商客户端应配置最多 3 秒的调用超时。验证码生成/校验、注册、登录、限流、密码哈希和刷新会话无需修改。真实配置使用 `PIXCAKE_CLOUD_MODE=production`，关闭 `PIXCAKE_SMS_PROVIDER=mock`；生产环境拒绝启用开发 mock，Release 桌面拒绝开发后端的自动验证码能力。

## 账号和文件同步验收

以下命令在 `backend/cloud` 执行，均只使用临时测试数据，不修改用户钥匙串：

```sh
uv run --locked python -m tools.smoke_auth_https ../../build-cloud-space/apps/shell/pixcake_app_shell.app/Contents/MacOS/pixcake_app_shell
uv run --locked python -m tools.smoke_sync_https ../../build-cloud-space/tests/test_cloud_sync
```

账号 HTTPS 桌面闭环和文件同步 HTTPS 双目录验收均已执行通过，后者包括 renderd 重新打开下载项目。先前的自动审批额度限制已恢复。无网络监听的真实应用集成也可在仓库根目录运行：

```sh
build-cloud-space/tests/test_cloud_sync --asgi "$PWD/backend/cloud/.venv/bin/python" "$PWD/backend/cloud/tools/sync_asgi_bridge.py"
```

此测试走 C++ 客户端 → 管道 → 真实 FastAPI/SQLite/文件存储，覆盖两份独立本地目录、图片解码、参数/副本/图层、断点恢复、提交响应丢失、损坏块、事务回滚与冲突另存；不验证网络和 TLS。

契约：[云后端 API v1](../../docs/superpowers/specs/2026-11-09-cloud-backend-api.md)；机器定义：[openapi.json](openapi.json)；依赖选型：[记录](../../docs/superpowers/evidence/2026-11-09-cloud-backend-dependencies.md)。

## 安装与测试

当前验证环境为 Python 3.12、uv 0.13.1、macOS arm64。所有命令在 `backend/cloud` 目录执行。

```sh
uv sync --locked
uv run --locked python -m pytest -q
uv run --locked python -m tools.smoke_http
```

最后一条会创建临时数据库与测试账号，在 `127.0.0.1` 启动真实 HTTP 服务，验证分页、隔离和退出，然后自动终止服务并清理数据。需要执行环境允许本地 socket。测试凭证不会打印，也不访问真实照片或桌面项目库。

FastAPI 当前使用的 Starlette 会提示测试客户端 HTTPX 兼容入口将弃用；目前兼容入口仍通过测试。依赖升级时须处理此提示，不在生产服务中引入测试客户端。

## 桌面 C++ HTTPS 联调

在仓库根目录构建可选的 macOS 实现；首次配置会从官方固定标签下载并校验 nlohmann/json 的头和许可证，HTTP 使用 SDK 系统 libcurl：

```sh
cmake -S . -B build-cloud-space -DCMAKE_BUILD_TYPE=Debug -DPIXCAKE_ENABLE_CLOUD_HTTP=ON -DPIXCAKE_ENABLE_INFERENCE=OFF
cmake --build build-cloud-space --target test_backend_v1_codec test_curl_http_transport test_cloud_https_integration -j 4
ctest --test-dir build-cloud-space -R '^(test_backend_v1_codec|test_curl_http_transport)$' --output-on-failure
```

然后在 `backend/cloud` 执行：

```sh
uv run --locked python -m tools.smoke_cpp_https ../../build-cloud-space/tests/test_cloud_https_integration
```

脚本创建临时 CA、独立服务端证书、数据库与两个账号，经私密文件把测试凭证交给 C++ 测试程序，启动仅监听本机的 TLS 服务，最后清理进程和文件。不会修改系统钥匙串。实际验证分页、身份隔离、退出撤权、故障保留目录、TLS 链/主机名、重定向、整体超时和解压后接收上限。故障端点仅由 `tools/https_fixture.py` 注册；生产 `pixcake_cloud` 应用不包含这些端点。该脚本要求本机 socket 权限及 macOS `/usr/bin/openssl`。

HTTP 开关默认关闭，不影响不需要网络实现的目录服务构建。当前只验证 macOS arm64；其他平台需独立适配。详见[客户端验收记录](../../docs/superpowers/evidence/2026-11-10-cloud-desktop-https.md)。

页面联调在根目录增加 `-DPIXCAKE_DEV_PAGES=ON`，构建 `pixcake_app_bundle`；然后在本目录给脚本增加第二个可执行文件参数：

```sh
uv run --locked python -m tools.smoke_cpp_https \
  ../../build-cloud-space/tests/test_cloud_https_integration \
  ../../build-cloud-space/apps/shell/pixcake_app_shell.app/Contents/MacOS/pixcake_app_shell
```

脚本通过 `PIXCAKE_CLOUD_URL`（根 HTTPS 来源）和 `PIXCAKE_CLOUD_CA_FILE`（测试 CA）配置应用，使用仅限开发构建的 `--dev-cloud-session-file` 读取私密令牌文件。此文件必须归当前用户、无组/其他用户权限、是普通文件且不是符号链接，内容为 43 字符令牌及可选换行；账号 ID 必须由服务器验证返回。脚本同时验证宽权限、符号链接、超长文件和命名管道被拒绝。发布构建不接受此选项。

正常运行时设置 `PIXCAKE_CLOUD_URL`；公共受信任证书通常不需设置 CA 文件。登录身份由后端确认，不能用环境变量伪造已登录状态。版本 0/大小 0 的目录元数据仍不代表已上传照片。旧目录阶段的历史验证见[页面验收](../../docs/superpowers/evidence/2026-11-10-cloud-browser.md)。

## 本地运行

先设置专用服务端数据库；切勿使用桌面的 `projects.sqlite`。初始化会检查专属数据库标识和 schema 版本，未知数据库拒绝使用。

```sh
export PIXCAKE_CLOUD_DB="$PWD/.state/cloud.sqlite"
uv run --locked python -m pixcake_cloud init
uv run --locked python -m pixcake_cloud create-account --name '本地开发账号'
```

记下输出的 `account_id` 和 `personal_space_id`，将下列占位内容替换为实际 ID。本地管理命令要求操作者能直接访问服务端数据库，它们不是公开 HTTP 注册或授权接口。

```sh
uv run --locked python -m pixcake_cloud issue-session \
  --account '替换为 account_id' --ttl 3600 --output .state/dev.token
uv run --locked python -m pixcake_cloud create-project \
  --space '替换为 personal_space_id' --name '目录测试项目'
```

令牌只写入新建的私密文件（POSIX 下权限 0600），不会写到终端，也不会覆盖已有文件。数据库保存令牌摘要；默认 1 小时，最长 24 小时。`create-project` 创建版本 0、大小 0 的元数据，不生成照片或上传成功标记。

生成当前开发环境的分页签名密钥并启动：

```sh
export PIXCAKE_CLOUD_CURSOR_SECRET="$(uv run --locked python -c 'import secrets; print(secrets.token_urlsafe(32))')"
uv run --locked python -m pixcake_cloud serve --port 8088
```

入口固定监听 `127.0.0.1`，默认单 worker、并发上限 64、不信任代理头、不启用访问日志/公开文档页/重定向。停止服务使用 Ctrl+C。需要保留跨重启分页时应安全保存同一签名密钥；轮换密钥使旧游标无效，不会凭空恢复会话。

另一终端可通过受保护的令牌文件查询（不把令牌放进 URL 或命令行参数）：

```sh
uv run --locked python - <<'PY'
from pathlib import Path
import httpx
token = Path('.state/dev.token').read_text().strip()
with httpx.Client(base_url='http://127.0.0.1:8088', trust_env=False, timeout=5) as client:
    response = client.get('/v1/spaces', headers={'Authorization': f'Bearer {token}'})
    print(response.status_code, response.json())
PY
```

其他本地命令：`create-team --owner ID --name 名称`、`set-permissions --account ID --space ID --permission read`、`revoke-membership --account ID --space ID`。权限选项可重复以组合 read/write/manage；不传权限表示空能力集。个人空间不能加入其他账号。

## 存储与接口维护

- 独立 SQLite、WAL、外键、每个请求独立连接；读取会话与目录处于同一个事务。服务端账号、会话、目录与撤销状态跨进程重启保留。
- 空间/项目按 ID 排序；游标绑定会话、账号、目录、页大小、修订，首次查询后 5 分钟过期。409/410 时从首页重试，不拼接过期页。
- 修改接口后运行 `uv run --locked python -m pixcake_cloud export-openapi > openapi.json`，评审 schema 变化并执行测试。导出需要已初始化的测试数据库和签名密钥，不携带数据库记录。
- 发行环境保留依赖的许可证文件；[THIRD_PARTY.md](THIRD_PARTY.md) 列出锁定包与许可证。无桌面 Python/数据库运行时新增依赖。

## 尚未完成的部署工作

本机测试不代表公网可用。生产前仍需：真实短信供应商、HTTPS 入口、受控密钥配置、运行时补丁和镜像锁定、漏洞审计、备份恢复演练及多实例数据库/对象存储方案。不得为本地联调放宽桌面网关的 HTTPS 策略。数据库已升到 schema 3，`init` 显式、事务化迁移 schema 1/2；备份必须同时保存专用 SQLite 数据库和相邻的 `.objects` 目录，不能只备份目录元数据。
