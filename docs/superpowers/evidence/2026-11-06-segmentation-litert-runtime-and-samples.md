# LiteRT 运行时隔离验证与样片语义核对

日期：2026-11-06  
状态：隔离验证完成；LiteRT 尚未写入生产 CMake。  
决定来源：用户 2026-11-06 选择“引入运行时直接运行官方 `.tflite`，不做离线转换”，理由是便于扩展和更新模型。BiSeNet 继续使用已验证的 ONNX Runtime 1.30.0。

## 1. LiteRT 来源与锁定

| 项 | 值 |
|---|---|
| 仓库 | <https://github.com/google-ai-edge/LiteRT>（TFLite 的官方后继仓库） |
| 版本 | tag `v2.3.2`，commit `d30595a09f74b4c4c4f669eafeb26928a92deb5a`（2026-11-04） |
| 许可证 | Apache-2.0（仓库 `LICENSE`） |
| 构建入口 | `tflite/CMakeLists.txt`，目标 `tensorflow-lite`（静态库） |
| 构建参数 | `-DCMAKE_BUILD_TYPE=Release -DTFLITE_ENABLE_XNNPACK=ON -DTFLITE_ENABLE_GPU=OFF`；CMake 4.4.2，Apple clang，arm64 |
| 产物 | `libtensorflow-lite.a` 8.3 MB；连同依赖共 105 个静态库；静态链接的冒烟程序 6.5 MB |

构建时由 LiteRT 自带 CMake 模块按固定版本拉取的依赖（实测 HEAD）：

| 依赖 | 版本 / commit |
|---|---|
| TensorFlow 源码 | tag `v2.21.0-rc0`，`a344c313f75bbe28d00abe63f5d49cd6f233b6d0`（**预发布 tag**，入生产前需评估或改钉 commit） |
| abseil-cpp | `20260526.0`，`5650e9cf76d3be4318d5fa3af38ee483ddfd5e4a` |
| flatbuffers | `v25.9.23`，`187240970746d00bbd26b0f5873ed54d2477f9f3` |
| XNNPACK | `d89ef6669a14db203b3b7935b1b3862cb63fb6df` |
| protobuf | `v3.21.9`，`90b73ac3f0b10320315c2ca0d03a5a9b095d2f66` |
| eigen | `ea13a98decd497a8c5588fb5de71b57bcf10d864` |
| ruy | `3286a34cc8de6149ac6844107dfdffac91531e72` |
| gemmlowp | `16e8662c34917be0065110bfcd9cc27d30f52fdf` |
| cpuinfo | `8ce83db858065145192c97af90cb668ad72a12e9` |
| farmhash | `0d859a811870d10f53a594927d0d0b97573ad06d` |
| ml_dtypes | `00d98cd92ade342fef589c0470379abb27baebe9` |
| FXdiv | `63058eff77e11aa15bf531df5dd34395ec3017c8` |
| fft2d | OouraFFT v1.0 tarball（`storage.googleapis.com/mirror.tensorflow.org`） |

注意：
- 依赖均为固定 tag/commit，符合依赖规范“禁止无版本拉取 latest”；但构建期需联网。入生产前应改为项目认可的源码缓存方式，并把各依赖许可证收入 NOTICE。
- `tflite/c/CMakeLists.txt`（C API 动态库）在 LiteRT 新目录结构下的源路径默认值对不上（指向 `<TF_SOURCE_DIR>/tensorflow/lite`），本次未使用；改用 C++ 静态库。
- 链接时必须排除 `tensorflow-src/tensorflow/lite/ios/testdata/**/input.a`（测试数据，不是 Mach-O 归档）。
- 调用方必须使用与库相同的编译宏（`TFL_STATIC_LIBRARY_BUILD`、`TFLITE_BUILD_WITH_XNNPACK_DELEGATE` 等，取自 `CMakeFiles/tensorflow-lite.dir/flags.make`）。

## 2. C++ 冒烟测试（MediaPipe SelfieMulticlass）

模型：`selfie_multiclass_256x256.tflite`，SHA-256 `c6748b1253a99067ef71f7e26ca71096cd449baefa8f101900ea23016507e0e0`（见许可证审查记录）。

模型元数据（用 MediaPipe 自带 flatbuffer schema 读取）：
- 输入 `input_29`：`[1,256,256,3]` float32，RGB；归一化 mean 127.5、std 127.5（即 `[-1,1]`）。
- 输出 `Identity`：`[1,256,256,6]` float32 logits；`SEGMENTER_METADATA` 激活为 SOFTMAX，需调用方做 softmax。
- 只用内置算子，无自定义算子。

结果（Apple Silicon，macOS 27.2，4 线程，10 次热身后取值）：

| 配置 | 首次 | 热运行最佳 / 平均 |
|---|---|---|
| XNNPACK 默认委托（执行计划 7 个节点） | 33–41 ms | 31.9 / 32.5 ms |
| 不用委托（参考内核，175 个节点） | 85 ms | 52.6 / 56.8 ms |

两种配置输出 logits 最大绝对差 `1.95e-05`，逐像素类别 100% 一致。

## 3. 与官方 MediaPipe 的一致性

参照：官方 Python 包 `mediapipe 1.2.2` 的 `ImageSegmenter`（IMAGE 模式）。我们：缩放到 256×256 → 归一化 → C++ LiteRT → softmax → 双线性放大回原图 → argmax。

| 样片 | 尺寸 | 原图尺寸类别一致率 | 置信度平均绝对差 |
|---|---|---|---|
| 脸部特写 | 1024×1024 | 99.03% | 0.0035 |
| 半身演讲 | 2000×1333 | 99.20% | 0.0031 |
| 四人合影 | 1200×900 | 98.57% | 0.0053 |
| 侧身撩发 | 898×1056 | 98.93% | 0.0043 |

把同一张 256×256 图同时交给官方和我们（双方都不缩放）：四张样片**类别一致率 100%、置信度差 0.0**。结论：运行时、归一化、softmax 与官方完全一致；剩余约 1% 差异只来自 MediaPipe 内部的缩放实现，集中在边缘。更换 PIL/OpenCV 缩放或先放大 logits 再 softmax，一致率变化不超过 0.13 个百分点。

## 4. 样片语义核对

样片：上游 face-parsing、SCHP 仓库自带演示图 3 张 + 项目登录页插图 1 张；只在本机临时目录使用，不入仓库。**缺口：没有深肤色样片，没有复杂发丝背景、逆光样片。**

### MediaPipe SelfieMulticlass

- `face-skin` **覆盖整张脸**，包括眉、眼、鼻、唇、牙齿。它对应项目的“面部”，不是“面部皮肤”。
- `others` 在四人合影中只标出了一副眼镜；挂绳、胸牌归入 `clothes`。
- `clothes` 覆盖整身衣物（上衣、裤子、外套），不区分上下装。
- `body-skin` 覆盖脖子、手、手臂。
- 整图直接输入即可，单人、多人、半身、全身都能用。

### BiSeNet（face-parsing v0.0.2，ONNX）

- 脸部特写上：`skin` 不含眉、眼、鼻、唇；`nose` 单独一类；上下唇、眉、耳分开，结果干净。
- 整张半身图直接输入：失效（手被判为皮肤，出现误判的眼镜、帽子）。
- 脸部裁剪后：效果好，但贴在头上的手被判为头发；裁剪框偏离时出现误判的帽子。
- 结论：BiSeNet **必须配人脸检测**给出准确裁剪框，只在脸部区域内使用。

## 5. 映射结论

- MediaPipe：`face-skin` → 面部（supported）；面部皮肤、全部皮肤不能由 MediaPipe 单独给出（union 会带上五官）→ unsupported。
- 面部皮肤可由 BiSeNet `skin` + `nose` 派生（鼻子属于皮肤；需确认项目“面部皮肤”是否包含鼻子），前提是有人脸检测裁剪。
- 详细表格见 `2026-11-06-segmentation-label-mapping.md`。

## 6. 下一步

1. 人脸检测：候选 MediaPipe Face Detector（BlazeFace，`.tflite`，可直接跑在已验证的 LiteRT 上），需要按同样流程审查许可证并锁定模型。
2. LiteRT 进生产 CMake：源码缓存方式、`v2.21.0-rc0` 预发布依赖的处置、NOTICE 汇总、`third_party` 锁定文件和锁定检查测试。
3. 补样片：深肤色、逆光、复杂背景发丝、多人遮挡。
4. 边缘：256×256 放大到全分辨率的发丝边缘质量，导出前需要边缘细化方案。
