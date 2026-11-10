# 像素公式与来源逐参数登记

日期：2026-11-09
状态：当前 reference 实际公式与 PhotoCraft 移植目标的逐参数登记
总裁决：[PhotoCraft 能力来源与 PixCake 移植规范](../specs/2026-11-09-photocraft-reference-and-porting-policy.md)

这份文档回答四个问题：参数如何映射到内部数值、当前 reference 实际执行什么公式、PhotoCraft 对应目标在哪里、来源能支持到什么程度。当前代码公式仍如实记录，但只要 PhotoCraft 有对应能力，当前 PixCake 公式就是 `legacy-provisional`，不能继续作为第二套行为标准；“GPU 与 reference 读回一致”也不等于“与 PhotoCraft 或 PixCake 官方一致”。

## 阅读规则

- p 是 RenderExecutor 收到的归一化 RGB 工作值，理论范围为 [0,1]；alpha 不参与这些颜色公式。当前 macOS 解码器仍输出 sRGB RGBA8，进入线性工作缓冲区的转换没有闭环，见[来源与共识登记](../specs/2026-11-06-pixel-processing-provenance.md)。
- clamp01(x)=min(max(x,0),1)。reference 在每个绘制步骤写回 RGBA8；最终写回使用 round(clamp01(x)*255)。GPU shader 保持相同的步骤顺序和量化语义。
- standard 只说明外部标准支持公式的基础数学语义；PhotoCraft 有对应实现时来源列使用 `photocraft-target`，当前 PixCake 旧系数标为 `legacy-provisional`。只有没有 PhotoCraft 等价能力时，才使用 `PixCake-specific` 或 `project-decision`。
- 代码位置以 gpu/render/src/render_executor.cpp 为主，GPU 对应实现位于 gpu/render/src/shader_sources.hpp。

PhotoCraft 参考副本当前提交为 `885a6ddf27cc58c07faf1722e5c327cd38953657`。PhotoCraft 有两个来源族，必须按语义选，不按名字选（见移植规范 4.1）：

- **A. Camera Raw 滤镜管线**：PixCake 的基础显影滑杆、HSL 八色带、色彩分级、曲线和颗粒的目标。CPU 看 `third_party/photocraft/crates/algo/src/camera_raw.rs` 及其 `camera_raw/per_pixel.rs`、`local_tone.rs`、`whites_blacks.rs`；GPU 逐像素阶段看 `crates/gpu/src/camera_raw.wgsl`；parity 看 `crates/gpu/tests/it/camera_raw.rs`。
- **B. Photoshop 调整图层与合成**：色阶、可选颜色、3D LUT、混合与图层合成的目标。CPU 看 `crates/compose/src/adjust.rs`、`adjust/tone.rs`、`adjust/color.rs`、`psblend.rs`、`composite.rs`；GPU 和 lowering 看 `crates/gpu/src/compose.wgsl`、`crates/gpu/src/plan/lower.rs`。

这些路径定义移植目标，不改变 PixCake 的 C++/OpenGL 架构。下文“来源/结论”列先写来源（`photocraft-target`/`PixCake-specific`/`no-equivalent`），再写当前移植状态。

**2026-11-09 接入**：temperature、tint、exposure、contrast、brightness、vibrance、saturation、HSL 八色带和色彩分级已是 `photocraft-ported`（范围：8 位 sRGB 输入，executor 输入未线性化）。CPU 公式在 `gpu/render/src/camera_raw.cpp`、`gpu/render/src/brightness.cpp`，`RenderExecutor` 预览和导出执行它们，GLSL 在 `gpu/render/src/shader_sources.hpp` `kColor`；证据见[逐像素移植证据](2026-11-09-camera-raw-per-pixel-port.md)。迁移前的旧公式记录在第 11 节。

**2026-11-09 第三批**：dehaze、highlights、shadows、texture、clarity、whites、blacks 执行 PhotoCraft Camera Raw 整帧阶段（`gpu/render/src/camera_raw_spatial.cpp`），状态 `photocraft-reference-only`：C++ 与 PhotoCraft 逐位一致，预览和导出已接入，没有 GLSL，GPU 回退 reference。证据见[整帧阶段移植证据](2026-11-09-photocraft-spatial-stages-port.md)。

### 逐像素管线顺序

帧里的值是解码器输出的 sRGB 字节 /255，即 Camera Raw 的显示编码输入（`encoding_gamma = 0`），executor 不做线性化。每个像素依次：

1. Camera Raw 阶段 1：temperature、tint、exposure（PhotoCraft 移植）
2. AI 风格偏移（`amount`，PixCake-specific 暂定）
3. Photoshop 现代亮度（PhotoCraft 移植；按 2026-11-09 用户裁决放在阶段 1 之后）
4. Camera Raw 阶段 3 Contrast（PhotoCraft 移植）
5. Camera Raw 阶段 4 Presence：vibrance、saturation（PhotoCraft 移植）
6. 全局工具的分组：旧色阶 → 黑白配置文件时旧黑白混合，否则 Camera Raw Color Mixer → 旧可选颜色 → Camera Raw Color Grading（Balance 固定 0）
7. 颗粒，最后按蒙版/不透明度合成并写回（裁剪到 0..1 再量化）

含任一整帧参数（dehaze、highlights、shadows、texture、clarity、whites、blacks）的操作改走 PhotoCraft `develop` 的分段顺序（`draw_spatial`，只在 reference 上）：第 1–3 步逐像素 → 整帧去雾 → 整帧高光/阴影 → 整帧对比度 + 纹理 + 清晰度（PhotoCraft `tone()`）→ 整帧白色/黑色 → 第 5–6 步逐像素 → 按覆盖率回混。整帧阶段之间保持浮点、不裁剪；蒙版只决定在哪里显示。

只含 exposure、brightness 和 RGB 色阶的全局操作走 256 级逐通道查表快速路径；表由上面同一组函数在灰阶上生成，与通用路径逐字节相同（`test_scalar_tone_fast_path_matches_general_reference`，含半透明像素）。contrast 作用于亮度，不走快速路径。

## 1. 全局色彩和影调

### 白平衡、风格、曝光、对比度

| 参数 | UI 到内部值 | 当前实际公式 | 来源/结论 | 实现与证据 |
| --- | --- | --- | --- | --- |
| temperature | -100..100，与 Camera Raw 单位相同，直接传值 | `g = (1+0.35t, 1-0.25n, 1-0.35t)`，t=temperature/100、n=tint/100；增益 `g/luma(g)·2^exposure`；每通道 `linear_to_srgb(max(srgb_to_linear(c)·gain, 0))` | `photocraft-target`（A），`photocraft-ported`。9.3.0 的 +100/-50/-100 黑盒测量保留为官方一致性证据，不是公式来源 | `camera_raw::whiteBalanceGains/balanceGains/applyBalance`；GPU `cr_balance`；PhotoCraft `algo/src/camera_raw.rs:670`、`camera_raw/per_pixel.rs:13-20` |
| tint | -100..100，直接传值 | 与 temperature 共用一次增益（见上）；正值降低绿色、偏品红（旧 Bradford 实现正值偏绿，方向相反） | `photocraft-target`（A），`photocraft-ported`；像素蛋糕方向待黑盒确认 | 同上 |
| AI 风格 amount | a=clamp(amount/100,0,2) | r'=r+0.04a; g'=g+0.01a; b'=b-0.03a | `PixCake-specific`；没有 PhotoCraft 对应业务模型 | color()；render_executor.cpp |
| exposure | EV，范围 -5..5（与 Camera Raw 单位相同） | 与白平衡合并：线性光乘 `2^exposure`（+1 EV 把 sRGB 64 变为 90） | `photocraft-target`（A），`photocraft-ported`。Photoshop“曝光度”调整图层（`compose/src/adjust.rs:138-157`，带 offset/gamma）**不是**本滑杆的目标 | `camera_raw::balanceGains`；PhotoCraft `algo/src/camera_raw.rs` 的 `balance()` 与 `per_pixel::gains` |
| contrast | -100..100，强度 `k = contrast/100·0.6` | `l0 = max(luma(c),0)`、`lc = min(l0,1)`；`lv = max(lc - k·sin(2π·lc)/(2π), 0) + (l0-lc)`；`c' = c·lv/l0`（l0 ≤ 1e-5 时 `c + (lv-l0)`），保持颜色比例 | `photocraft-target`（A），`photocraft-ported`（逐像素 Contrast；与纹理/清晰度同开时由整帧阶段 3 一起执行，见下） | `camera_raw::applyContrast`；GPU `cr_contrast`；PhotoCraft `algo/src/camera_raw/per_pixel.rs:23-25` 与 `camera_raw.rs` 阶段 3 |

### 亮度、高光、阴影、白色、黑色

highlights/shadows 与 whites/blacks 是 PhotoCraft Camera Raw 的整帧阶段，`photocraft-target`（A），状态 `photocraft-reference-only`：

- **高光/阴影**（`camera_raw::localTone`，PhotoCraft `algo/src/camera_raw/local_tone.rs` `apply`）：在 log2 亮度（线性 luma + 1/65536）上做快速局部拉普拉斯滤波，参考层间距 0.125、最多 96 层；高光把比周围亮的细节按超出 0.05 stop 的部分拉向（或推离）周围，再按 1 % 分位数把最暗处放回原位；阴影按“比周围暗”的程度 smoothstep 提亮，再按 99 % 分位数保住最亮处；强度随周围在图像 1–99 % 范围中的位置按 5 个结点插值。超过 1600 px 时先按块平均缩小，变化量用引导滤波（r=2、eps=0.01）放大回原尺寸；最后每像素线性光乘 `2^change`。
- **白色/黑色**（`camera_raw::whitesBlacks`，PhotoCraft `algo/src/camera_raw/whites_blacks.rs`）：33 级灰阶上的实测曲线（白色 38 行、黑色 21 行，内部量 10 一行）。压缩方向（白色降低、黑色提高）用固定曲线、按亮度变化等比缩放像素；裁剪方向（白色提高、黑色降低）按图像黑白点自适应（`luminancePoints`：≤256 px 副本上亮度的 3 % 分位与最亮通道的 99.5 % 分位；`AW`/`AB` 表双线性插值，黑色至少 1.2 倍），逐通道作用。

预览与导出都在当前帧上计算统计和滤波；PhotoCraft 的代理尺寸（256/768/1600 px）让二者接近，但不完全相同（[证据](2026-11-09-photocraft-spatial-stages-port.md)：400×300 平均 2.45 级）。旧的 lift 公式见第 11 节。

brightness 例外：Camera Raw（PV2012）没有亮度滑杆。2026-11-09 用户裁决按 Photoshop 来做：来源是 `photocraft-target`（B），状态 `photocraft-ported`。公式是 Photoshop 现代亮度/对比度中的亮度部分 `modern_brightness`（`third_party/photocraft/crates/compose/src/adjust/tone.rs:155`）：`modern_lut(brightness, 0)`（`tone.rs:230`）生成 4096 点查找表，逐通道作用于编码值，保持纯黑和纯白不动；不使用 legacy 线性亮度（`compose/src/adjust.rs` 的 `legacy: true` 分支）。单位：Photoshop 现代亮度范围 −150..150，PixCake −100..100 直接取同值。提亮分支是膝点 126/255 以下的直线 `s·x` 加 Hermite 或幂函数尾段；压暗分支是提亮曲线的单调逆。管线里亮度之前的白平衡/曝光可能把值推出 0..1，因此表外按 PhotoCraft `lut_extended`（`tone.rs:134`）沿表两端 16 步的斜率外推，不在中途裁剪（`test_brightness_is_continuous_past_white`）。它在 A 管线中的位置是 `PixCake-specific` 顺序：阶段 1 之后（管线第 3 步），待像素蛋糕黑盒测量确认。

实现：CPU `brightness::modernLut/sampleLut`（与 PhotoCraft 650 个采样最大差 1.19e-7）；GPU 不建表，`modern_brightness()` 直接求曲线，压暗用 32 步二分求逆（`brightness::curve/raised`，`test_curve_matches_lut` 证明二者差 ≤1e-4）。

### 清晰度、纹理、祛雾、鲜艳度、饱和度

clarity、texture、dehaze 是 PhotoCraft Camera Raw 的整帧阶段，`photocraft-target`（A），状态 `photocraft-reference-only`：

- **去雾**（`camera_raw::dehaze`，PhotoCraft `algo/src/camera_raw.rs` 阶段 2）：≤768 px 代理上的暗通道先验（He、Sun、Tang 2009），窗口半径 max(长边/60, 2)；大气光为暗通道最亮 0.1 % 的平均颜色（每通道至少 0.2）；透射率 `1 − w·开运算(归一化暗通道)`，用代理亮度做引导滤波细化后双线性放大、限制在 0.1..1；正值 `c + (J − c)·a`，负值向大气光混合。
- **对比度 + 纹理 + 清晰度**（`camera_raw::toneDetail`，PhotoCraft 阶段 3 与 `tone()`）：`lc = min(luma, 1)`；清晰度 `lv += clarity/100·0.8·(lc − guided_fast(lc))·(1 − (2lv − 1)²)`，半径 max(长边·0.02, 3)、eps 0.005；纹理 `lv += texture/100·0.9·clamp(lc − gauss(lc), ±0.1)`，σ = max(长边·0.002, 1.5·pixel_scale)；然后对比度 S 曲线，按 `lv/l0` 缩放颜色。纹理和清晰度都为 0 时就是上表的逐像素 Contrast。

PhotoCraft 这些阶段只有 CPU 实现；PixCake 的 OpenGL 版本以这份 C++ reference 为 oracle（下一份计划）。旧的“局部对比度”公式见第 11 节。

vibrance/saturation 是 Camera Raw 阶段 4（Presence），`photocraft-ported`：

~~~text
l = luma(c); sat = (max-min)/max; hue = HSV 色相
skin = clamp(1 - |hue-25|/25, 0, 1) * 0.5
k = 1 + saturation/100 + vibrance/100 * (1-sat) * (1-skin)
c' = l + (c - l) * max(k, 0)
~~~

不是 Photoshop“自然饱和度”调整图层（`compose/src/adjust/color.rs:345-371`）。

实现：tone()、`camera_raw::applyPresence`；GPU：`cr_presence`。

## 2. 色阶：当前只有 RGB 复合通道

当前注册的参数只有：

~~~text
levels_rgb_input_black
levels_rgb_input_gray
levels_rgb_input_white
levels_rgb_output_black
levels_rgb_output_white
~~~

没有 levels_red_*、levels_green_* 或 levels_blue_* 参数，因此“红、绿、蓝各自的黑场/灰场/白场”目前没有实现；这一套参数就是 Photoshop 色阶的 RGB 复合（master）通道，三个单色通道为恒等。

`photocraft-ported`（8 位 sRGB 输入），PhotoCraft `third_party/photocraft/crates/compose/src/adjust/tone.rs:299-353`：

~~~text
master = (input_black/255, input_white/255, gamma = 10^input_gray, output_black/255, output_white/255)
levels_q(ch, v, 255):            # Photoshop 在整级上分三步、每步四舍五入
  u = floor(clamp((round(v·255)/255 - in_black) / max(in_white - in_black, 1e-6), 0, 1)·255 + 0.5 + 1e-3) / 255
  t = whole(levels_gamma(u, max(gamma, 0.01)))
  out = whole(out_black + t·(out_white - out_black))          # whole(x) = floor(x·255 + 0.5)/255
levels_gamma(u, g) = u^(1/g)；g > 1 时暗部用 a·u − b·u^2.2（a = 2^g）的 toe 与幂曲线等值等斜率衔接
table[k] = levels_q(master, levels_q(identity, k/4095, 255), 255)，4096 项
c' = lut(table, c)                # 输入裁到 0..1，表内线性插值；三个通道各自查表
~~~

`gamma = 10^input_gray` 是 PixCake 参数映射。GPU 不上传表，在着色器里算出采样点两侧的两项再插值，与 CPU 相同。

实现：`adjust::levelsQ/levelsGamma/levelsLut/sampleLut`、apply_levels()；GPU：shader_sources.hpp `levels()`；测试：test_adjust_port、test_reference_adjustments `test_levels_uses_photocraft`。

## 3. HSL 八色带

Camera Raw 阶段 6 Color Mixer，`photocraft-ported`。色带顺序为 red, orange, yellow, green, aqua, blue, purple, magenta，中心色相为 0,30,60,120,180,240,270,300 度。黑白配置文件下不执行（`test_monochrome_skips_color_mixer`）。

~~~text
(H, S, V) = RGB_to_HSV(max(c, 0));  S <= 1e-4 时原样返回（灰色不动）
w_k = max(0, 1 - |Δ(H, centre_k)| / span_k)    # 色相环帽函数，span 为到相邻色带中心的距离
dh = Σ w_k*hsl_hue_k;  ds = Σ w_k*hsl_saturation_k;  dl = Σ w_k*hsl_luminance_k
c1 = HSV_to_RGB(H + dh/100*30, clamp01(S*(1+ds/100)), V)
c1 = c1 * luma(c) / luma(c1)                      # 色相、饱和度保持亮度
c' = c1 * 2^(dl/100 * S)                          # 明亮度是按饱和度加权的增益
~~~

来源 `third_party/photocraft/crates/algo/src/camera_raw.rs` 的 `band_weights` 与阶段 6。Photoshop“色相/饱和度”调整图层（`compose/src/adjust/color.rs:6-71`）只有六个范围，不是本分组的目标。

实现：`camera_raw::bandWeights/applyColorMixer`、apply_hsl()；GPU：`hsl()`；测试：test_hsl()（逐值对照 `developPixel`）。

## 4. 黑白配置文件和八色黑白混合

只有 profile=monochrome 时执行；此时 HSL、vibrance、saturation 按参数禁用规则不执行。八个混合参数仍按 HSL 色带权重分配：

~~~text
grey = 0.2126*r + 0.7152*g + 0.0722*b
chroma = max(r,g,b) - min(r,g,b)
mix = Σ(weight_i * bw_mix_i/100)
grey' = clamp01(grey + mix * chroma * 0.5)
RGB' = (grey', grey', grey')
~~~

0.2126/0.7152/0.0722 是旧 PixCake 采用的 Rec.709-style 亮度权重；混合半色度、八色带和黑白档切换属于 `legacy-provisional`。来源是 `PixCake-specific`：PhotoCraft 的 Camera Raw 管线没有黑白处理，Photoshop BlackWhite 调整图层（`compose/src/adjust.rs:231`、`compose/src/adjust/color.rs:98-119`）只有六个权重，和八色带不等价，只能作为“按色相加权灰度”的参考。若后续决定改成六范围或采用 Camera Raw HSL 权重，需要先修改参数模式。

实现：apply_monochrome()，render_executor.cpp；GPU：shader_sources.hpp `monochrome()`；测试：test_monochrome()。

## 5. 可选颜色

`photocraft-ported`（8 位 sRGB 输入），PhotoCraft `third_party/photocraft/crates/compose/src/adjust/color.rs:271-323`。九个目标 red, yellow, green, cyan, blue, magenta, white, neutral, black 与 PixCake 参数一一对应：

~~~text
max, min, mid = 三通道的最大、最小、中间值
weights = [R 为最大 ? max−mid, B 为最小 ? mid−min, G 为最大 ? max−mid, R 为最小 ? mid−min,
           B 为最大 ? max−mid, G 为最小 ? mid−min, max((min−0.5)·2, 0),
           clamp(1 − |max−0.5| − |min−0.5|, 0, 1), max((0.5−max)·2, 0)]
对每个目标 r（权重 > 0）和每个通道 i：ink = 1 − c_i，d = (墨量_i + 黑_r)/100
  relative: d·ink        absolute: d > 0 ? d·min(2(1−ink), 1) : d·min(2·ink, 1)
  delta_i += d · weight_r
c'_i = clamp(c_i − delta_i, 0, 1)
~~~

实现：`adjust::selectiveWeights/selectiveColor`、apply_selective()；GPU：shader_sources.hpp `selective()`；测试：test_adjust_port、test_selective_color()、test_reference_adjustments。

## 6. 颜色分级

Camera Raw 阶段 7 Color Grading，`photocraft-ported`。四个色轮（阴影、中间调、高光、全局）的色相 0..360、饱和度 0..100、明亮度 −100..100 与 PixCake 单位一致：

~~~text
tint_z = (HSV(hue_z,1,1) - luma(HSV(hue_z,1,1))) * sat_z/100 * 0.3
blend = 0.15 + clamp(blending,0,100)/100 * 0.35;  balance = clamp(balance,-100,100)/100 * 0.25
l = clamp01(luma(c))
shadows = 1 - smoothstep(0, 0.5+balance+blend, l)
highlights = smoothstep(0.5+balance-blend, 1, l)
midtones = max(1 - shadows - highlights, 0);  global = 1
依次对每个区：c += mask_z * tint_z;  lum_z ≠ 0 时 c *= 2^(mask_z * lum_z/100 * 0.6)
~~~

来源 `third_party/photocraft/crates/algo/src/camera_raw.rs` 的 `Wheel`、阶段 7 与 `camera_raw/per_pixel.rs` 的 `Grade::new`。Photoshop ColorBalance 调整图层（`compose/src/adjust/color.rs:187-269`）不是本分组的目标。PixCake 没有注册“平衡”参数，executor 固定 Balance = 0，因此不能宣称颜色分级全量完成。

实现：`camera_raw::resolveGrade/applyColorGrading`、apply_grading()；GPU：`grading()`；测试：test_grading()。

## 7. 颗粒

amount/size/roughness 是 PhotoCraft Camera Raw 阶段 9（`third_party/photocraft/crates/algo/src/camera_raw.rs`、`camera_raw/per_pixel.rs` `Grain::new`、`photo_util.rs` `hash01`），`photocraft-ported`（8 位 sRGB 输入）：

~~~text
cell = 1 + grain_size/25（全分辨率像素）；rough = grain_roughness/100；amount = grain_amount/100·0.12；seed = 0
scale = 1 / pixel_scale（pixel_scale = 帧长边 / 原图长边，renderd 由解码器给出的原图长边计算，导出为 1；有裁剪/旋转/翻转时预览与导出的坐标原点会错开约 1/pixel_scale 个全分辨率像素）
fx = x·scale/cell, fy = y·scale/cell
n = value_noise(fx, fy, seed)·(1−rough) + value_noise(2.3fx, 2.3fy, seed+7)·rough
    # value_noise：格点 hash01（splitmix 风格 64 位哈希）加 smoothstep 双线性
weight = 0.4 + 2.4·l·(1−l)，l = clamp(luma, 0, 1)
c += (n − 0.5)·2·amount·weight
~~~

shadows/midtones/highlights/colour 四个子参数是 `PixCake-specific` 扩展，叠加在上面：

~~~text
zone(l) = max(0,1−l/0.5)·shadows + max(0,1−|l−0.5|/0.5)·midtones + max(0,1−(1−l)/0.5)·highlights
weight ×= zone(l; 滑杆) / zone(l; 0.30, 0.50, 0.15)        # 默认值时恰为 1
colour > 0：通道 k 的 n_k = n + (n'_k − n)·colour，n'_k 用 seed k+1（k = 0..2）
~~~

默认 30/50/15 且 colour = 0 时与 PhotoCraft 逐位一致；colour 默认值为 10，所以默认参数下有 10% 的逐通道噪声。amount = 0 时整步跳过。GPU 的 `hash01` 照 `crates/gpu/src/camera_raw.wgsl` 用 uvec2 模拟 64 位整数。

实现：`camera_raw::hash01/valueNoise/grainOf/grainNoise/grainWeight/applyGrain`、grain_of()/apply_grain()；GPU：shader_sources.hpp `kGrain`；测试：test_camera_raw_port `test_grain_matches_photocraft`、test_reference_adjustments、test_renderd（半分辨率预览与全分辨率同位置颗粒相同）。

## 8. 局部、后期和 AI 色彩路径

- `color.local` 和 `color.post` 的滑杆与全局调色走同一 `tone_color_of()`/`color()`/`tone()`：白平衡、曝光、亮度、对比度、鲜艳度/饱和度已执行移植公式，去雾、高光/阴影、纹理/清晰度、白色/黑色执行 PhotoCraft 整帧阶段（reference）。操作顺序、蒙版权重、选择区和 PixCake 参数语义是 PixCake-specific。蒙版下的整帧阶段在整幅图像上计算，再按蒙版混合（`test_spatial_local_masked`）。
- `color.style` 当前只执行固定 RGB 风格偏移：`r+=0.04a, g+=0.01a, b-=0.03a`，其中 `a=amount/100`。PhotoCraft 没有 PixCake 的 AI 风格业务模型，因此这是 `PixCake-specific` 的临时实现，不得称为 PhotoCraft AI。
- `color.match` 当前也不是 AI 模型。关闭 precise 时，amount 走同一固定 RGB 偏移；打开 precise 后，amount 本身不再绘制，效果来自写回到 `color.global`/`color.local` 的参数记录。PhotoCraft 没有对应 PixCake 点数/样片流程，当前实现是 `PixCake-specific`，不能用 PhotoCraft 普通调整冒充。
- 局部操作最多四个 GPU 蒙版通道；超过四个蒙版、含整帧参数、GPU 不可用或通道未通过支持门禁时回退 reference（整幅帧，分块导出也一样）。

## 9. 其他已执行的像素运算

| 运算 | 当前公式 | 来源/限制 |
| --- | --- | --- |
| 蒙版权重 | 多个蒙版 alpha 归一化后相乘；GPU 一张纹理最多四个通道 | 项目合成规则；超过四个蒙版回退 reference |
| 操作回混 | 覆盖率 k = 不透明度 × 蒙版；`c = b·(1−k) + B(b, s)·k`，alpha 保持 | PhotoCraft `compose/src/composite.rs` `blend_back`，`photocraft-ported`；像素自身 alpha 不乘进 k |
| 图层 normal | W3C 一般公式：as = top.a·opacity，ao = as + ab(1−as)，c = (ab(1−as)·cb + as·cs)/ao | PhotoCraft `psblend.rs` `composite`/`normal_rgb`，`photocraft-ported` |
| multiply | B = cb·cs；图层 c = ((1−as)·ab·cb + (1−ab)·as·cs + as·ab·B)/ao | PhotoCraft，`photocraft-ported`；官方产品一致性未验证 |
| screen | B = cb + cs − cb·cs；图层同上 | PhotoCraft，`photocraft-ported`；官方产品一致性未验证 |
| 3D LUT | PhotoCraft `lut3d_sample` 三线性（`adjust::lut3dSample`），再按 amount 与输入混合 | 采样 `photocraft-ported`；amount 混合是 PixCake-specific；资源版本、色彩空间和许可证尚未登记；GPU 没有 3D LUT |

## 10. 这次审查确认的缺口

1. **已完成来源迁移：** Camera Raw 逐像素阶段与现代亮度（[逐像素移植证据](2026-11-09-camera-raw-per-pixel-port.md)），以及色阶、可选颜色、混合、3D LUT 采样和颗粒基础三项（[调整图层与颗粒移植证据](2026-11-09-photocraft-adjustments-and-grain-port.md)），均为 `photocraft-ported`（8 位 sRGB 输入）。高光/阴影/白色/黑色、纹理/清晰度/去雾为 `photocraft-reference-only`（[整帧阶段移植证据](2026-11-09-photocraft-spatial-stages-port.md)：C++ 与 PhotoCraft 逐位一致，预览和导出已接入，缺 GLSL）；曲线未实现。八色带黑白、颗粒扩展子参数和 AI 业务是 PixCake-specific；色彩编辑器是 no-equivalent。
2. **色阶不是红绿蓝独立色阶。** 当前只有一套 RGB 复合黑/灰/白参数，代码对 R/G/B 复用同一映射；用户所说的红、绿、蓝独立黑场/灰场/白场目前没有实现。
3. **黑白混合的 Rec.709 来源链接没有在登记中固定。** 代码和测试都写了权重，但来源登记仍不完整。
4. **GPU readback 证明的是与 CPU reference 对齐。** 已移植的各项靠 CPU reference 与 PhotoCraft 对照向量（`test_camera_raw_port`、`test_brightness_port`、`test_adjust_port`）加 GPU 与 reference 差 ≤1 两段证明 PhotoCraft parity；仍为旧公式的项，GPU 证据不能证明 PhotoCraft parity。任何一项都没有官方 PixCake 逐像素证明。
5. **颜色编辑器仍无目标颜色公式，输入色彩管理仍未闭环。** 这两项不能写成“已实现”。

因此，这份文档把当前旧实现的公式逐项摊开，同时明确 PhotoCraft 目标、PixCake-specific 能力和没有实现的部分。旧公式的详细记录是迁移基线，不是继续保留两套行为标准的理由。

## 11. 迁移前记录（2026-11-09 之前 executor 执行的旧公式）

以下是被 PhotoCraft 移植替换掉的旧 PixCake `legacy-provisional` 公式，只作回归对照，不再执行。被替换的测试期望值（旧 → 新）记在提交 `6a9db0e`、`308b21b`、`cda7d36` 的说明里。

~~~text
# PP-WB-001 白平衡：IEC/ICC sRGB D65 矩阵 + Bradford CAT（ICC 1:2022 附录 E）
target_white = D65 向 D50（temperature > 0）或 D75（< 0）按 |temperature|/100 插值
target_lms.M *= 2^(clamp(tint/100,-1,1) * 0.5)          # tint：绿锥半档，正值偏绿
RGB -> XYZ -> Bradford LMS -> 按白点缩放 -> XYZ -> RGB   # 直接作用于帧值
# 向量：灰 128 temperature +100 -> 151/125/92；tint +50 -> 95/156/126

# PP-EXP-001 曝光：帧值直接乘 2^EV（灰 64 +1 EV -> 128）
# 亮度：p += brightness/100
# PP-CONTRAST-001 对比度（W3C affine）：p' = (p - 0.5) * (1 + contrast/100) + 0.5

# 鲜艳度、饱和度
sat = 1 + saturation/100 + (vibrance/100) * (1 - max(r,g,b))
p' = luma + (p - luma) * sat

# HSL 八色带：HSL 色彩空间，相邻两带线性权重（和为 1）
H' = mod(H + 30*Σw·hue/100, 360)
S' = clamp01(S * (1 + Σw·sat/100))
L' = clamp01(L + light * 0.5 * S0 * (light>0 ? 1-L : L))

# 颜色分级：中心 0/0.5/1 的三角分区，全局权重 1
radius = 0.5 + 0.5 * grading_blending/100
w_z = max(0, 1 - |luma - center_z| / radius)
p += w_z*sat_z/100*0.25 * (HSL(hue_z,1,0.5) - luma(HSL(hue_z,1,0.5))) + w_z*lum_z/100*0.25
~~~

### 色阶、可选颜色、颗粒、合成（2026-11-09 第二批之前）

~~~text
# 色阶：span = max(iw − ib, 1/255)；c' = ob + clamp01((c − ib)/span)^(1/gamma) · (ow − ob)（无整级量化、无 toe）
# 可选颜色：next = clamp01(absolute ? ink + s : ink·(1 + s))；next += k·(k > 0 ? 1 − next : next)；
#           change += weight·(next − ink)；c' = clamp01(c − change)
# 颗粒：cell = max(1, min(w,h)·(0.0008 + 0.006·size))；32 位哈希值噪声，roughness 混入逐像素哈希；
#       strength = amount·zone·0.5（zone 为 shadows/midtones/highlights 三角分区）
# 合成：opacity·top.a 作为混合量，base + (B − base)·量（像素自身 alpha 乘进了调整强度）
~~~

### 高光/阴影/白色/黑色与局部对比度（2026-11-09 第三批之前）

~~~text
# 管线第 4 步，在现代亮度之后、Contrast 之前，逐像素：
l = 0.2126*r + 0.7152*g + 0.0722*b
hi = clamp01((l - 0.5) * 2);  lo = clamp01((0.5 - l) * 2)
p += highlights/100*hi*0.45 + shadows/100*lo*0.45 + whites/100*hi*0.20 + blacks/100*lo*0.20
local = (clarity + texture + dehaze) / 300 * 0.35
p = (p - 0.5) * (1 + local) + 0.5
# GPU：kColor 中同一公式，uniform tone1 = (h, s, w, k)、tone2.x = local
~~~
