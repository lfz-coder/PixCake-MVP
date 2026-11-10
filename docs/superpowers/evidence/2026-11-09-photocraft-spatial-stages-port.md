# PhotoCraft Camera Raw 整帧阶段移植证据

日期：2026-11-09
状态：`photocraft-ported`（范围：8 位 sRGB 输入）。C++ reference 与 PhotoCraft 逐位一致，executor 预览和导出已接入；2026-11-10 起 GLSL 与 reference 端到端差 ≤1（见“GPU（GLSL）”一节）
计划：[空间与整图统计阶段移植](../plans/2026-11-09-photocraft-spatial-stages.md)
来源：PhotoCraft `885a6ddf27cc58c07faf1722e5c327cd38953657`，MIT（从 MIT OR Apache-2.0 中选择）

## 移植了什么

| PixCake 参数 | PhotoCraft 来源 | C++ 移植 |
| --- | --- | --- |
| `dehaze` | `crates/algo/src/camera_raw.rs` `dehaze`（≤768 px 代理上的暗通道先验、大气光取暗通道最亮 0.1 %、引导滤波细化透射率、双线性放大） | `camera_raw::dehaze` |
| `highlights` / `shadows` | `camera_raw/local_tone.rs` `apply`（log2 亮度上的快速局部拉普拉斯，最多 96 个参考层，>1600 px 时缩小后用引导滤波上采样变化量） | `camera_raw::localTone` |
| `contrast` + `texture` + `clarity` | `camera_raw.rs` `develop` 阶段 3 与 `tone()`（清晰度：`guided_fast`，半径 max(长边·0.02, 3)；纹理：`gauss`，σ = max(长边·0.002, 1.5·pixel_scale)） | `camera_raw::toneDetail` |
| `whites` / `blacks` | `camera_raw/whites_blacks.rs` 全部实测表与 `amounts`/`apply`；`camera_raw.rs` `luminance_points` | `camera_raw::whitesBlacks`、`luminancePoints`；表在 `whites_blacks_tables.inc`（脚本从 Rust 源码原样转出） |
| 共用滤波 | `camera_raw.rs` `box_mean`、`gauss`、`guided`、`guided_fast`；`fxutil.rs` `gauss_box_radii`；`photo_util.rs` `bilinear` | `boxMean`、`gauss`、`guided`、`guidedFast`、`gaussBoxRadii`、`bilinear` |

文件：`gpu/render/include/pixcake/gpu/camera_raw_spatial.hpp`、`gpu/render/src/camera_raw_spatial.cpp`、`gpu/render/src/whites_blacks_tables.inc`。PhotoCraft 按行并行（`par_rows`，平面 ≥ 2^15 个样本时）；C++ 用一个常驻线程池按行（盒式均值的列方向按列）分带，门槛相同，每个输出样本的求和顺序和类型（f32/f64）与 Rust 相同，所以结果逐位相同（`test_camera_raw_spatial` 把门槛设为 1 再跑全部用例，差 0；ThreadSanitizer 下 0 警告）。线程池忙时（另一线程上的渲染）在调用线程上直接执行。

## Executor 顺序

含任一整帧参数的操作在 reference 上按 PhotoCraft `develop` 分段执行（`draw_spatial`）：

1. 逐像素：Camera Raw 白平衡/曝光 → AI 风格偏移 → 现代亮度
2. 整帧：去雾 → 高光/阴影 → 对比度 + 纹理 + 清晰度 → 白色/黑色（中间值保持浮点、不裁剪）
3. 逐像素：Presence → 全局工具分组
4. 按覆盖率（不透明度 × 蒙版）回混；蒙版只决定在哪里显示，整帧阶段始终看到整幅图像

不含整帧参数的操作仍走原逐像素路径和 GPU。旧的 lift（0.45/0.20 系数）和“局部对比度”（三者共用的全局对比度）连同 GPU `tone1`/`tone2` uniform 一起删除，记录在[公式登记第 11 节](2026-11-09-pixel-formula-registry.md)。

## 对照向量

`tools/photocraft_vectors spatial` 直接调用 PhotoCraft 的 `develop`，生成 `tests/gpu/data/spatial_vectors.inc`（872 KB）：

- 帧：`detail` 24×16（渐变 + 硬边 + 3 px 棋盘细节）、`grey` 24×16（纯灰 0.5）、`block` 24×16（亮块）、`strip` 840×6（触发去雾代理缩小与 `guided_fast` 子采样，每 15 个像素取一个）、`long` 1700×12（宽于 1600 px，触发 local_tone 的缩小与引导上采样，只跑高光/阴影用例，每 25 个像素取一个）
- 用例 14 组：去雾 30/−100、对比度 40、纹理 100/−50（pixel_scale 0.5）、清晰度 100、清晰度 −50 + 对比度 40 + 纹理 −30、阴影 100、高光 −100、阴影 −50 + 高光 50、白色 100、黑色 −100、白色 −50 + 黑色 50、全部阶段组合
- 结果：`test_camera_raw_spatial` 60 个帧×用例组合，**最大差 0**（默认构建和 `-O3` 构建都是 0；渲染库 `-ffp-contract=off`）

变异检查（改一处常数后测试失败）：去雾权重 0.95、清晰度系数 0.8、阴影 sigma 0.571、参考层间距 0.125、引导上采样 eps 0.01（只有 1700 px 帧能抓到）、黑色照片系数 1.2、白点分位数 0.995。

## Executor 测试（`test_reference_spatial`）

| 测试 | 结果 |
| --- | --- |
| `test_spatial_order_matches_develop` | 全局操作同时含 temperature、exposure、dehaze、shadows、highlights、clarity、texture、contrast、whites、blacks、vibrance，逐像素等于把各阶段函数按上述顺序串起来，差 0；对调去雾与高光/阴影的顺序后失败 |
| `test_spatial_preview_matches_export` | 2400×1800 导出与 600×450 预览（源图 4× 缩小、pixel_scale 0.25），导出结果同样缩小后比较。六组参数（组合、阴影 100、高光 −100、白色 100/黑色 −100、清晰度+纹理 60、去雾 50）平均差 0.05–0.33 级（上限 2）。最大差只在源图平坦的 4×4 块上统计：高光/阴影、白色/黑色、纹理/清晰度 1–6 级（上限 8），去雾 16、组合 11（上限 20：透射率在块内变化，1/t 的均值不等于均值的 1/t）。跨硬边的块上，逐像素曲线作用在块均值上与先作用再平均本来就不同（白色/黑色 100 达 69 级），与移植无关。预览跳过白色/黑色的变异会失败；忽略 pixel_scale 或改工作尺寸这类只差 1–2 级的变异抓不到，那是 PhotoCraft 自身的预览/导出差距之内 |
| `test_spatial_tiny_frames` | 1×1、1×5、7×1 不崩溃 |
| `test_spatial_falls_back_to_reference` | 七个整帧参数任一非零时 `PassRegistry::classify` 为 `ReferenceOnly`，为 0 时仍为 `Implemented` |
| `test_spatial_local_masked` | 带蒙版的 `color.local`：蒙版内等于整幅渲染结果，蒙版外等于输入 |

`test_gpu_backend_native`：新用例“tone, whole-frame (reference)”回退 reference，与纯 reference 差 0；32 px 分块导出也回退到整幅 reference，差 0。

## 耗时（Release `-O3`，Apple M 系列 12 核）

`bench_reference_render`，参数：去雾 30、阴影 50、高光 −40、纹理 20、清晰度 30、白色 20、黑色 −20。

| 帧 | 单线程（第一版） | 线程池（当前） |
| --- | --- | --- |
| 600×450（拖动滑杆时的交互尺寸） | 234 ms | 88 ms |
| 4000×3000（预览上限） | 3.36 s | 0.72 s |

与 PhotoCraft 自身（同一台机器、同一噪声帧，`develop` release 构建）对比，单阶段耗时：

| 阶段 | 600×450 PixCake / PhotoCraft | 4000×3000 PixCake / PhotoCraft |
| --- | --- | --- |
| 去雾 30 | 33 / 45 ms | 81 / 84 ms |
| 阴影 50 + 高光 −40 | 134 / 131 ms | 514 / 513 ms |
| 纹理 + 清晰度 | 3 / 5 ms | 126 / 146 ms |
| 白色/黑色 | 6 / 6 ms | 179 / 198 ms |

C++ reference 已与 PhotoCraft 同速；剩下的耗时是算法本身（局部拉普拉斯每个参考层一整套高斯金字塔，最多 96 层）。同一 600 px 帧上的“全部色彩分组”为 7 ms，所以拖动高光/阴影时仍明显慢于其他滑杆；进一步提速要靠 GLSL 版本。

## GPU（GLSL，2026-11-10）

计划：[整帧阶段 GLSL](../plans/2026-11-10-photocraft-spatial-stages-glsl.md)。文件：`gpu/render/include/pixcake/gpu/camera_raw_gl.hpp`、`gpu/render/src/camera_raw_gl.cpp`、`gpu/render/src/camera_raw_gl_shaders.hpp`；GL 封装层的 R32F/RGBA32F 纹理与浮点读回（`gl.hpp`、两个 backend）。

- 每个阶段是一组片元着色器，作用在浮点纹理上（只用 `texelFetch`，不依赖浮点过滤或混合）。中间值不裁剪。
- 统计量读回小平面后，在 CPU 上用 reference 的同一函数计算（`quantileOf`、`localToneRange`、`whiteBlackPointsOfCells`、`whitesBlacksAmounts`、`whitesRow`/`blacksRow`）：
  - 去雾的大气光与两个均值：≤768 px 代理
  - 高光/阴影的取值范围与分位数：≤1600 px 工作平面
  - 白色/黑色的黑白点：≤256 px 副本
- 白色/黑色的曲线按强度插值成一行（PhotoCraft `row_at`）后上传，与原曲线差 ≤1.2e-7。
- executor 的分段：
  1. `kColor` 前段把 colour step 和亮度写入 Rgba32F 帧（`stage0.w`）
  2. 四个阶段按 `develop` 顺序执行
  3. `kColor` 后段从单元 3 读回结果（`stage0.z`），做 Presence 和分组，然后按覆盖率回混
- 适用条件：帧不超过一个 GPU 分块（4096 px），且不少于 `RenderExecutor::kDefaultSpatialGpuMinPixels`（32 万像素）。否则整幅走 reference，分块导出不会切开整帧阶段。

阶段级（`test_camera_raw_gl`，GPU 浮点帧对 C++ 浮点帧，容差 1/4096 ≈ 2.4e-4；帧：detail、grey、block、strip、scene 400×300、scaled ×2.5、long 1700×12）：

| 阶段 | 用例 | 最大差 |
| --- | --- | --- |
| 盒式均值、高斯、引导滤波、`guidedFast`、最小/最大滤波 | 噪声平面 −1..3，r 到 81 | ≤1.1e-5；最小/最大 0 |
| 去雾 | ±30、±100 | ≤7.7e-6 |
| 对比度 + 纹理 + 清晰度 | 五组 | ≤9.6e-7 |
| 高光/阴影 | 三组，含 1700 px 缩小与引导上采样 | ≤4.6e-6；1×1、1×5、7×1 逐位相同，2×2、2×7 ≤6e-8 |
| 白色/黑色 | 六组 | ≤8.4e-7 |

变异检查（都会让测试失败）：引导滤波 eps×2、去雾权重 0.96、remap smoothstep 系数、上采样 ×4.1、曲线采样 ×31.9、executor 后段关闭 Presence。

端到端（`test_gpu_backend_native`，RGBA8 readback 对 reference）：

| 用例 | 差 |
| --- | --- |
| 七个整帧参数 + exposure/contrast（97×61） | ≤1 |
| exposure 2 + 去雾/高光/阴影/清晰度 | ≤1 |
| `color.local` 两个蒙版 multiply | 0 |
| 整帧操作后接颗粒操作 | ≤1 |
| 1000×750 合成图（去雾代理 k=2、清晰度 `guidedFast` s=2） | ≤1 |
| 7×1 | 0 |
| 32 px 分块：整幅回退 reference | 0 |
| 默认最小像素数下的 97×61 帧：回退 reference | 0 |

耗时（Release，七个整帧参数，`bench_reference_render`）：

| 帧 | reference | GPU |
| --- | --- | --- |
| 600×450 | 89 ms | 94 ms |
| 800×600 | 148 ms | 125 ms |
| 1000×750 | 195 ms | 140 ms |
| 1200×900 | 256 ms | 152 ms |
| 1600×1200 | 422 ms | 190 ms |
| 4000×3000 | 724 ms | 200 ms |

交叉点约在 31 万像素，所以 `kDefaultSpatialGpuMinPixels` 取 32 万：拖动滑杆时的 600 px 帧继续走 reference，提交后的预览（≥800 px）走 GPU。

浮点平面只在一次渲染内复用，`execute()` 结束时全部释放（`RenderExecutor::gpuPooledBytes()` 渲染后为 0，`test_gpu_backend_native`）。所以常驻的 renderd/exportd 不会因为帧尺寸变化而累积 GPU 内存。一次 4000×3000 渲染期间的峰值，比不含整帧参数时约多 1 GB（最终审查实测）。改为每次渲染释放后，耗时不变（4000×3000 GPU 215 ms）。

## 门禁（移植规范第 8 节）

1. 来源与许可证：文件头与 `check_photocraft_license.cmake` 已登记，满足。
2. CPU 与 PhotoCraft 对照向量：最大差 0，满足。
3. GPU：满足。真实 context 上 readback（`test_camera_raw_gl`、`test_gpu_backend_native`）；没有 context 时这两个测试明确跳过，executor 回退 reference。
4. CPU 与 OpenGL 误差：满足。阶段级 ≤1.1e-5（声明容差 1/4096），8 位输出 ≤1 级；oracle 是 C++ reference，它本身与 PhotoCraft 向量逐位一致。
5. 预览与导出执行同一公式：满足（renderd 和 exportd 共用 `RenderExecutor`，回退也是整幅 reference）。
6. 旧公式测试“旧 → 新”：见提交 `cda7d36` 说明。

最终审查（独立 Opus 审查者）另用自己的 Rust/C++ 对照工具核对了 64 个帧×参数组合（1×1 到 1703×1301、真二维金字塔、超出 0..1 的输入），全部逐位一致。

因此状态是 `photocraft-ported`（范围：8 位 sRGB 输入）。2026-11-09 的第一版只有 C++ reference，当时是 `photocraft-reference-only`。
