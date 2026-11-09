# 像素公式与来源逐参数登记

日期：2026-11-09  
状态：当前 reference 实际公式的逐参数登记

这份文档回答三个问题：参数如何映射到内部数值、reference 实际执行什么公式、这个公式的来源能支持到什么程度。公式按当前代码登记，不把“GPU 与 reference 读回一致”写成“与 PixCake 官方一致”。

## 阅读规则

- p 是 RenderExecutor 收到的归一化 RGB 工作值，理论范围为 [0,1]；alpha 不参与这些颜色公式。当前 macOS 解码器仍输出 sRGB RGBA8，进入线性工作缓冲区的转换没有闭环，见[来源与共识登记](../specs/2026-11-06-pixel-processing-provenance.md)。
- clamp01(x)=min(max(x,0),1)。reference 在每个绘制步骤写回 RGBA8；最终写回使用 round(clamp01(x)*255)。GPU shader 保持相同的步骤顺序和量化语义。
- standard 只说明外部标准支持公式的基础数学语义；滑杆范围、系数和参数映射仍要单独标为 project-decision，除非有固定版本的官方黑盒证据。
- 代码位置以 gpu/render/src/render_executor.cpp 为主，GPU 对应实现位于 gpu/render/src/shader_sources.hpp。

## 1. 全局色彩和影调

### 白平衡、风格、曝光、对比度

| 参数 | UI 到内部值 | 当前实际公式 | 来源/结论 | 实现与证据 |
| --- | --- | --- | --- | --- |
| temperature | -100..100 线性映射到 D75→D65→D50 | M = XYZ→RGB · Bradford⁻¹ · diag(LMS_target/LMS_source) · Bradford · RGB→XYZ；D50/D65/D75 的 xy 白点在两端线性插值 | Bradford、sRGB 矩阵：normative-standard（ICC/CIE）；滑杆映射：project-decision；官方一致性未验证 | white_balance_matrix()；render_executor.cpp:75-97 |
| tint | t/100·0.5 stop | LMS_target.g *= 2^(t/100·0.5)，再走同一 Bradford 矩阵 | Bradford：normative-standard；半档比例：project-decision | render_executor.cpp:88-97 |
| AI 风格 amount | a=clamp(amount/100,0,2) | r'=r+0.04a; g'=g+0.01a; b'=b-0.03a | project-decision；没有官方像素公式来源 | color()；render_executor.cpp:153-163 |
| exposure | EV，范围 -5..5 | g=2^EV；p'=p·g | EV 的倍增语义：ISO 2720；PixCake 滑杆保护策略未验证 | apply_exposure()、tone()；render_executor.cpp:30-34,139-140,166-168 |
| contrast | k=1+contrast/100 | p'=(p-0.5)·k+0.5，最后在写回时裁剪 | affine contrast 的基础语义：W3C Filter Effects；-100..100→0..2 是 project-decision | render_executor.cpp:151-152,169-171 |

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

结论：brightness、highlights、shadows、whites、blacks 的实现公式现在是清楚的，但来源不是官方公式；0.45、0.20、中灰分界和三角权重都是 project-decision。代码注释已明确它们仍是 provisional，不能登记成 standard 或 verified-against-product。

实现：tone()，render_executor.cpp:165-178；GPU：shader_sources.hpp:157-168。

### 清晰度、纹理、祛雾、鲜艳度、饱和度

~~~text
local = (clarity + texture + dehaze) / 300 * 0.35
p = (p - 0.5) * (1 + local) + 0.5

sat = 1 + saturation/100 + (vibrance/100) * (1 - max(r,g,b))
gray = 0.2126*r + 0.7152*g + 0.0722*b
p' = gray + (p - gray) * sat
~~~

结论：三个“局部/细节”滑杆当前共用一个 local 对比度变换；vibrance 只是按最大通道衰减的饱和度缩放。没有记录到 PixCake 官方传递函数，这些全部是 project-decision，不能按产品原算法验收。

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

这里的 c 依次是原始 R、G、B，但参数只有一套 ib/iw/gamma/ob/ow，不是独立通道色阶。输入/输出黑白点的范围和 UI 名称来自架构附录及 test 版数值框观察；gamma=10^gray、最小跨度 1/255 和逐通道复用是 project-decision。红/绿/蓝独立通道的官方来源、参数和实现均缺失。

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

HSL 转换本身是常见色彩空间算法，但八色带、±100→±30°、明亮度半余量和按原饱和度缩放都是 project-decision；没有 PixCake 官方公式来源。灰色 S=0 时直接保持不变。

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

0.2126/0.7152/0.0722 是项目采用的 Rec.709-style 亮度权重；登记中还没有把可复核的标准原文链接固定下来。混合半色度、八色带和黑白档切换都是 project-decision。这正是“算法有实现但来源登记不完整”的一项。

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

目标权重是由 max/mid/min 的差值、离中灰的距离构成的九项经验权重。CMYK 形状、相对/绝对映射、黑色混合和目标权重均为 project-decision；没有官方公式来源。

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

全局区权重恒为 1。0.25、三角分区和混合半径是 project-decision；没有官方颜色分级传递函数。当前还没有注册“平衡”参数，因此不能宣称颜色分级全量完成。

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

hash01 的整数常量、三次平滑插值和尺寸映射都是项目选择；没有 PixCake 官方颗粒公式或胶片标定来源。amount=0 时整步跳过；colour=0 为单色颗粒，colour=1 为独立通道颗粒。

实现：grain_of()、hash01()、value_noise()、apply_grain()，render_executor.cpp:453-516；GPU：shader_sources.hpp:256-300；测试：test_grain()。

## 8. 局部、后期和 AI 色彩路径

- `color.local` 和 `color.post` 复用同一组 white balance、style、tone 和 color-group reference 公式；它们的差异来自操作顺序和蒙版权重，不是另一套像素公式。每个像素先算出 `after`，再用蒙版 alpha 的乘积作为 opacity 做合成。
- `color.style` 当前只执行固定 RGB 风格偏移：`r+=0.04a, g+=0.01a, b-=0.03a`，其中 `a=amount/100`。这不是官方 AI 风格模型。
- `color.match` 当前也不是 AI 模型。关闭 precise 时，amount 走同一固定 RGB 偏移；打开 precise 后，amount 本身不再绘制，效果来自写回到 `color.global`/`color.local` 的参数记录。两种路径都没有官方模型或逐像素来源。
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

1. **不是每个参数都有来源。** 对比度已有 W3C 基础公式，但亮度、高光、阴影、白色、黑色、清晰度、纹理、祛雾、鲜艳度、饱和度、HSL、黑白、可选颜色、颜色分级和颗粒的具体系数主要是项目决定。
2. **色阶不是红绿蓝独立色阶。** 当前只有一套 RGB 复合黑/灰/白参数，代码对 R/G/B 复用同一映射；用户所说的红、绿、蓝独立黑场/灰场/白场目前没有实现。
3. **黑白混合的 Rec.709 来源链接没有在登记中固定。** 代码和测试都写了权重，但来源登记仍不完整。
4. **GPU 证据只证明 CPU reference 对齐。** test_gpu_backend_C57_C60、test_gpu_shader_path 和 test_reference_color_groups 不能证明官方 PixCake 逐像素一致。
5. **颜色编辑器仍无目标颜色公式，输入色彩管理仍未闭环。** 这两项不能写成“已实现”。

因此，你的判断是对的：之前的“来源与公式登记”只能算分组级状态登记，不能算完整的逐公式来源登记。现在这份文档把已有实现的公式逐项摊开，同时把没有来源、没有独立通道或没有实现的部分明确标出来。
