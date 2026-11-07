# .app 里带上 renderd、exportd、推理运行库与模型（2026-11-07）

此前 .app 里只有壳层。renderd 和 exportd 从构建目录启动（`PIXCAKE_RENDERD_DEV_PATH` / `PIXCAKE_EXPORTD_DEV_PATH`），onnxruntime 动态库的查找路径也写死在构建目录里，所以离开构建目录就不能预览，也不能导出。

## 做法

`cmake/PixcakeBundle.cmake` 新增 `pixcake_app_bundle` 目标，属于默认构建（ALL），依赖壳层、renderd 和 exportd。它负责把下面这些组装进 .app：

| 位置 | 内容 | 谁用 |
| --- | --- | --- |
| `Contents/MacOS/` | `pixcake_app_renderd`、`pixcake_app_exportd` | 壳层的 `renderd_executable()` / `exportd_executable()` 先找这里，找不到才退回开发路径 |
| `Contents/Frameworks/libonnxruntime.1.dylib` | 锁定版本的 ONNX Runtime（只在推理构建） | 两个进程多了一条查找路径 `@executable_path/../Frameworks`，并且排在构建目录那条之前 |
| `Contents/Resources/models/` | 三个锁定的模型（只在推理构建） | `model_directory_near_executable()` 先找这里 |
| `Contents/Resources/third_party_notices/` | LiteRT 及其依赖、ONNX Runtime、模型的许可证文本（只在推理构建） | 发布时随包附带 |

组装步骤放在顶层的最后（`include(cmake/PixcakeBundle.cmake)`），因为模型目录等变量要等推理配置加载后才有。

**没做的**：Qt 框架没有打进包里（这是发布时 macdeployqt 的事）；整个 .app 没有重新签名（开发构建只有链接器给的临时签名）。

## 验证（M2 Pro）

- **移动后的 .app**：把推理构建的 .app 复制到构建目录之外，然后：
  - 用 `DYLD_PRINT_LIBRARIES` 跑 `pixcake_app_exportd --health` 和 `pixcake_app_renderd --health`，两者都从包内 `Contents/Frameworks` 加载 onnxruntime，模型目录也报包内的 `Contents/Resources/models`；
  - 从这个 .app 跑壳层的导入加导出冒烟：精修、缩略图、导出（0.10 MB 限制，104230 字节）全部通过，exportd 日志的 `exportd.segmentation` 显示用的是包内模型。
- **`test_app_bundle`**（`tests/apps/check_app_bundle.cmake`）：
  - 默认构建检查两个进程都在包里；
  - 推理构建另外检查动态库、三个模型和许可证文本都在，并检查两个进程从包内加载动态库、使用包内模型。
- **变异检查**：从包内 exportd 删掉 `@executable_path/../Frameworks` 这条查找路径并重新签名，测试失败；重新组装后恢复通过。
- **全量测试**：默认构建 91/91，推理构建 94/94，Release 推理 94/94。
