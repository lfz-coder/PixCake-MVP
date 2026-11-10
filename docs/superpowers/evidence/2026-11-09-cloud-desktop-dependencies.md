# 桌面云 HTTPS 与 JSON 依赖记录

日期：2026-11-09
范围：客户端阶段 2B1，独立网络适配目标与后端 API v1 codec；不向内核加入网络或 JSON 类型。

| 能力 | 候选与官方资料 | 选择及边界 |
| --- | --- | --- |
| HTTPS / URL 解析 | [libcurl](https://curl.se/libcurl/) / [Apple URLSession](https://developer.apple.com/documentation/foundation/urlsession) | libcurl 的 URL API、接收回调和整体超时符合现有同步 `HttpTransport`。首期使用 macOS SDK/系统提供的 libcurl；URLSession 需要额外 Objective-C 异步桥接，本轮不引入。 |
| JSON | [nlohmann/json](https://json.nlohmann.me/integration/cmake/) / [Boost.JSON](https://www.boost.org/doc/libs/latest/libs/json/doc/html/index.html) | 选 nlohmann/json 3.12.0 单头文件及公开 SAX 接口；不新增 Boost 编译组件，不手写 JSON 解析器。 |

## 版本、来源与许可

- nlohmann/json 固定 [v3.12.0 官方发布](https://github.com/nlohmann/json/releases/tag/v3.12.0)，MIT。官方单头文件 SHA-256 为 `aaf127c04cb31c406e5b04a63f1ae89369fccde6d8fa7cdda1ed4f32dfc5de63`；固定标签的头与许可分别记录在 `third_party/nlohmann-json.lock.cmake`，CMake 下载时验证，许可复制到构建产物的 `third_party_notices/nlohmann_json/`。无外部传递运行库。
- macOS 当前工具链：SDK 27.2、SDK libcurl 头版本 8.7.1；运行环境 macOS 27.2/26B123、arm64。只链接 SDK 的系统库声明，不从 Homebrew 寻找任意 curl/TLS 版本，不随桌面重分发系统 dylib。系统依赖由 Apple 更新，不能以其版本号推断已完成漏洞审计。
- 本机系统 curl 报告 SecureTransport、LibreSSL 3.3.6、zlib 1.2.12、nghttp2 1.69.0；这些由 OS 提供，本轮不打包。curl 许可见[官方版权说明](https://curl.se/docs/copyright.html)。发布前仍需核实受支持 OS、安全更新、许可证和签名要求。
- 当前只实现 macOS 系统库构建路径。其他平台打开该可选功能时明确配置失败，不静默使用不受控的 TLS 库。Windows/鸿蒙实现需另行固定依赖和实机验证。

## 构建与运行契约

- `PIXCAKE_ENABLE_CLOUD_HTTP` 显式开启网络实现；普通目录服务/测试仍不需要外部 JSON 下载。目标 `pixcake_http_curl` 拥有具体 HTTP 库，cloud codec 仅知道 `HttpRequest/HttpResponse`。
- 仅 HTTPS，固定 scheme/host/port 来源；URL 完整语法由 libcurl 处理。强制证书链/主机名验证，支持显式 CA 文件以测试私有 CA，不提供关闭校验开关。
- 不自动跟随重定向，不继承环境代理、netrc 或 cookie 凭证；协议只允许 HTTPS，输入有界，认证只来自调用方显式 Header。
- `CURLOPT_TIMEOUT_MS` 执行整体 I/O 预算；运行时要求异步 DNS 支持，避免同步解析绕过时限。回调在扩展应用正文/头部缓冲前检查限额，正文限制包括自动解压后的字节。
- JSON 输入最多 2 MiB，公开 SAX 回调限制深度/节点/集合和字符串；只接受 API v1 的已定义字段及类型，拒绝重复 key、坏 UTF-8、整数溢出和未知权限。无效响应不产生部分结果。
- 失败通过既有 `ResultCode` 返回，不输出请求、响应、凭证、游标或第三方错误文本。

实际构建/联调结果见 [2026-11-10 验收记录](2026-11-10-cloud-desktop-https.md)。启用和关闭开关均已构建验证；联调可执行文件为 arm64，`otool -L` 确认使用 `/usr/lib/libcurl.4.dylib`，未带入 Homebrew TLS 库。选型与本机联调不等于生产部署完成。正式应用将来链接这些目标时，必须把构建输出中的 MIT 许可纳入应用包的许可目录。
