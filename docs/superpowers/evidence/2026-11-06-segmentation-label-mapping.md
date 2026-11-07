# 语义标签映射审计记录

日期：2026-11-06  
状态：候选映射，尚未绑定生产模型资源。

## 规则

项目部位只能由模型明确输出的原始标签产生：

- `supported`：模型有一个语义明确、与项目部位一致的原始标签。
- `derived`：项目部位是多个明确原始标签的逐像素并集；适配器使用 alpha 最大值合并。
- `unsupported`：没有完整且语义一致的原始标签，保持不可用。

`face`、`eye`、`mouth`、`cloth` 等相邻或更粗的标签不能自动转换为眼白、眼瞳、口腔、牙齿、全身衣物或身体皮肤。映射表必须由具体模型适配器显式提供，不能根据标签字符串猜测。

## BiSeNet face-parsing v0.0.2 候选映射

官方模型标签为：`background`、`skin`、左右眉、左右眼、`eyeglasses`、左右耳、`earring`、`nose`、`mouth`、`upper_lip`、`lower_lip`、`neck`、`necklace`、`cloth`、`hair`、`hat`。

| 项目部位 | 候选状态 | 原始标签 | 说明 |
|---|---|---|---|
| 面部皮肤 | supported | `skin` | 仅限该模型定义的面部 skin |
| 脖子 | supported | `neck` | 直接标签 |
| 眉毛 | derived | `left_eyebrow` + `right_eyebrow` | 左右眉并集 |
| 嘴唇 | derived | `upper_lip` + `lower_lip` | 不包含 mouth 内部 |
| 耳朵 | derived | `left_ear` + `right_ear` | 左右耳并集 |
| 头发 | supported | `hair` | 直接标签 |
| 头纱/帽子 | supported | `hat` | 仅覆盖帽子，不扩展为头纱 |
| 全部人物、全部皮肤、面部、身体皮肤 | unsupported | — | 该模型是脸部解析，缺少完整人体范围或完整皮肤语义 |
| 眼白、眼瞳、牙齿、口腔 | unsupported | — | `left_eye/right_eye`、`mouth` 不足以证明这些细分部位 |
| 全部衣服、上衣、内搭、连体衣、下装、手套、鞋子、背包、首饰、其他物品 | unsupported | — | `cloth`、`earring`、`necklace` 不覆盖项目定义的完整部位 |

该候选曾因训练数据非商业条款被挡在产品外；2026-11-06 修订：用户确认 PixCake 不用于商用。该条款不再是否决理由。样片验证完成前，上表仍只用于 PoC 和回归样片，不开启产品能力。

## SCHP ATR 候选映射

ATR 官方标签为：`Background`、`Hat`、`Hair`、`Sunglasses`、`Upper-clothes`、`Skirt`、`Pants`、`Dress`、`Belt`、左右鞋、`Face`、左右腿、左右臂、`Bag`、`Scarf`。

当前保守映射：

| 项目部位 | 候选状态 | 原始标签 | 说明 |
|---|---|---|---|
| 面部 | supported | `Face` | 直接标签 |
| 上衣 | supported | `Upper-clothes` | 直接标签 |
| 头纱/帽子 | supported | `Hat` | 仅覆盖帽子 |
| 鞋子 | derived | `Left-shoe` + `Right-shoe` | 左右鞋并集 |
| 背包 | supported | `Bag` | 直接标签 |
| 头发 | supported | `Hair` | 直接标签 |
| 连体衣、内搭、手套、首饰、眼白、眼瞳、嘴唇、牙齿、口腔、耳朵、脖子、身体皮肤 | unsupported | — | ATR 没有对应的完整独立标签 |
| 全部人物、全部皮肤、全部衣服、下装、其他物品 | unsupported | — | 需要更严格的合并语义或更完整标签覆盖，当前不宣称支持 |

ATR 代码仓库的许可证为 MIT；官方权重和 ATR 数据集的使用条款仍未取得可复现的明确文本（2026-11-06 修订：用户确认 PixCake 不用于商用。按非商用门槛评估）。条款记录前这张表不开启产品能力。

## MediaPipe SelfieMulticlass 候选映射

许可证为 Apache 2.0；审查记录见 `2026-11-06-segmentation-license-audit-mediapipe-vision.md`。模型内嵌标签：`background`、`hair`、`body-skin`、`face-skin`、`clothes`、`others`。

| 项目部位 | 候选状态 | 原始标签 | 说明 |
|---|---|---|---|
| 头发 | supported | `hair` | 直接标签 |
| 身体皮肤 | supported | `body-skin` | 样片确认：覆盖脖子、手、手臂 |
| 面部 | supported | `face-skin` | 样片确认：该类覆盖整张脸，含眉、眼、鼻、唇、牙齿，因此映射“面部”而非“面部皮肤” |
| 面部皮肤 | unsupported | — | MediaPipe 没有不含五官的脸部皮肤标签；由 BiSeNet 提供（见下） |
| 全部皮肤 | unsupported | — | `body-skin` + `face-skin` 的并集会带上眉眼唇齿，不是纯皮肤 |
| 全部衣服 | supported | `clothes` | 样片确认：覆盖整身衣物，不拆分上衣/下装 |
| 全部人物 | derived | `hair` + `body-skin` + `face-skin` + `clothes` + `others` | 样片中 `others` 只出现在眼镜上，属于人物；手持物仍需补样片确认 |
| 其他所有部位 | unsupported | — | 模型没有对应的细分标签；`others` 不映射到首饰或其他物品 |

BiSeNet 补充（需人脸检测裁剪后使用，见 `2026-11-06-segmentation-litert-runtime-and-samples.md`）：

| 项目部位 | 候选状态 | 原始标签 | 说明 |
|---|---|---|---|
| 面部皮肤 | derived | `skin` + `nose` | 样片确认 `skin` 不含鼻子；用户 2026-11-06 确认项目“面部皮肤”包含鼻子。需 BlazeFace 全距离检测裁剪（框长边 × 2.0），见 `2026-11-06-segmentation-face-detection.md` |

样片已做第一轮核对（4 张，缺深肤色/逆光/复杂背景）；补样片和边缘质量验证完成前，这张表只用于 PoC，不开启产品能力。

## 生产映射（`portrait_rules()`，2026-11-06）

MediaPipe（整人）+ BiSeNet（人脸裁剪内）组合后支持 10 个部位，表格见 `2026-11-06-segmentation-onnxruntime-integration.md` §3。BiSeNet 标签在代码中加前缀 `face_parsing/`，避免与 MediaPipe 标签同名。脖子因裁剪可能截断而不声明支持。

## 实现状态

`apps/renderd` 已提供通用映射适配器：它只接受显式规则，校验原始标签的几何信息，按规则合并 alpha，并在源标签缺失时返回 `UnsupportedFormat`。它不会把缺失部位替换成相邻部位，也不会生成占位蒙版。
