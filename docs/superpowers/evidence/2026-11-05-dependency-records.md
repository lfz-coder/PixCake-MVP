# 依赖记录与手写例外：2026-11-05

依据：`docs/superpowers/specs/2026-11-06-pixcake-third-party-dependency-policy.md` §2（引入记录六项）、§3（锁定与可追溯）、§4（手写实现例外）。
对应：第三轮审核 S-33/Q-34、S-34/Q-35。

## 一、Glad OpenGL loader（`third_party/glad`）

锁定文件：`third_party/glad.lock.cmake`

| 规范 §2 项 | 记录 |
| --- | --- |
| 1. 来源、版本、校验、维护 | 由 glad 生成器 2.0.8 生成，生成时间 `Fri Sep 25 08:13:49 2026`，命令行 `--api='gl:core=3.3' --extensions='' c --loader`，选项 `LOADER=True`，其余选项为 False（均取自 `gl.h` 头部）。三个文件的 SHA-256 已写入锁定文件。仓库里是生成后的源码，不是二进制文件。 |
| 2. 平台和工具链 | 只由 `gpu/backend/native` 的 `pixcake_glad` 静态库使用（`gpu/backend/native/CMakeLists.txt`），以 C++ 编译，链接 `OpenGL::GL`。当前只在 macOS 的 CGL Core profile 上验证过（`test_gpu_backend_C57_C60` 实际执行）。Windows 和鸿蒙尚未验证。 |
| 3. 许可证与随包义务 | `gl.h`、`gl.c` 头部的 SPDX 标识为 `(WTFPL OR CC0-1.0) AND Apache-2.0`；`KHR/khrplatform.h` 文件内自带 Khronos 的 MIT 式许可声明。发布时需要随包附带 Apache-2.0 全文，以及选定的 CC0-1.0 或 WTFPL 文本。**这两份文本目前没有放进仓库**（见“阻塞项”）。 |
| 4. 构建方式与 ABI | 直接编译仓库内源码，不在构建时联网获取；只在 `PIXCAKE_GL_BACKEND` 选择 native 后端时参与构建。 |
| 5. 安全与输入 | 只做 OpenGL 函数指针加载，不处理用户文件。函数加载失败时，由 native 后端在创建 context 时返回明确错误（C-57–C-60 合同测试已覆盖）。 |
| 6. 与项目结果码的映射 | 不直接暴露给业务代码；由 `pixcake::gl` facade 和 native 后端把失败转换成项目自己的错误文本和结果。 |

候选比较：OpenGL 函数加载只能二选一，要么用生成器生成，要么手写函数指针表。手写不满足规范 §4，因此按规范 §2 末段说明：这项能力只有“生成的 loader”这一类可接受方案。glad 以外的生成器本轮没有做比较，在下面的阻塞项里补。

**发布前阻塞项**

1. **复现比对**：用 glad 2.0.8 和上面的命令行重新生成，确认除时间戳一行外与仓库文件一致。本轮尝试在 scratchpad 的独立虚拟环境里安装 `glad2==2.0.8`，被会话权限拦下，没有执行。
2. **许可证全文**：本机没有找到 Apache-2.0、CC0-1.0 的原文文件；我没有凭记忆写许可证全文。需要从官方来源取得后放进 `third_party/glad/LICENSES/`。
3. **备选生成器比较**：补一份简短比较，例如与 glad 1.x、GLEW、libepoxy 的对比。

**阻塞项处理结果（2026-11-06，用户授权联网）**

1. **复现比对：已通过。** 在 scratchpad 独立虚拟环境安装 `glad2==2.0.8`（wheel SHA-256 `cfe84018…9d70b`，依赖 Jinja2 3.1.6、MarkupSafe 3.0.4；本机 python.org 版 Python 缺 CA 证书，安装时临时指定 `SSL_CERT_FILE=/etc/ssl/cert.pem`，未改系统）。按锁定命令行加 `--reproducible`（用 wheel 自带的 `gl.xml`）生成：`gl.h`、`khrplatform.h` 与仓库一致（`gl.h` 只差生成时间一行）；`gl.c` 有 9 行不同，是首次导入（`120c53b`）时随带的三处本地改动：`GLAD_UNUSED(&glad_gl_has_extension)`、Linux 分支追加 `libEGL.so.1`/`libEGL.so`、Linux 分支追加 `eglGetProcAddress` 回退。这些改动没有单独记录过，现整理为 `third_party/glad/patches/0001-gl-c-local-adjustments.patch`；“重新生成 + 打补丁”与仓库三个文件逐字节一致（除时间戳行）。对 Khronos 在线规范生成，结果与 `--reproducible` 相同。用项目的 `-Wall -Wextra -Werror` 编译未打补丁的 `gl.c` 也能通过，所以第一处改动不是为了消除警告；三处都不影响 macOS、Windows 代码路径。
2. **许可证全文：已补。** SPDX `(WTFPL OR CC0-1.0) AND Apache-2.0` 取 CC0-1.0 与 Apache-2.0：`third_party/glad/LICENSES/Apache-2.0.txt`（https://www.apache.org/licenses/LICENSE-2.0.txt）、`CC0-1.0.txt`（https://creativecommons.org/publicdomain/zero/1.0/legalcode.txt），2026-11-06 下载，SHA-256 写入锁定文件。目前没有发布的应用链接 `pixcake_glad`（壳层只用 Qt 后端），所以还没有“随包”对象；哪个应用开始链接它，就要把 `LICENSES/` 放进应用包。
3. **备选比较：已补**（GitHub API，2026-11-06）：

| 候选 | 形态 | 许可证（GitHub 识别） | 最近发布 | 是否适合本项目 |
| --- | --- | --- | --- | --- |
| glad 2（Dav1dde/glad） | 按官方规范生成源码，可只生成 `gl:core=3.3` | 生成结果头部写明 SPDX；仓库为多许可（GitHub 记 NOASSERTION） | v2.0.8（2024-09-29，即所用版本） | 采用：源码进仓库、不引入运行时库、可按命令行复现 |
| GLEW（nigels-com/glew） | 运行时库，面向扩展加载 | NOASSERTION | glew-2.3.1（2026-01-24） | 不采用：要多链接一个库，本项目不需要扩展加载 |
| libepoxy（anholt/libepoxy） | 运行时库，自动管理函数指针 | NOASSERTION | 1.5.10（2022-03-18） | 不采用：运行时库依赖，发布节奏慢 |
| gl3w（skaslev/gl3w） | 生成核心档加载器的脚本 | Unlicense | 无发布标签 | 不采用：没有可锁定的版本号，只能锁提交 |

锁定文件 `PIXCAKE_GLAD_RELEASE_BLOCKERS` 已清空。新增 CTest `third_party_locks_T22`（`tests/arch/check_third_party_locks.cmake`）：核对 glad 三个文件、补丁、两份许可证文本的 SHA-256，并要求阻塞项为空；改动许可证文本一个字节即失败（已验证）。

## 二、`kernel/import/src/source_probe.cpp` 手写例外（规范 §4）

| 规范 §4 要求 | 记录 |
| --- | --- |
| 手写的是什么 | 读取 PNG、JPEG、TIFF、BMP 的签名和图像尺寸，用于导入准入（契约 C-11、C-12、B-40、B-90）。**不解码像素，不做色彩管理。** |
| 考虑过的候选 | libvips、OpenImageIO、LibRaw（规范 §5 列出的图像 I/O 方向）。其中 libvips 已在 `third_party/libvips.lock.cmake` 锁定版本，作为 `apps/renderd` / `apps/exportd` 解码和编码适配器的主候选。 |
| 排除理由 | 依赖规范第 13 行：“图像 I/O 依赖应放在 `apps/renderd`、`apps/exportd` 或明确的平台适配器，不能把图像库头文件和实现塞进 `kernel/`”；§3：“公共内核不因为调用方需要而直接链接图像库”。准入发生在内核导入流程里，不能为了读几个字节的文件头引入整套图像库。注意：现有架构门禁只对 `kernel/ledger`、`kernel/layerdoc`、`kernel/workspace` 扫描 `jpeglib.h`（rule 6/13/14），rule 15 只拦 Qt/OpenGL/opencv/cuda/torch/onnxruntime，`kernel/import` 引入图像库目前不会被门禁发现，这条缺口记入第四轮复审。**2026-11-06 已补**：rule 16 扫描整个 `kernel/` 的图像库头文件（png、jpeglib、turbojpeg、tiff、webp、vips、libraw、OpenImageIO、libheif、exiv2），rule 15 增加对应链接目标；用临时违规文件验证 4 种头文件和 `PNG::PNG` 链接都被拦下。 |
| 范围 | 只读文件头里固定位置的字段，读取前先检查长度。宽或高为 0、负数，或宽乘高溢出时拒绝（`source_probe.cpp` 中的尺寸检查）。 |
| 测试向量 | `tests/kernel/import/test_source_probe.cpp`：损坏的 PNG/JPEG/TIFF 判 `ImageCorrupt`；BMP 等不支持的格式判 `UnsupportedFormat`；缺失文件判 `SourceMissing`；拒读判 `PermissionDenied`；以及尺寸为 0 和溢出的用例。`tests/kernel/import/test_import_coordinator.cpp` 覆盖批量导入、准入凭证、准入后文件被替换、准入后文件消失。 |
| 损坏输入处理 | 截断或签名不完整时返回 `ImageCorrupt`；不认识的签名返回 `UnsupportedFormat`；都不进入项目（S-07）。 |
| 替换路径 | `apps/renderd` 的 `ImageDecoder` 接入 libvips 后，由解码适配器做完整内容验证（真实解码），首次打开不能解码的图片标成不可读（B-91）。内核里的这份探测届时只作为快速预筛，不再作为“内容可信”的唯一依据。 |

## 三、本轮同时完成的 S-07 改动

`ProjectLibrary::add_photo` 只接受 `AdmittedSource`。这个凭证由 `image_import::admit_source` 在内容探测成功后签发，构造函数私有。写入前会重新识别文件：文件不存在返回 `SourceMissing`，身份与准入时不同返回 `StateConflict`，两种情况都不写入。见 `docs/superpowers/plans/2026-11-05-import-admission-and-dependency-records.md`。
