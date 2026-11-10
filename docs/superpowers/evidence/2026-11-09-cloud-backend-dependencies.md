# 云后端依赖选型与锁定记录

日期：2026-11-09
范围：独立 `backend/cloud` 服务，非桌面运行时。选型遵循[第三方依赖规范](../specs/2026-11-06-pixcake-third-party-dependency-policy.md)。

## 候选与结论

| 能力 | 比较的候选与官方资料 | 本阶段选择与理由 |
| --- | --- | --- |
| API、校验、schema | [FastAPI](https://fastapi.tiangolo.com/features/) / [Django](https://www.djangoproject.com/start/overview/) | FastAPI：当前需要少量 API 和 OpenAPI，不需要 Django 的模板/管理站点；Pydantic/Starlette 作为其必要依赖统一锁定，不手写 JSON/HTTP 解析。 |
| ASGI 运行器 | [Uvicorn](https://uvicorn.dev/settings/) / [Hypercorn](https://hypercorn.readthedocs.io/en/latest/) | Uvicorn 基础包：本地 HTTP/1 验证只需 h11/asyncio，不引入 HTTP/2/WebSocket 运行时扩展。 |
| 签名游标 | [ItsDangerous](https://itsdangerous.palletsprojects.com/en/stable/url_safe/) / [PyJWT](https://pyjwt.readthedocs.io/en/stable/) | ItsDangerous：单服务不透明分页游标，无 JWT 联邦认证需求；使用库签名并绑定业务上下文，不手写密码学。 |
| 数据库 | [SQLite](https://www.sqlite.org/whentouse.html) / [PostgreSQL](https://www.postgresql.org/about/) | Python 标准库 sqlite3，仅用于单机目录服务起步；并发写/多实例与高可用部署前重新落实 PostgreSQL 迁移，不能将本轮结果当作规模化验收。 |
| 随机令牌与摘要 | [Python secrets](https://docs.python.org/3.12/library/secrets.html)、hashlib | 使用标准库提供的系统 CSPRNG 和 SHA-256；不生成自定义随机算法或自行实现加密。 |
| 测试请求 | [HTTPX](https://www.python-httpx.org/) / Python urllib | HTTPX 支持 FastAPI 测试客户端；运行端点联调用其关闭重定向和代理继承的 HTTP 客户端。 |
| 测试执行 | [pytest](https://docs.pytest.org/en/stable/) / unittest | pytest 提供隔离数据库夹具、参数化边界与异常断言；仅 dev 依赖。 |

## 版本与获取

官方 PyPI 元数据已通过 TLS 验证读取：FastAPI 0.162.2（MIT）、Uvicorn 0.54.0（BSD-3-Clause）、ItsDangerous 2.2.0（[BSD-3-Clause](https://itsdangerous.palletsprojects.com/en/stable/license/)）；测试 HTTPX 0.28.1（BSD-3-Clause）、pytest 9.2.0（MIT）。直接版本固定在 `pyproject.toml`，传递依赖及发行包 SHA-256 由 `uv.lock` 固定。使用官方 `https://pypi.org/simple`，不使用未知镜像或禁用 TLS 验证。

Python 运行范围限制为 3.12，开发机使用 3.12.10，内置 SQLite 3.49.1；部署前须选定已维护的补丁版和镜像摘要并复验，本轮不宣称已完成生产镜像锁定。服务代码不使用 macOS 专用 API；Linux/Windows 实机验证仍待执行。Pydantic 的编译扩展由匹配平台 wheel 提供，不依赖手工修改全局 Python 环境。

## 运行与更新边界

- 隔离环境安装，`uv sync --locked`；代码/锁文件不一致即失败，不在服务启动时追踪 latest。
- 同步数据库操作由 ASGI 同步端点执行，每个请求独立连接；短事务、外键与索引，SQLite busy 超时 2 秒。默认单 worker。
- 最大 100 条/页、字符串/原始请求限额、限时签名游标；默认关闭访问日志和公开 schema 页面，凭证不进入异常响应。
- 升级依赖时重新锁定、检查传递许可证、运行 API/事务/资源限制回归；发布前还需漏洞审计、TLS/限流和备份恢复验证。
- 运行环境须保留发行包内许可证文件；完整直接/传递依赖、版本、运行/测试用途与已核对的许可文件见[THIRD_PARTY.md](../../../backend/cloud/THIRD_PARTY.md)。

本轮 `uv sync --locked --offline` 成功，证明锁文件可复核；未将缓存或虚拟环境加入版本控制。Starlette 1.10.0 的 TestClient 对 HTTPX 兼容入口发出弃用提示，当前测试通过；后续依赖升级应迁移测试客户端，不隐瞒该提示。
