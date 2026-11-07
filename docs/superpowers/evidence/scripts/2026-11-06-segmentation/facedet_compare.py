"""Official MediaPipe FaceDetector vs our own decode of the raw model output.

usage: facedet_compare.py <smoke_binary> <model> <short|full> <image>...
Our pipeline: letterbox (keep aspect, zero pad) to the model input, normalize to
[-1,1], run C++ LiteRT, decode SSD anchors, sigmoid, weighted NMS, undo letterbox.
"""
import json
import subprocess
import sys
import tempfile
from pathlib import Path

import cv2
import numpy as np
from PIL import Image
import mediapipe as mp
from mediapipe.tasks import python as mp_python
from mediapipe.tasks.python import vision

smoke, model, kind = sys.argv[1], sys.argv[2], sys.argv[3]
SIZE = 128 if kind == "short" else 192


def anchors():
    if kind == "short":
        strides, per_cell = [8, 16, 16, 16], None
    else:
        strides = [4]
    out = []
    layer = 0
    while layer < len(strides):
        # layers with the same stride share one grid; each layer adds 2 anchors
        # (aspect 1.0 + interpolated) for short range, 1 anchor for full range.
        last = layer
        while last < len(strides) and strides[last] == strides[layer]:
            last += 1
        n = (last - layer) * (2 if kind == "short" else 1)
        g = int(np.ceil(SIZE / strides[layer]))
        for y in range(g):
            for x in range(g):
                for _ in range(n):
                    out.append(((x + 0.5) / g, (y + 0.5) / g))
        layer = last
    return np.array(out, np.float32)


ANCHORS = anchors()


def decode(raw, scores, min_score=0.5, iou_thresh=0.3):
    s = 1 / (1 + np.exp(-np.clip(scores[:, 0], -100, 100)))
    keep = s >= min_score
    boxes = []
    for i in np.where(keep)[0]:
        ax, ay = ANCHORS[i]
        r = raw[i] / SIZE
        cx, cy, w, h = r[0] + ax, r[1] + ay, r[2], r[3]
        kps = [(r[4 + 2 * k] + ax, r[5 + 2 * k] + ay) for k in range(6)]
        boxes.append((s[i], cx - w / 2, cy - h / 2, cx + w / 2, cy + h / 2, kps))
    boxes.sort(key=lambda b: -b[0])
    result = []
    while boxes:  # weighted (blending) NMS
        top = boxes[0]
        group, rest = [], []
        for b in boxes:
            ix = max(0, min(top[3], b[3]) - max(top[1], b[1]))
            iy = max(0, min(top[4], b[4]) - max(top[2], b[2]))
            inter = ix * iy
            union = (top[3] - top[1]) * (top[4] - top[2]) + (b[3] - b[1]) * (b[4] - b[2]) - inter
            (group if union > 0 and inter / union > iou_thresh else rest).append(b)
        wsum = sum(b[0] for b in group)
        coords = [sum(b[0] * b[k] for b in group) / wsum for k in range(1, 5)]
        kps = [(sum(b[0] * b[5][k][0] for b in group) / wsum, sum(b[0] * b[5][k][1] for b in group) / wsum)
               for k in range(6)]
        result.append((top[0], *coords, kps))
        boxes = rest
    return result


det = vision.FaceDetector.create_from_options(vision.FaceDetectorOptions(
    base_options=mp_python.BaseOptions(model_asset_path=model), running_mode=vision.RunningMode.IMAGE))
report = {}
tmp = Path(tempfile.mkdtemp())
for path in sys.argv[4:]:
    rgb = np.asarray(Image.open(path).convert("RGB"))
    h, w = rgb.shape[:2]
    off = det.detect(mp.Image(image_format=mp.ImageFormat.SRGB, data=np.ascontiguousarray(rgb)))
    official = [[round(d.categories[0].score, 3), d.bounding_box.origin_x, d.bounding_box.origin_y,
                 d.bounding_box.width, d.bounding_box.height] for d in off.detections]

    scale = SIZE / max(w, h)
    nw, nh = round(w * scale), round(h * scale)
    canvas = np.zeros((SIZE, SIZE, 3), np.float32)
    px, py = (SIZE - nw) // 2, (SIZE - nh) // 2
    canvas[py:py + nh, px:px + nw] = cv2.resize(rgb, (nw, nh), interpolation=cv2.INTER_AREA)
    ((canvas - 127.5) / 127.5).astype(np.float32)[None].tofile(tmp / "in.f32")
    subprocess.run([smoke, model, str(tmp / "in.f32"), str(tmp / "out.f32"), "4", "1", "2"],
                   capture_output=True, check=True)
    n = len(ANCHORS)
    outs = np.fromfile(tmp / "out.f32", np.float32)
    raw, scores = outs[:n * 16].reshape(n, 16), outs[n * 16:].reshape(n, 1)
    ours = []
    for d in decode(raw, scores):
        x0 = (d[1] * SIZE - px) / scale
        y0 = (d[2] * SIZE - py) / scale
        x1 = (d[3] * SIZE - px) / scale
        y1 = (d[4] * SIZE - py) / scale
        ours.append([round(float(d[0]), 3), int(round(x0)), int(round(y0)), int(round(x1 - x0)), int(round(y1 - y0))])
    report[Path(path).stem] = {"size": [w, h], "official": official, "ours": ours}
print(json.dumps(report, indent=1))
