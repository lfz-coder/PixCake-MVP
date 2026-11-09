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

**2026-11-09 移植进展**：temperature、tint、exposure、contrast、vibrance、saturation、HSL 八色带和色彩分级的 PhotoCraft Camera Raw 公式已移植为 `gpu/render/src/camera_raw.cpp`，与 PhotoCraft 对照向量一致，状态为 `photocraft-reference-only`（见[逐像素移植证据](2026-11-09-camera-raw-per-pixel-port.md)）。下文“当前实际公式”仍描述 `RenderExecutor` 正在执行的 legacy 公式，直到接入完成。

## 1. 全局色彩和影调

### 白平衡、风格、曝光、对比度

| 参数 | UI 到内部值 | 当前实际公式 | 来源/结论 | 实现与证据 |
| --- | --- | --- | --- | --- |
| temperature | 当前 -100..100 为 PixCake 暂定映射 | 当前是旧 Bradford/D50-D65-D75 或黑盒曲线路径，具体以代码为准；PhotoCraft 目标为 `white_balance_gains(temperature, tint)` 按亮度归一的三通道增益，在线性光中相乘 | `photocraft-target`（A）；executor 仍为 `legacy-provisional`，C++ 移植为 `photocraft-reference-only`。9.3.0 的 +100/-50/-100 黑盒测量保留为官方一致性证据，不是公式来源 | white_balance_matrix()/apply_warm()；render_executor.cpp；PhotoCraft `algo/src/camera_raw.rs:670`、`camera_raw/per_pixel.rs:13-20` |
| tint | 当前 t/100·0.5 stop | 旧路径为 Bradford 绿锥缩放；PhotoCraft 目标同上，与 temperature 共用一次增益 | `photocraft-target`（A）；executor 仍为 `legacy-provisional`，C++ 移植为 `photocraft-reference-only` | tint_matrix()；render_executor.cpp；同上 |
| AI 风格 amount | a=clamp(amount/100,0,2) | r'=r+0.04a; g'=g+0.01a; b'=b-0.03a | `PixCake-specific`；没有 PhotoCraft 对应业务模型 | color()；render_executor.cpp:153-163 |
| exposure | EV，范围 -5..5（与 Camera Raw 单位相同） | 旧路径 g=2^EV，作用于未闭环的“工作值”；PhotoCraft 目标为文档值按 sRGB 解码到线性光 → 乘 `2^exposure`（与白平衡增益合并）→ 非负裁剪 → 重新编码 | `photocraft-target`（A）；executor 仍为 `legacy-provisional`，C++ 移植为 `photocraft-reference-only`。Photoshop“曝光度”调整图层（`compose/src/adjust.rs:138-157`，带 offset/gamma）**不是**本滑杆的目标 | apply_exposure()、tone()；PhotoCraft `algo/src/camera_raw.rs` 的 `balance()` 与 `per_pixel::gains` |
| contrast | k=1+contrast/100 | 旧路径 p'=(p-0.5)·k+0.5；PhotoCraft 目标是 Camera Raw 阶段 3 中围绕中灰的 S 曲线（强度 `contrast/100·0.6`），作用于亮度并保持颜色比例 | `photocraft-target`（A）；executor 仍为 `legacy-provisional`，C++ 移植为 `photocraft-reference-only` | render_executor.cpp:151-152,169-171；PhotoCraft `algo/src/camera_raw/per_pixel.rs:23-25` 与 `camera_raw.rs` 阶段 3 |

### 亮度、高光、阴影、白色、黑色

这些参数目前不是各自独立的官方模型，而是同一个项目 reference 中的不同系数：

~~~text
brightness_offset = brightness / 100
h = highlights / 100
s = shadows / 100
w = whites / 100
k = blacks / 100

p = p + brightness_offset
l = 0.2126*r + 0.7152*g + 0.0722*B
hi = clamp01((l - 0.5) * 2)
lo = clamp01((0.5 - l) * 2)
lift = h*hi*0.45 + s*lo*0.45 + w*hi*0.20 + k*lo*0.20
p = p + lift
~~~

结论：当前公式是清楚的，但 0.45、0.20、中灰分界和三角权重都是旧 PixCake `legacy-provisional`。highlights/shadows/whites/blacks 都有 PhotoCraft Camera Raw 目标（A）：高光/阴影是按引导滤波基底加权的局部曝光变化（`algo/src/camera_raw/local_tone.rs`），白色/黑色是按图像黑白点自适应的全局曲线（`algo/src/camera_raw/whites_blacks.rs`、`per_pixel::white_black_points`）。这两类都依赖整幅图像统计，移植时必须让预览代理和全分辨率导出共用同一统计。

brightness 例外：Camera Raw（PV2012）没有亮度滑杆。2026-11-09 用户裁决按 Photoshop 来做：来源是 `photocraft-target`（B），目标为 Photoshop 现代亮度/对比度中的亮度部分 `modern_brightness`（`third_party/photocraft/crates/compose/src/adjust/tone.rs:155`），实现方式是 `modern_lut(brightness, 0)`（`tone.rs:230`）生成的 4096 点查找表，逐通道作用于编码值，保持纯黑和纯白不动；不使用 legacy 线性亮度（`compose/src/adjust.rs` 的 `legacy: true` 分支）。单位：Photoshop 现代亮度范围 −150..150，PixCake −100..100 直接取同值。对比度不走这里，仍按 Camera Raw。它在 A 管线中的位置是 `PixCake-specific` 顺序：暂定放在阶段 1（白平衡/曝光）之后、阶段 2（去雾）之前，沿用当前 PixCake“曝光后先做亮度”的顺序，待像素蛋糕黑盒测量确认。当前实现仍是旧加法偏移，状态 `legacy-provisional`。

实现：tone()，render_executor.cpp:165-178；GPU：shader_sources.hpp:157-168。

### 清晰度、纹理、祛雾、鲜艳度、饱和度

~~~text
local = (clarity + texture + dehaze) / 300 * 0.35
p = (p - 0.5) * (1 + local) + 0.5

sat = 1 + saturation/100 + (vibrance/100) * (1 - max(r,g,b))
gray = 0.2126*r + 0.7152*g + 0.0722*b
p' = gray + (p - gray) * sat
~~~

结论：clarity/texture/dehaze 当前共用一个全局对比度变换，这是 `legacy-provisional`，与目标语义差距很大。三者都有 PhotoCraft Camera Raw 目标（A）：清晰度和纹理是引导滤波/高斯分带的局部对比度（`algo/src/camera_raw/local_tone.rs`），去雾是暗通道先验（He、Sun、Tang 2009）加引导滤波细化（`algo/src/camera_raw.rs` 阶段 2）。它们是空间阶段，PhotoCraft 只有 CPU 实现，PixCake 的 OpenGL 版本以移植后的 C++ reference 为 oracle。

vibrance/saturation 的目标是 Camera Raw 阶段 4（Presence，鲜艳度向低饱和颜色加权），不是 Photoshop“自然饱和度”调整图层（`compose/src/adjust/color.rs:345-371`）。当前最大通道衰减公式是 `legacy-provisional`。

实现：tone()、render_executor.cpp:149-150,176-182；GPU：shader_sources.hpp:165-168。

## 2. 色阶：当前只有 RGB 复合通道

当前注册的参数只有：

~~~text
levels_rgb_input_black
levels_rgb_input_gray
levels_rgb_input_white
levels_rgb_output_black
levels_rgb_output_white
~~~

没有 levels_red_*、levels_green_* 或 levels_blue_* 参数。因此“红、绿、蓝各自的黑场/灰场/白场”目前没有实现，也没有可登记的公式；当前代码把同一公式分别应用到 R、G、B 三个通道：

~~~text
ib = input_black / 255
iw = input_white / 255
ob = output_black / 255
ow = output_white / 255
gamma = 10^(input_gray)
span = max(iw - ib, 1/255)
x = clamp01((c - ib) / span)
y = x^(1/gamma)
c' = ob + y * (ow - ob)
~~~

这里的 c 依次是原始 R、G、B，但参数只有一套 ib/iw/gamma/ob/ow，不是独立通道色阶。输入/输出黑白点的范围和 UI 名称来自架构附录及 test 版数值框观察；gamma=10^gray、最小跨度 1/255 和逐通道复用是旧 PixCake `legacy-provisional`。PhotoCraft 的 `levels`、`levels_gamma`、`levels_float` 和 per-channel/master 组合在 `third_party/photocraft/crates/compose/src/adjust/tone.rs:299-367`，移植时必须覆盖这些边界；红/绿/蓝独立参数仍属于 PixCake 参数模式缺口。

实现：color_groups()、apply_levels()，render_executor.cpp:278-290,329-336；GPU：shader_sources.hpp:170-175。

已有验证只覆盖复合 RGB：tests/gpu/test_reference_color_groups.cpp:test_levels()，没有独立 R/G/B 色阶测试。

## 3. HSL 八色带

色带顺序为 red, orange, yellow, green, aqua, blue, purple, magenta，中心色相为 0,30,60,120,180,240,270,300 度。输入色相位于相邻两带之间时使用线性权重，权重总和为 1。

~~~text
hue = Σ(weight_i * hsl_hue_i/100)
sat = Σ(weight_i * hsl_saturation_i/100)
light = Σ(weight_i * hsl_luminance_i/100)

H' = mod(H + 30*hue, 360)
S' = clamp01(S * (1 + sat))
L' = clamp01(L + light * 0.5 * S0 * (light>0 ? 1-L : L))
RGB' = HSL_to_RGB(H', S', L')
~~~

HSL 转换本身是常见色彩空间算法；当前 ±100→±30°、明亮度半余量、按原饱和度缩放和相邻两带线性权重是旧 PixCake `legacy-provisional`。目标是 PhotoCraft Camera Raw 阶段 6 Color Mixer（A）：同样的八个色带、色相环上的帽函数权重（`third_party/photocraft/crates/algo/src/camera_raw.rs` 的 `hsl_hue/hsl_sat/hsl_lum`）。Photoshop“色相/饱和度”调整图层（`compose/src/adjust/color.rs:6-71`）只有六个范围，不是本分组的目标。灰色（HSV 饱和度 ≤ 1e-4）原样通过，已由移植测试 `test_hsl_leaves_greys` 固定；executor 的旧路径尚未切换。

实现：to_hsl()、band_weights()、apply_hsl()，render_executor.cpp:204-258,339-358；GPU：shader_sources.hpp:101-149,176-187。

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

实现：apply_monochrome()，render_executor.cpp:360-381；GPU：shader_sources.hpp:189-198；测试：test_monochrome()。

## 5. 可选颜色

九个目标为 red, yellow, green, cyan, blue, magenta, white, neutral, black，每个目标有 CMYK 四个量。代码先计算目标权重，再在每个 RGB 通道上把颜色换算成墨量 ink=1-channel：

~~~text
relative: next = clamp01(ink * (1 + selective_target_ink/100))
absolute: next = clamp01(ink + selective_target_ink/100)
next = next + black * (black>0 ? 1-next : next)
change += target_weight * (next - ink)
channel' = clamp01(channel - change)
~~~

目标权重是当前 PixCake 的九项经验权重，旧路径的 CMYK 形状、相对/绝对映射、黑色混合和目标权重均为 `legacy-provisional`。来源是 `photocraft-target`（B）：PhotoCraft 的 `selective_color_weights` 与 `selective_color` 在 `third_party/photocraft/crates/compose/src/adjust/color.rs:271-323`，九个目标和相对/绝对模式与 PixCake 参数一一对应，应替换为移植目标并补齐相对/绝对 parity。

实现：apply_selective()，render_executor.cpp:384-414；GPU：shader_sources.hpp:199-224；测试：test_selective_color()。

## 6. 颜色分级

分区为阴影、中间调、高光、全局。前三个中心分别是 0,0.5,1，混合值控制三角权重半径：

~~~text
radius = 0.5 + 0.5 * grading_blending/100
w_z = max(0, 1 - abs(luminance - center_z)/radius)

tint = w_z * grading_saturation_z/100 * 0.25
lift = w_z * grading_luminance_z/100 * 0.25
p += tint * (HSL(hue_z,1,0.5) - luminance(HSL(hue_z,1,0.5))) + lift
~~~

全局区权重恒为 1。当前 0.25、三角分区和混合半径是旧 PixCake `legacy-provisional`。来源是 `photocraft-target`（A）：Camera Raw 阶段 7 Color Grading 有同样的阴影/中间调/高光/全局四个色轮（色相 0..360、饱和度 0..100、明亮度 −100..100）以及 Blending 和 Balance（`third_party/photocraft/crates/algo/src/camera_raw.rs` 的 `Wheel`、`grade_*`），单位与 PixCake 一致。Photoshop ColorBalance 调整图层（`compose/src/adjust/color.rs:187-269`）不是本分组的目标。PixCake 当前没有注册“平衡”参数，因此不能宣称颜色分级全量完成。

实现：apply_grading()，render_executor.cpp:416-437；GPU：shader_sources.hpp:225-239；测试：test_grading()。

## 7. 颗粒

颗粒是确定性的项目噪声算法，不是已登记的胶片或官方传感器模型：

~~~text
amount, shadows, midtones, highlights, size, roughness, colour = slider / 100
cell = max(1, min(width,height) * (0.0008 + 0.006*size))
inv_cell = 1 / cell

smooth = value_noise(x*inv_cell, y*inv_cell, channel)
noise = smooth + (hash01(x,y,channel+8) - smooth)*roughness - 0.5

zone = max(0,1-l/0.5)*shadows
     + max(0,1-|l-0.5|/0.5)*midtones
     + max(0,1-(1-l)/0.5)*highlights
strength = amount * zone * 0.5

mono = noise(channel=0)
R += strength * (mono + (noise(1)-mono)*colour)
G += strength * (mono + (noise(2)-mono)*colour)
B += strength * (mono + (noise(3)-mono)*colour)
~~~

hash01 的整数常量、三次平滑插值和尺寸映射都是项目选择，当前为 `legacy-provisional`。amount/size/roughness 有 PhotoCraft Camera Raw 阶段 9 grain 目标（A，按位置哈希、确定性、按全分辨率坐标——见 `crates/gpu/tests/it/camera_raw.rs` 的 `grain_in_full_resolution_coordinates`）；shadows/midtones/highlights/colour 四个子参数是 `PixCake-specific` 扩展，Camera Raw 没有。amount=0 时整步跳过；colour=0 为单色颗粒，colour=1 为独立通道颗粒。

实现：grain_of()、hash01()、value_noise()、apply_grain()，render_executor.cpp:453-516；GPU：shader_sources.hpp:256-300；测试：test_grain()。

## 8. 局部、后期和 AI 色彩路径

- `color.local` 和 `color.post` 的基础白平衡/影调/色彩操作与全局调色共用同一 Camera Raw 目标（A）；操作顺序、蒙版权重、选择区和 PixCake 参数语义是 PixCake-specific。当前复用旧 reference，状态是 `legacy-provisional`，不是已完成移植。蒙版下的空间阶段（清晰度、去雾等）仍按整幅图像统计计算，再按蒙版混合。
- `color.style` 当前只执行固定 RGB 风格偏移：`r+=0.04a, g+=0.01a, b-=0.03a`，其中 `a=amount/100`。PhotoCraft 没有 PixCake 的 AI 风格业务模型，因此这是 `PixCake-specific` 的临时实现，不得称为 PhotoCraft AI。
- `color.match` 当前也不是 AI 模型。关闭 precise 时，amount 走同一固定 RGB 偏移；打开 precise 后，amount 本身不再绘制，效果来自写回到 `color.global`/`color.local` 的参数记录。PhotoCraft 没有对应 PixCake 点数/样片流程，当前实现是 `PixCake-specific`，不能用 PhotoCraft 普通调整冒充。
- 局部操作最多四个 GPU 蒙版通道；超过四个蒙版、GPU 不可用或通道未通过支持门禁时回退 reference。

## 9. 其他已执行的像素运算

| 运算 | 当前公式 | 来源/限制 |
| --- | --- | --- |
| 蒙版权重 | 多个蒙版 alpha 归一化后相乘；GPU 一张纹理最多四个通道 | 项目合成规则；超过四个蒙版回退 reference |
| normal 合成 | base + (top-base) * clamp(opacity*top.a) | 项目合成规则 |
| multiply | top = base * top，再按 opacity 合成 | 通用混合语义；官方产品一致性未验证 |
| screen | top = 1-(1-base)*(1-top)，再按 opacity 合成 | 通用混合语义；官方产品一致性未验证 |
| 3D LUT | 资源坐标乘 size-1，8 个邻点三线性插值，再按 amount 与输入混合 | 只有 reference 资源路径；资源版本、色彩空间和许可证尚未登记；GPU 没有 3D LUT |

## 10. 这次审查确认的缺口

1. **还没有任何参数在 executor 中完成来源迁移。** 白平衡、曝光、对比度、鲜艳度/饱和度、HSL 和色彩分级已有 PhotoCraft 移植的 C++ reference（`photocraft-reference-only`，见[逐像素移植证据](2026-11-09-camera-raw-per-pixel-port.md)），但预览和导出仍执行旧公式。来源目标：白平衡、曝光、对比度、高光/阴影/白色/黑色、纹理/清晰度/去雾、鲜艳度/饱和度、曲线、HSL、色彩分级和颗粒基础三项以 Camera Raw（A）为目标；亮度、色阶、可选颜色、3D LUT 和混合以调整图层/合成（B）为目标；RenderExecutor 当前对它们执行的都还是旧公式。亮度按用户裁决以 Photoshop `modern_brightness`（B）为目标；八色带黑白、颗粒扩展子参数和 AI 业务是 PixCake-specific；色彩编辑器是 no-equivalent。
2. **色阶不是红绿蓝独立色阶。** 当前只有一套 RGB 复合黑/灰/白参数，代码对 R/G/B 复用同一映射；用户所说的红、绿、蓝独立黑场/灰场/白场目前没有实现。
3. **黑白混合的 Rec.709 来源链接没有在登记中固定。** 代码和测试都写了权重，但来源登记仍不完整。
4. **GPU 证据只证明 CPU reference 对齐。** test_gpu_backend_C57_C60、test_gpu_shader_path 和 test_reference_color_groups 不能证明 PhotoCraft parity 或官方 PixCake 逐像素一致；PhotoCraft 目标必须单独引入向量/器件测试。
5. **颜色编辑器仍无目标颜色公式，输入色彩管理仍未闭环。** 这两项不能写成“已实现”。

因此，这份文档把当前旧实现的公式逐项摊开，同时明确 PhotoCraft 目标、PixCake-specific 能力和没有实现的部分。旧公式的详细记录是迁移基线，不是继续保留两套行为标准的理由。
