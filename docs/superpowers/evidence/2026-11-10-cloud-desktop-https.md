# 桌面云目录 API v1 / HTTPS 验收

日期：2026-11-10
范围：客户端阶段 2B1；CLD-14–18。后端为本项目 B1a 的真实目录服务，本次完成服务层接入，未接正式登录或云空间页面。

## 实现

- `BackendV1Codec` 编码空间/项目 GET 查询与 Bearer，按 API v1 解析 JSON；拒绝未知版本/字段/权限、重复 key、坏 UTF-8、非法整数和超限结构，错误清空输出。跨页 ID/空间语义仍由 `CloudService` 统一验证。
- `CurlHttpTransport` 为独立可选目标：libcurl URL API 固定 HTTPS scheme/host/port，验证证书和主机名，禁用重定向/环境代理/netrc，接收回调执行累计头部和解压后正文上限。设置包括准备时间的剩余 I/O 超时，失败丢弃状态/头部/正文。
- `PIXCAKE_ENABLE_CLOUD_HTTP=ON` 才启用 JSON codec 与 curl 目标。JSON 固定 3.12.0 单头/许可证 SHA-256，HTTPS 只链接 macOS SDK 系统 curl，详见[依赖记录](2026-11-09-cloud-desktop-dependencies.md)。未新增桌面 Python 运行时。
- 架构检查约束目录层、协议层、具体 HTTP 适配层，禁止公共头暴露 JSON/curl，禁止 kernel 引用这些协议或传输目标。29 个门禁样例（4 正、25 负）通过。

## 环境与执行结果

macOS 27.2/26B123、arm64，AppleClang 21.0.0、SDK 27.2、CMake 4.4.2；SDK curl 头为 8.7.1。联调二进制为 arm64，`otool -L` 显示系统 `/usr/lib/libcurl.4.dylib`。后端 Python 3.12、锁定的 FastAPI/Uvicorn 环境。

| 检查 | 结果 |
| --- | --- |
| 开启 HTTP 的 Debug 构建 | codec、curl、HTTPS 联调程序和共享网络使用者编译通过；项目 `-Wall -Wextra -Werror` 生效 |
| 客户端专项/回归/门禁 | 10/10 通过：account_session、cloud_service、http_cloud_gateway、backend_v1_codec、curl_http_transport、cloud_dependencies、generated、generation_adapter、arch_dependencies、third_party_locks_T22 |
| `architecture_gate` 构建目标 | 通过 |
| 全新关闭 HTTP 的构建 | 无 JSON 下载/具体网络目标；cloud_service、http_cloud_gateway、cloud_dependencies、arch_dependencies 共 4/4 通过 |
| 后端测试 | 29 通过；1 条现有 Starlette/HTTPX 测试客户端弃用提示 |
| 真实 TLS 集成 | `tools/smoke_cpp_https.py` 启动真实后端并运行实际 C++ 服务链，通过 |

JSON 单测验证 100 项合法页、101 项拒绝、2 MiB 正文上限、结构深度/节点/字段/字符串限制；测试错误字段放在第二项，确认不泄露前面成功解析的部分。请求预检查用无网络用例验证跨来源、HTTP、userinfo、fragment、头注入/重复、保留头、方法、超时及限额拒绝。

TLS 集成使用临时 CA 与单独签发的 SAN=127.0.0.1 服务端证书。测试证书初版主体配置导致验证拒绝，修正为不同的 CA/叶证书主体后通过；全过程保持证书校验开启，没有使用 insecure 开关。临时凭证经 0600 文件交给 C++，不放在 argv/URL/日志；不修改系统钥匙串，结束时关闭子进程并清理目录。

真实服务链覆盖：

1. 每页 1 项，获取个人/团队两个空间和三个中文项目；不同账号目录隔离，跨团队请求 403，未知令牌 401。
2. 默认不信任临时 CA 时失败；显式 CA 成功；把来源改为 localhost 后，因证书仅有 IP SAN 而失败。
3. 302 保留原响应，服务端目标计数仍为 0，证明没有跟随。
4. 普通与 gzip 正文均成功解压到 256 KiB；把接收上限降至 1 KiB 均中止且无部分内容；头部数量/字节超限同样拒绝。
5. 400ms 延迟请求受 75ms 客户端预算限制；两页各延迟 120ms 时，180ms 整体查询预算使第二页失败并保留旧目录。另验证 503 同样保留旧目录。
6. 真实返回的重复 JSON key 被 codec 拒绝；`DELETE /v1/session` 返回 204，下一次服务刷新得到 PermissionDenied 并清除目录权限。

故障端点仅注册在 `backend/cloud/tools/https_fixture.py` 的测试应用，生产应用用例确认相应路径返回 404。所有本地 socket 测试在获准的回环环境执行，不对外监听。

## 复现

仓库根目录：

```sh
cmake -S . -B build-cloud-space -DCMAKE_BUILD_TYPE=Debug -DPIXCAKE_ENABLE_CLOUD_HTTP=ON -DPIXCAKE_ENABLE_INFERENCE=OFF
cmake --build build-cloud-space --target test_backend_v1_codec test_curl_http_transport test_cloud_https_integration test_http_cloud_gateway test_cloud_service test_account_session test_generated test_generation_adapter architecture_gate -j 4
ctest --test-dir build-cloud-space -R '^(test_(backend_v1_codec|curl_http_transport|http_cloud_gateway|cloud_service|account_session|cloud_dependencies|generation_adapter|generated)|arch_dependencies|third_party_locks_T22)$' --output-on-failure
```

`backend/cloud` 中：

```sh
uv sync --locked
uv run --locked python -m pytest -q
uv run --locked python -m tools.smoke_cpp_https ../../build-cloud-space/tests/test_cloud_https_integration
```

## 尚未验收

真实短信与刷新、从服务器取得并提交可信账号快照、正式应用后台线程和 UI 编排、线上部署、Windows/鸿蒙、凭证持久化、上传下载/版本提交和对象存储均未完成。当前目录条目是服务端真实保存的元数据，不代表照片已上传。同步服务不能直接放入 UI 事件线程；正式应用链接时还需把第三方许可装入应用包。
