# 人脸检测选型与验证：MediaPipe BlazeFace

日期：2026-11-06  
状态：选定 BlazeFace 全距离版；C++ 后处理已实现并有单元测试；LiteRT 推理已接入 `renderd`（开关 `PIXCAKE_ENABLE_INFERENCE`）。  
用途：给 BiSeNet 脸部解析提供裁剪框（BiSeNet 在整图上会误判，见 `2026-11-06-segmentation-litert-runtime-and-samples.md` §4）。

## 1. 候选与许可证

官方文档 <https://developers.google.com/edge/mediapipe/solutions/vision/face_detector> 列出三个模型，均为 `.tflite`、float16：

| 模型 | 输入 | 锚框 | 固定版本 URL（`/1/`） | SHA-256 | 大小 |
|---|---|---|---|---|---|
| 短距离 | 128×128 | 896 | `.../blaze_face_short_range/float16/1/blaze_face_short_range.tflite` | `b4578f35940bf5a1a655214a1cce5cab13eba73c1297cd78e1a04c2380b0152f` | 229,746 |
| **全距离（选定）** | 192×192 | 2304 | `.../blaze_face_full_range/float16/1/blaze_face_full_range.tflite` | `3698b18f063835bc609069ef052228fbe86d9c9a6dc8dcb7c7c2d69aed2b181b` | 1,083,786 |
| 全距离稀疏版 | 192×192 | — | `.../blaze_face_full_range/float16/1/blaze_face_full_range_sparse.tflite` | `2c3728e6da56f21e21a320433396fb06d40d9088f2247c05e5635a688d45dfe1` | 676,746 |

`/1/` 与 `latest` 下载内容逐字节相同（2026-11-06）。

全距离版模型卡（<https://storage.googleapis.com/mediapipe-assets/MediaPipe%20BlazeFace%20Model%20Card%20(Full%20Range).pdf>）：
- “LICENSED UNDER Apache License, Version 2.0”；作者 Valentin Bazarevsky（Google），2021-01-19。
- 训练数据：“trained and evaluated on consented images of people … captured with smartphone cameras”，10 万+ 张，覆盖 17 个地理分区。
- 适用条件：俯仰/侧倾 ≤ 45°，偏航 ≤ 90°；人脸框边长 ≥ 图片对应边的 5%；人脸框至少 70% 在图内。
- 不适用：人群计数、背向/大角度人脸、5 米以外的人、任何监控或身份识别。
- 公平性：各肤色召回 98%–100%，平均 99.2%。

## 2. 实测（官方 Python `mediapipe 1.2.2` 对照）

| 样片 | 短距离（官方） | 全距离（官方） |
|---|---|---|
| 脸部特写 | 1 张脸 | 1 张脸 |
| 半身演讲 | 1 | 1 |
| 四人合影 | **0（全部漏检）** | **4（全部检出）** |
| 侧身撩发 | 1 | 1 |
| MediaPipe 测试图 `portrait.jpg`（深肤色） | — | 1，框 (290,110) 228×228，分数 0.85 |

结论：短距离版面向前置自拍，合影/远景漏检；修图照片多为后置相机，选全距离版。

## 3. 后处理实现与一致性

模型输出是未解码的锚框偏移（`[1,2304,16]`）和分数 logits（`[1,2304,1]`），需要调用方解码。MediaPipe 的后处理在其 C++ 计算图里，必须用 Bazel 构建整个 MediaPipe 框架才能复用，不适合引入；因此按依赖规范 §4 记录手写例外：

- 范围：仅 BlazeFace 全距离版的锚框生成（48×48 网格、每格 1 个、固定尺寸）、sigmoid（分数先截断到 ±100）、加权 NMS（IoU > 0.3 的候选按分数加权平均，保留最高分）、信箱坐标还原。
- 实现：`apps/renderd/src/face_detection.cpp`，不依赖任何运行时。
- 测试向量：`tests/apps/test_face_detection.cpp`（锚点位置、单框解码、加权合并、低分过滤、`max_faces`、信箱还原、畸形张量、NaN 分数、裁剪框）。
- 与官方一致性：把同一张 192×192 图同时交给官方和我们（双方都不缩放），4 张样片**分数完全一致、框位置最多差 1 像素**（取整方式不同）。原图输入时的偏差（框差几到十几像素、分数差 ≤ 0.06）来自官方内部的缩放实现。
- 替换路径：如果 LiteRT/MediaPipe 将来提供可独立链接的后处理库，换掉本实现，测试向量保留。

## 4. 人脸裁剪 + BiSeNet 得到面部皮肤

流程：全距离检测 → 以框中心取正方形、边长 = 框长边 × 2.0 → BiSeNet（ONNX）→ 面部皮肤 = `skin` + `nose`（用户 2026-11-06 确认面部皮肤包含鼻子）→ 贴回原图。

样片结果（倍数 1.6 / 2.0 / 2.4 结果几乎相同，取 2.0）：
- 面部皮肤包含鼻子，不含眉、眼、唇、牙齿。
- 四人合影四张脸全部覆盖。
- 撩发照片里放在头上的手不被算作皮肤（不用检测、手动裁剪时这只手曾被判为头发）。
- 深肤色 `portrait.jpg` 正常。

BiSeNet 仍运行在隔离环境的 ONNX Runtime 上；ONNX Runtime 进入生产构建是下一步。

## 5. 复现脚本

`docs/superpowers/evidence/scripts/2026-11-06-segmentation/`：
- `facedet_compare.py`：官方 FaceDetector 与 C++ LiteRT + 自写解码的对比；
- `face_pipeline.py`、`bisenet_crop.py`：检测 → 裁剪 → BiSeNet → 面部皮肤；
- `compare_mp.py`：分割模型官方对比；`dump_meta.py`：读取 `.tflite` 元数据；`litert_smoke.cc`：C++ 冒烟程序。

样片图片不入仓库。
