# 云后端 B1a 目录服务证据

日期：2026-11-09
结论：独立后端已可在本机运行，29 项后端测试、真实 HTTP 冒烟与 4 项客户端/架构检查通过。尚未完成公共短信登录、HTTPS 部署、桌面真实协议对接或上传下载。

依据：[后端计划](../plans/2026-11-09-cloud-backend.md)、[API v1 / BCL-01–07](../specs/2026-11-09-cloud-backend-api.md)、[依赖选型](2026-11-09-cloud-backend-dependencies.md)。实现来源为 PixCake-specific。

## 交付

- `backend/cloud` 独立 Python 项目，`pyproject.toml` 固定直接依赖，`uv.lock` 固定传递依赖与包 SHA-256；不进入桌面 CMake/应用包。
- `pixcake_cloud/store.py`：专属数据库标识与版本、独立 SQLite/WAL、账号/个人空间/团队/成员/项目元数据、令牌摘要、会话期限与撤销；本地管理写入有事务约束。
- `service.py` / `cursors.py`：身份与权限检查、有界排序分页、绑定会话/账号/目录/页大小的签名游标、五分钟固定期限与目录修订检查。业务失败以显式 `Result` 传到 HTTP 层。
- `api.py` / `boundary.py`：五个 HTTP 端点、稳定错误封装、原始请求限额、Cache-Control/no-store、隐藏异常/输入详情、不接受客户端自报身份或权限。
- 本地管理命令与运行入口、[运行说明](../../../backend/cloud/README.md)、[OpenAPI](../../../backend/cloud/openapi.json)、[许可证清单](../../../backend/cloud/THIRD_PARTY.md)。签发令牌只写新私密文件，交付失败回滚；没有公开管理/测试登录端点。

## 自动化验证

`tests/test_directory_api.py`：24 个用例（包含参数化项）。覆盖有效身份、数据库不存明文令牌、退出/重启、过期/禁用、未认证/错误认证、账号隔离、read/write/manage 不互相推导、个人空间成员限制、空间与项目分页、游标篡改/跨会话/跨账号/跨接口/页大小、固定 TTL、目录变化、团队撤权、坏 query 与非 ASCII 游标、原始头/正文限额、名称与权限输入、脱敏错误与存储失败、schema 和 OpenAPI 一致性。

`tests/test_operations.py`：5 个用例。覆盖 CLI 初始化与 0600 令牌交付、不覆盖文件、交付/外键写失败回滚、撤权与读事务快照边界、拒绝误用同版本桌面数据库、后端不进入桌面构建。

RED：新测试首次执行因 `pixcake_cloud` 尚不存在而收集失败。实现后仅 OpenAPI 快照缺失失败；导出并检查五个端点、Bearer 安全声明及统一错误模型后通过。后续增加数据库专属标识，补充同版本外部数据库不可被采用的回归。

项目根目录执行：

```sh
uv sync --project backend/cloud --locked
backend/cloud/.venv/bin/python -m pytest backend/cloud/tests -q
cmake --build build-cloud-space --target architecture_gate -j4
ctest --test-dir build-cloud-space \
  -R '^(test_cloud_service|test_http_cloud_gateway|test_cloud_dependencies|arch_dependencies)$' \
  --output-on-failure -j2
```

最终：`29 passed, 1 warning in 1.24s`。提示为 Starlette TestClient 的 HTTPX 兼容入口弃用，不是测试失败，后续升级需处理。CTest 为 `100% tests passed out of 4`，1.30 秒；构建门禁通过。没有声称全仓测试或全产品验收通过。

## 真实 HTTP 验证

在 `backend/cloud` 执行：

```sh
.venv/bin/python -m tools.smoke_http
```

脚本在临时目录初始化服务端数据库，创建两个账号、一个团队和三个项目摘要；启动独立 Uvicorn 进程，只监听 `127.0.0.1`。使用 HTTPX 发出实际 TCP/HTTP 请求，验证未认证 401、空间目录、三页项目、跨账号 403、退出 204 与失效令牌 401，结束后停止服务并清理数据。输出：`HTTP smoke passed: live server, identity isolation, 3 pages, logout persistence.`

沙箱首次禁止绑定 socket（Operation not permitted）；经执行环境授权后完成本机验证。未访问外部云 API、未创建云资源、未上传照片。数据库专属标识补充后再次执行通过。

## 限制与后续

- 本轮使用本地可信管理命令创建测试身份。没有真实短信供应商、验证码登录/注册/刷新 API，不能把这些命令接成产品公开登录。
- HTTP 冒烟只验证本机回环；TLS、入口限流/慢请求超时、生产镜像/运行时补丁锁定、漏洞审计、备份恢复、Linux/Windows 实机与多实例数据库仍未验收。
- 桌面仍缺生产 codec/transport；本机服务器运行不代表桌面云空间页面已接通，也不会放宽桌面 HTTPS 规则。
- 项目只是版本 0 的元数据，未实现原图/参数快照上传、对象存储、下载落库、冲突提交或持久化传输任务。
