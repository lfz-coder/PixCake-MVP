# LiteRT 正式接入 `renderd`

日期：2026-11-06  
状态：已接入项目构建（开关 `PIXCAKE_ENABLE_INFERENCE`，默认关闭；原名 `PIXCAKE_ENABLE_LITERT`，2026-11-06 加入 ONNX Runtime 后改为总开关）；人物分割与人脸检测可在 `renderd` 内调用；尚未接入预览/导出路径（计划 Step 6）。

## 1. 接入方式

| 文件 | 作用 |
|---|---|
| `third_party/litert.lock.cmake` | LiteRT v2.3.2（commit `d30595a`）与其依赖的 TensorFlow 源码（`v2.21.0-rc0`，commit `a344c31`）的源码包 URL、SHA-256；传递依赖的版本和许可证清单 |
| `third_party/models.lock.cmake` | 两个模型和一张测试图的固定版本 URL、SHA-256、许可证 |
| `third_party/litert/LICENSES/LiteRT-LICENSE.txt`、`third_party/models/LICENSES/Apache-2.0.txt` | 随包许可证文本（SHA-256 记在锁定文件里） |
| `cmake/PixcakeLiteRT.cmake` | 下载并校验源码包 → 把 LiteRT 的 `tflite/` 作为子目录构建；把 16 份依赖许可证收集到 `<build>/third_party_notices/litert/`，缺一份就配置失败；下载并校验模型 |
| `CMakeLists.txt` | `option(PIXCAKE_ENABLE_INFERENCE ... OFF)` |
| `apps/renderd/CMakeLists.txt` | 开关打开时构建 `pixcake_renderd_inference`；只有它私有链接 `tensorflow-lite`、`XNNPACK` |

为什么默认关闭：首次配置要下载约 300 MB 锁定源码并编译 LiteRT，会拖慢日常构建，也会让并行会话的 `build/` 突然需要联网。发布构建需要打开它。

## 2. 代码

| 文件 | 内容 | 依赖 |
|---|---|---|
| `face_detection.hpp/.cpp` | BlazeFace 全距离版后处理（锚框、加权 NMS、信箱还原、裁剪框） | 无，始终编译 |
| `segmentation_models.hpp/.cpp` | MediaPipe 标签 → 项目部位映射表 | 无，始终编译 |
| `litert_model.hpp/.cpp` | `LiteRtModel`（加载、形状检查、推理），`resize_bilinear_u8`（XNNPACK 双线性缩放） | 头文件不暴露 LiteRT 类型；只有 `.cpp` 包含 `tflite/`、`xnnpack.h` |
| `mediapipe_inference.hpp/.cpp` | `MediaPipeSelfieSegmenter`（实现 `SemanticSegmenter`），`BlazeFaceDetector` | 经 `LiteRtModel` 间接使用 |

缩放用 XNNPACK 的 `xnn_create_resize_bilinear2d_nhwc`（u8，半像素中心），不手写缩放，符合依赖规范 §1。

错误映射：模型文件读不到 → `IoFailed`；不是合法模型、算子缺失、张量不是 float32 或形状不符 → `UnsupportedFormat`；输入尺寸/缓冲区不对 → `StateConflict`；推理失败 → `IoFailed`。

## 3. 架构约束

- `tests/arch/check_dependencies.cmake` 规则 15：kernel 的 CMake 不得出现 `tensorflow-lite`、`tflite`、`litert`、`XNNPACK`、`xnnpack`。
- 规则 16：kernel 源码不得包含 `tflite/`、`xnnpack.h`、`onnxruntime_cxx_api.h`。
- `tests/arch/check_third_party_locks.cmake`（T-22）：许可证文本 SHA-256、源码包和模型 URL 必须是 https 固定版本（含 `/latest/` 即失败，已实测）、SHA-256 必须是 64 位十六进制、URL 必须包含锁定的 commit、发布阻塞项为空。

## 4. 验证（2026-11-06，Apple Silicon，macOS 27.2，CMake 4.4.2）

全新构建目录，`-DPIXCAKE_ENABLE_INFERENCE=ON`：
- 配置 1 分 56 秒（含下载、校验）；编译 LiteRT + 测试 1 分 10 秒（10 并行）。
- LiteRT 目标以 `-O3 -DNDEBUG` 编译；项目自己的目标和测试不带 `NDEBUG`，`assert` 生效。
- `ctest`：76/76 通过（默认关闭时 75/75）。

`tests/apps/test_litert_inference.cpp`（对照官方 `mediapipe 1.2.2` 在同一文件上的结果）：

| 项 | 官方 | 我们 |
|---|---|---|
| 人脸框 | (290,110) 228×228，分数 0.85 | (287.5,107.9) 231.6×231.6，分数 0.772；IoU ≥ 0.85 |
| 面部占比 | 0.0576 | 0.0567 |
| 头发占比 | 0.0132 | 0.0126 |
| 衣服占比 | 0.4148 | 0.4121 |
| 人物占比 | 0.5088 | 0.5036 |
| 耗时（820×1024，4 线程） | — | 检测 5.3 ms；分割 137 ms（含 softmax、5 张蒙版放大到原图、映射） |

另测：缺失/损坏模型文件、模型类型用错、输入元素数不对、缓冲区过短、空白图无人脸、行填充输入与紧凑输入结果完全一致、XNNPACK 缩放对行填充不敏感。

分数偏差（0.772 对 0.85）与框偏差来自缩放实现不同：在原生 192×192 输入上两边完全一致（见 `2026-11-06-segmentation-face-detection.md` §3）。

## 5. 尚未完成

1. 预览和导出路径调用（计划 Step 6），包括取消、超时、资源上限。
2. ~~ONNX Runtime 进入生产构建，BiSeNet 面部解析接入（Step 5g）~~：已完成，见 `2026-11-06-segmentation-onnxruntime-integration.md`。
3. 平台：只需 macOS（用户 2026-11-06 确认），不再列 Windows/鸿蒙验证。
4. 打包：`third_party_notices/litert/` 需要进入安装包（当前没有安装包流程）。
