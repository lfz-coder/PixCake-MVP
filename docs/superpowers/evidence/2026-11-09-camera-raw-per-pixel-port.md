# Camera Raw 逐像素阶段移植证据

日期：2026-11-09
总裁决：[PhotoCraft 能力来源与 PixCake 移植规范](../specs/2026-11-09-photocraft-reference-and-porting-policy.md)
计划：[Camera Raw 逐像素阶段移植](../plans/2026-11-09-camera-raw-per-pixel-port.md)

## 结论

PhotoCraft Camera Raw 管线的五个逐像素阶段已经移植为 PixCake C++ reference，并与 PhotoCraft 自己的输出逐像素一致：

| 阶段 | PixCake 参数 | 移植位置 |
| --- | --- | --- |
| 1 白平衡与曝光 | `temperature`、`tint`、`exposure` | `gpu/render/src/camera_raw.cpp` `balance()` |
| 3 对比度（不含纹理/清晰度） | `contrast` | `contrast()` |
| 4 Presence | `vibrance`、`saturation` | `presence()` |
| 6 Color Mixer | `hsl_{hue,saturation,luminance}_*` 八色带 | `colorMixer()`、`bandWeights()` |
| 7 Color Grading | `grading_{shadows,midtones,highlights,global}_*`、`grading_blending`（`grade_balance` 已实现，PixCake 未注册参数） | `colorGrading()` |

这些参数的移植状态因此是 `photocraft-reference-only`：**新的 C++ reference 还没有接入 `RenderExecutor`**，预览和导出仍跑 `render_executor.cpp` 里的 legacy 公式，GPU 也还没有对应 GLSL。

## 来源与许可

- PhotoCraft `885a6ddf27cc58c07faf1722e5c327cd38953657`：`crates/algo/src/camera_raw.rs`（`develop()` 阶段 1、3–7，`rgb_to_hsv`、`hsv_to_rgb`、`band_weights`、`white_balance_gains`）、`crates/algo/src/camera_raw/per_pixel.rs`（`gains`、`contrast`、`Grade::new`）、`crates/color/src/convert.rs`（sRGB 传递函数）。
- 许可：MIT（选自 `MIT OR Apache-2.0`），移植文件头保留版权声明和来源提交；锁定记录见 `third_party/photocraft.lock.cmake`。

## 对照向量

- 生成：`tools/photocraft_vectors` 直接调用 `photocraft_algo::camera_raw::develop(px, n, 1, &p, false)`（sRGB 编码，8 位语义：结尾裁剪到 0..1），Rust 1.99.1。生成物 `tests/gpu/data/camera_raw_per_pixel_vectors.inc`。
- 规模：28 组设置 × 73 个像素 = 2044 个像素。像素为 {0.05, 0.3, 0.6, 0.95}³ 的 64 色、灰阶 0/0.18/0.5/1、纯红/绿/蓝、橙 (1, 0.5, 0)、青 (0, 1, 1)。设置覆盖恒等、各阶段单独的正负极值（含曝光 ±5）、八个色带各一组、四个色轮、Blending 0/100、Balance +60，以及一组全部叠加。
- 测试：`tests/gpu/test_camera_raw_port.cpp` 的 `test_matches_photocraft_vectors`，容差 2e-6，**最大观测差 8.34e-7**。另有 9 个解析测试（恒等、增益、一档曝光、黑色/中灰对比度、色带权重、灰色不受 HSL 影响、饱和度 −100 去色、色轮方向）。
- 变异检查：把色彩分级色调系数 0.3 改为 0.31，`grade_shadows` 用例失败；已恢复。

运行：

```sh
cmake -S . -B build-camera-raw && cmake --build build-camera-raw --target test_camera_raw_port
ctest --test-dir build-camera-raw -R test_camera_raw_port --output-on-failure
```

## 仍未覆盖

1. 接入 `RenderExecutor`（替换 legacy `tone()`/`color()`/`apply_hsl()`/`apply_grading()` 的对应部分）、亮度在阶段 1 之后的插入点、预览/导出同一路径。
2. OpenGL/GLSL 移植与 readback parity（对照 `crates/gpu/src/camera_raw.wgsl`，容差 1/4096）。
3. 输入编码：本移植吃显示编码（sRGB）值；executor 的线性输入契约尚未闭环，接入时必须在边界写明转换。
4. Power-law 编码（`encoding_gamma ≠ 0`）、32 位浮点（`float = true`）。
5. 白色/黑色（整幅图像统计）、曲线（PixCake 参数未注册）、颗粒、暗角，以及空间阶段（去雾、高光/阴影、纹理、清晰度、锐化/降噪）。
6. 与像素蛋糕官方的一致性：未验证。
