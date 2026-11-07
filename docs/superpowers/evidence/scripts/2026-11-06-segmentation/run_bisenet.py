"""Run BiSeNet face parsing (ONNX) on samples, with an optional face crop box.

usage: run_bisenet.py <model.onnx> <out_dir> <image>[:x0,y0,x1,y1] ...
Writes colour maps and prints per-class pixel share inside the processed region.
"""
import json
import sys
from pathlib import Path

import numpy as np
import onnxruntime as ort
from PIL import Image

LABELS = ['background', 'skin', 'left_eyebrow', 'right_eyebrow', 'left_eye', 'right_eye', 'eyeglasses',
          'left_ear', 'right_ear', 'earring', 'nose', 'mouth', 'upper_lip', 'lower_lip', 'neck', 'necklace',
          'cloth', 'hair', 'hat']
rng = np.random.default_rng(3)
COLORS = rng.integers(40, 255, (19, 3)).astype(np.uint8)
COLORS[0] = 0
COLORS[1] = [255, 200, 150]   # skin
COLORS[10] = [255, 120, 60]   # nose
COLORS[17] = [0, 0, 255]      # hair

sess = ort.InferenceSession(sys.argv[1], providers=['CPUExecutionProvider'])
out_dir = Path(sys.argv[2])
out_dir.mkdir(parents=True, exist_ok=True)
report = {}
for spec in sys.argv[3:]:
    path, _, box = spec.partition(':')
    img = Image.open(path).convert('RGB')
    if box:
        img = img.crop(tuple(int(v) for v in box.split(',')))
    arr = np.asarray(img.resize((512, 512), Image.Resampling.BILINEAR), np.float32) / 255.0
    arr = (arr - np.array([0.485, 0.456, 0.406], np.float32)) / np.array([0.229, 0.224, 0.225], np.float32)
    logits = sess.run(None, {sess.get_inputs()[0].name: np.transpose(arr, (2, 0, 1))[None]})[0][0]
    pred = logits.argmax(0).astype(np.uint8)
    name = Path(path).stem + ('_crop' if box else '')
    Image.fromarray(COLORS[pred]).resize(img.size, Image.NEAREST).save(out_dir / f'{name}.bisenet.png')
    img.save(out_dir / f'{name}.input.png')
    report[name] = {LABELS[i]: round(float((pred == i).mean()), 4) for i in np.unique(pred)}
print(json.dumps(report, indent=1))
