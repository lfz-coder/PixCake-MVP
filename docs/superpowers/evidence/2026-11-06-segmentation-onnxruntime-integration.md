# ONNX Runtime 接入与人像组合分割

日期：2026-11-06  
状态：ONNX Runtime 与 BiSeNet 已接入 `renderd`（总开关 `PIXCAKE_ENABLE_INFERENCE`，默认关闭）；`PortraitSegmenter` 可输出 10 个部位；尚未接入预览/导出路径（计划 Step 6）。平台范围：只验证 macOS arm64（用户 2026-11-06 确认只需 macOS）。

## 1. ONNX Runtime

| 项 | 值 |
|---|---|
| 来源 | 官方 release 包 `https://github.com/microsoft/onnxruntime/releases/download/v1.30.0/onnxruntime-osx-arm64-1.30.0.tgz` |
| SHA-256 | `6ebb5062a934537c352937821f9fe9718e7de1a2db1122a93dd363ffd53a7012`（2026-11-06 重新下载核对一致） |
| 包内版本 / commit | `1.30.0` / `f2c39fe2f838cf35ce7da92824f5a5e3ee6e88a7`（配置时校验 `VERSION_NUMBER`） |
| 许可证 | MIT（`third_party/onnxruntime/LICENSES/LICENSE.txt`）；包内 `ThirdPartyNotices.txt` 按 SHA-256 校验后复制到 `<build>/third_party_notices/onnxruntime/` |
| 链接方式 | 动态库 `libonnxruntime.1.30.0.dylib`（install name `@rpath/libonnxruntime.1.dylib`），项目自定义导入目标 `pixcake_onnxruntime` |

为什么不用包自带的 `lib/cmake` 配置：它把头文件目录写成 `include/onnxruntime`，而包里头文件实际在 `include/`。

`cmake/PixcakeOnnxRuntime.cmake` 在非 macOS arm64 上直接配置失败，不会悄悄退化成没有运行时的构建。

## 2. BiSeNet 模型

| 项 | 值 |
|---|---|
| 来源 | `https://github.com/yakhyo/face-parsing/releases/download/v0.0.2/resnet18.onnx`（release v0.0.2，commit `5a8613c`） |
| SHA-256 | `0d9bd318e46987c3bdbfacae9e2c0f461cae1c6ac6ea6d43bbe541a91727e33f`（与 PoC 记录一致） |
| 输入 / 输出 | `input [N,3,512,512]` float；三个输出，只计算第一个 `output [N,19,512,512]`（另两个是训练辅助分支） |
| 许可 | 代码与 release 为 MIT；权重用 CelebAMask-HQ 训练，该数据集只允许非商业研究/教育用途。项目不商用（用户 2026-11-06），记录在 `models.lock.cmake` 的 `USE_TERMS`；改为商用前必须重新审计 |

注：`runtime-probe` 记录里的 `e9d7c9…` 是 SCHP 的权重，不是 BiSeNet，两份记录并不矛盾。

## 3. 组合流程（`PortraitSegmenter`）

1. MediaPipe SelfieMulticlass（LiteRT）→ 头发、身体皮肤、整张脸、衣服、配饰的概率蒙版。
2. BlazeFace 全距离版（LiteRT）→ 人脸框。
3. 每张脸：以框中心取正方形、边长 = 框长边 × 2.0，图外部分填黑 → XNNPACK 缩放到 512×512 → ImageNet 归一化 → BiSeNet（ONNX Runtime）→ 19 类 softmax → 只保留映射需要的 8 类 → 缩放回裁剪尺寸 → 与全图蒙版取最大值。
4. 没有检测到人脸时，8 张面部蒙版仍然存在且全为 0（部位受支持，只是这张照片里没有）。
5. 按 `portrait_rules()` 交给通用适配器校验。

支持的 10 个部位：

| 部位 | 来源 |
|---|---|
| 全部人物 | MediaPipe `hair`+`body-skin`+`face-skin`+`clothes`+`others` |
| 头发 | MediaPipe `hair` |
| 身体皮肤 | MediaPipe `body-skin` |
| 面部 | MediaPipe `face-skin`（整张脸） |
| 全部衣服 | MediaPipe `clothes` |
| 面部皮肤 | BiSeNet `skin`+`nose`（含鼻子，用户确认） |
| 全部皮肤 | MediaPipe `body-skin` + BiSeNet `skin`+`nose` |
| 眉毛 | BiSeNet 左右眉 |
| 嘴唇 | BiSeNet 上下唇 |
| 耳朵 | BiSeNet 左右耳 |

不支持（各有诊断文字）：脖子（裁剪可能截断）、眼白、眼瞳（BiSeNet 只有整只眼）、牙齿、口腔（只有整个嘴）、头纱/帽子（样片误报）、首饰（耳环/项链不覆盖全部首饰）、上衣、内搭、连体衣、下装、手套、鞋子、背包、其他物品。

## 4. 验证（macOS 27.2，Apple Silicon，4 线程）

`tests/apps/test_portrait_inference.cpp`，测试图为 MediaPipe 官方的 `portrait.jpg`（深肤色、露齿笑）：

| 部位面积占比 | Python 参考（同一裁剪、softmax > 0.5） | C++ |
|---|---|---|
| 面部皮肤 | 0.04744（用同一个检测框） | 0.04743 |
| 眉毛 | 0.00178 | 0.00179 |
| 嘴唇 | 0.00179 | 0.00180 |
| 耳朵 | 0.00424 | 0.00425 |

（测试断言使用官方检测框得到的参考值 0.0472 / 0.00154 / 0.00179 / 0.00395 加容差。）

另测：鼻尖属于面部皮肤；两只眼睛附近 15 像素内都有被挖掉的眼睛区域（BlazeFace 眼睛关键点是近似值，本图一个点落在下眼睑上，约差 10 像素）；裁剪框外为 0；空白图无人脸、10 个部位仍齐全、面部部位全为 0；模型缺失/损坏/类型用错、输入形状不符、超大人脸裁剪被拒绝。

耗时：820×1024 整张约 500 ms（MediaPipe 约 135 ms、检测约 6 ms、BiSeNet 一张脸约 350 ms，CPU）。

测试结果：`PIXCAKE_ENABLE_INFERENCE=ON` 77/77 通过；默认关闭 75/75 通过。

过程中修掉的问题：`Ort::Session::GetInputTypeInfo(0).GetTensorTypeAndShapeInfo()` 返回的信息借用临时 `TypeInfo`，语句结束即悬空，导致把 float 模型误判为非 float；改为先保存 `TypeInfo`。

## 5. 尚未完成

1. 预览和导出路径调用（Step 6），包括取消、超时、内存上限。2400 万像素原图时，13 张全图原始蒙版加 10 张结果蒙版约 550 MB，预览应在缩小图上运行，导出再决定是否全分辨率重算。
2. BiSeNet 目前只用 CPU；可试 ONNX Runtime 的 CoreML 执行提供者（PoC 时 CPU/CoreML 输出一致）。
3. 安装包流程（当前没有）：`libonnxruntime` 动态库与 `third_party_notices/` 需要进入 `.app`。
