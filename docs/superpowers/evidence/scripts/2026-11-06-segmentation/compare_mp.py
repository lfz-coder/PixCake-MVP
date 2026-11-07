"""Compare official MediaPipe ImageSegmenter against our C++ LiteRT run.

usage: compare_mp.py <model> <smoke_binary> <out_dir> <image>...
For every image:
  * official: mediapipe.tasks ImageSegmenter (category + confidence masks)
  * ours: stretch-resize to 256x256 RGB, (x-127.5)/127.5, C++ LiteRT, softmax,
    bilinear upsample to the original size, argmax
Writes per-image colour maps and per-class masks, prints agreement and IoU.
"""
import json
import subprocess
import sys
from pathlib import Path

import os
import cv2
import numpy as np
from PIL import Image
import mediapipe as mp
from mediapipe.tasks import python as mp_python
from mediapipe.tasks.python import vision

LABELS = ["background", "hair", "body-skin", "face-skin", "clothes", "others"]
COLORS = np.array([[0, 0, 0], [0, 0, 255], [255, 200, 150], [0, 255, 255], [0, 200, 0], [255, 0, 255]], np.uint8)

model, smoke, out_dir = sys.argv[1], sys.argv[2], Path(sys.argv[3])
out_dir.mkdir(parents=True, exist_ok=True)
segmenter = vision.ImageSegmenter.create_from_options(vision.ImageSegmenterOptions(
    base_options=mp_python.BaseOptions(model_asset_path=model),
    running_mode=vision.RunningMode.IMAGE,
    output_category_mask=True, output_confidence_masks=True))

report = {}
for path in sys.argv[4:]:
    name = Path(path).stem
    rgb = np.asarray(Image.open(path).convert("RGB"))
    h, w = rgb.shape[:2]

    res = segmenter.segment(mp.Image(image_format=mp.ImageFormat.SRGB, data=np.ascontiguousarray(rgb)))
    official = res.category_mask.numpy_view().astype(np.int32).reshape(h, w)
    official_conf = np.stack([m.numpy_view().reshape(h, w) for m in res.confidence_masks], -1)

    if os.environ.get("DOWN") == "cv2":
        small = cv2.resize(rgb, (256, 256), interpolation=cv2.INTER_LINEAR).astype(np.float32)
    else:
        small = np.asarray(Image.fromarray(rgb).resize((256, 256), Image.BILINEAR), np.float32)
    tensor = ((small - 127.5) / 127.5).astype(np.float32)[None]
    fin, fout = out_dir / f"{name}.in.f32", out_dir / f"{name}.out.f32"
    tensor.tofile(fin)
    log = subprocess.run([smoke, model, str(fin), str(fout), "4", "11"], capture_output=True, text=True, check=True).stdout
    logits = np.fromfile(fout, np.float32).reshape(256, 256, 6)
    e = np.exp(logits - logits.max(-1, keepdims=True))
    prob = e / e.sum(-1, keepdims=True)
    if os.environ.get("UP") == "logits":
        lu = cv2.resize(logits, (w, h), interpolation=cv2.INTER_LINEAR)
        e2 = np.exp(lu - lu.max(-1, keepdims=True)); up = e2 / e2.sum(-1, keepdims=True)
    else:
        up = np.stack([cv2.resize(prob[..., c], (w, h), interpolation=cv2.INTER_LINEAR) for c in range(6)], -1)
    ours = up.argmax(-1)

    agree = float((ours == official).mean())
    ious = {}
    for c, label in enumerate(LABELS):
        a, b = ours == c, official == c
        union = (a | b).sum()
        ious[label] = None if union == 0 else round(float((a & b).sum() / union), 4)
    conf_mae = float(np.abs(up - official_conf).mean())
    share = {label: round(float((official == c).mean()), 4) for c, label in enumerate(LABELS)}
    report[name] = {"size": [w, h], "agreement": round(agree, 5), "iou_ours_vs_official": ious,
                    "confidence_mae": round(conf_mae, 5), "official_class_share": share,
                    "timing": [l for l in log.splitlines() if "invoke" in l]}

    Image.fromarray(COLORS[official]).save(out_dir / f"{name}.official.png")
    Image.fromarray(COLORS[ours.astype(np.int32)]).save(out_dir / f"{name}.ours.png")
    overlay = (0.5 * rgb + 0.5 * COLORS[official]).astype(np.uint8)
    Image.fromarray(overlay).save(out_dir / f"{name}.overlay.png")

print(json.dumps(report, indent=1, ensure_ascii=False))
(out_dir / "report.json").write_text(json.dumps(report, indent=1, ensure_ascii=False))
