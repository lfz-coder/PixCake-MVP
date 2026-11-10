# Camera Raw 逐像素阶段移植证据

日期：2026-11-09
总裁决：[PhotoCraft 能力来源与 PixCake 移植规范](../specs/2026-11-09-photocraft-reference-and-porting-policy.md)
计划：[Camera Raw 逐像素阶段移植](../plans/2026-11-09-camera-raw-per-pixel-port.md)、[接入 RenderExecutor](../plans/2026-11-09-camera-raw-executor-wiring.md)

## 结论

PhotoCraft Camera Raw 管线的五个逐像素阶段已经移植为 PixCake C++ reference，并与 PhotoCraft 自己的输出逐像素一致：

| 阶段 | PixCake 参数 | 移植位置 |
| --- | --- | --- |
| 1 白平衡与曝光 | `temperature`、`tint`、`exposure` | `gpu/render/src/camera_raw.cpp` `balance()` |
| 3 对比度（不含纹理/清晰度） | `contrast` | `contrast()` |
| 4 Presence | `vibrance`、`saturation` | `presence()` |
| 6 Color Mixer | `hsl_{hue,saturation,luminance}_*` 八色带 | `colorMixer()`、`bandWeights()` |
| 7 Color Grading | `grading_{shadows,midtones,highlights,global}_*`、`grading_blending`（`grade_balance` 已实现，PixCake 未注册参数） | `colorGrading()` |

2026-11-09 这些阶段和 Photoshop 现代亮度（`gpu/render/src/brightness.cpp`）已接入 `RenderExecutor` 和 GLSL，状态为 `photocraft-ported`（范围：8 位 sRGB 输入，executor 输入未线性化），见下文“接入”。

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

## 接入 RenderExecutor 与 GPU（2026-11-09）

提交：`b6e6344`（按阶段可调用）、`095e3fe`（现代亮度移植）、`6a9db0e`（reference 路径）、`308b21b`（GPU 路径）。

### 现代亮度移植

- 来源：PhotoCraft `crates/compose/src/adjust/tone.rs:155` `modern_brightness`、`tone.rs:230` `modern_lut(brightness, 0)`；移植为 `gpu/render/src/brightness.cpp`（`modernLut`、`sampleLut`、`curve`、`raised`）。
- 对照：`tools/photocraft_vectors -- brightness` 生成 `tests/gpu/data/modern_brightness_vectors.inc`（650 个采样），`test_brightness_port` 最大差 **1.19e-7**；`test_curve_matches_lut` 证明 GPU 用的“直接求曲线 + 32 步二分求逆”与 4096 项表的差 ≤1e-4。

### 门禁逐条（移植规范第 8 节）

| 门禁 | 证据 |
| --- | --- |
| 1 参数、单位、阶段顺序 | 单位与 Camera Raw/Photoshop 相同，直接传值；顺序见[公式登记](2026-11-09-pixel-formula-registry.md)“逐像素管线顺序”；亮度位置是 2026-11-09 用户裁决；Balance 未注册，固定 0 |
| 2 CPU reference 覆盖 | 恒等、正负极值、灰阶/颜色向量见上文 28×73 对照与 9 个解析测试；executor 层 `test_global_uses_camera_raw`（三个像素逐值等于 `developPixel`）、`test_brightness_after_balance`、`test_monochrome_skips_color_mixer`、`test_local_uses_camera_raw`，重写的 `test_hsl`、`test_grading` 逐值对照 `developPixel`。范围只有 8 位 sRGB；16 位/浮点未覆盖 |
| 3 OpenGL readback | `test_gpu_backend_C57_C60`（原生 CGL）在本机真实 context 上运行；无 context 时该测试跳过 |
| 4 CPU/GPU 容差 | 新用例 `wb_warm`、`exposure_brightness`、`brightness_down`、`contrast_cr`、`presence` 整帧与 32 px 分块最大差 **0**，`grading_cr` **1**（一个通道）；既有 `tone`、`levels`、`hsl` 差 0，`grading` 差 1。PhotoCraft parity 由 CPU 对照向量提供 |
| 5 预览与导出同一路径 | renderd 与 exportd 共用 `RenderExecutor`；`test_preview_server`、`test_semantic_mask_provider`、`test_exportd` 按新公式更新期望并通过 |
| 6 来源、许可证、文件头 | `camera_raw.cpp/.hpp`、`brightness.cpp/.hpp` 文件头带 MIT 声明与来源提交，`check_photocraft_license` CTest 检查；`kColor` 注释注明来源；矩阵、公式登记、来源登记同步更新 |

### 快速路径

只含 exposure、brightness 和 RGB 色阶的全局操作仍走 256 级逐通道查表；表由通用路径的 `color()`/`tone()`/`apply_levels()` 在灰阶上生成。`test_scalar_tone_fast_path_matches_general_reference` 对 brightness 单独、exposure+brightness+levels 组合，在不透明和半透明图上逐字节比较快速路径与强制通用路径，全部相同。contrast 作用于亮度，退出快速路径。

### 被替换的期望值

测试里钉住旧公式的期望值逐条记在提交 `6a9db0e`、`308b21b` 的说明中（例如灰 128 色温 +100：151/125/92 → 144/125/102；+1 EV：灰 64 → 128 改为 → 90）。tint 方向相反（Camera Raw 正值偏品红，旧实现偏绿），按 PhotoCraft 语义执行，像素蛋糕方向待黑盒确认。

### 性能（`bench_reference_render`，4000×3000）

同一台机器、同一构建配置（无 `CMAKE_BUILD_TYPE`），改动前 `095e3fe` 与改动后交替各跑两次，取两次范围。测量时机器负载约 14–17（其他应用占用），绝对值偏高，只看前后对比：

| 案例 | 改动前 reference | 改动后 reference | GPU（两者） |
| --- | ---: | ---: | ---: |
| 曝光（快速路径） | 123–135 ms | 151–199 ms | ≈650 ms |
| 色阶 | 122–215 ms | 137–168 ms | ≈650 ms |
| HSL | 391–487 ms | 876–1023 ms | ≈645 ms |
| 颜色分级 | 249–256 ms | 348–355 ms | ≈650 ms |
| 全部颜色组 | 812–920 ms | 1508–1556 ms | ≈650 ms |
| 24 MP 导出，全部 | 1571–2013 ms | 3071–3083 ms | ≈1080 ms |
| 拖动预览 600 px，全部 | 24.0–31.3 ms | 42.4–44.7 ms | — |

GPU 时间不变（以 readback 为主）。CPU reference 的 HSL 与“全部颜色组”约慢一倍：Color Mixer 每像素做 HSV 往返、八色带帽函数和一次 `pow`。只有 GPU 不可用时预览才走 reference；600 px 拖动预览从约 24–31 ms 变为约 43 ms（约 23 fps）。

## 仍未覆盖

1. 输入编码：executor 把解码器的 sRGB 字节直接作为显示编码输入；ICC/RAW 与非 sRGB 来源未闭环。
2. CPU reference 在 GPU 不可用时的速度（见上表）。
3. Power-law 编码（`encoding_gamma ≠ 0`）、32 位浮点（`float = true`）。
4. 白色/黑色（整幅图像统计）、曲线（PixCake 参数未注册）、颗粒、暗角，以及空间阶段（去雾、高光/阴影、纹理、清晰度、锐化/降噪）。
5. 与像素蛋糕官方的一致性：未验证（含 tint 方向）。
