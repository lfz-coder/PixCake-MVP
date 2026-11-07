"""Full-range face detection (official MediaPipe, for boxes) -> expanded square crop
-> BiSeNet -> face skin (skin + nose) pasted back into the full image.

usage: face_pipeline.py <facedet.tflite> <bisenet.onnx> <expand> <out_dir> <image>...
Run with the mediapipe venv; BiSeNet is run through onnxruntime from the PoC venv
via a subprocess-free import path given in PYTHONPATH-free form (we shell out).
"""
import json
import subprocess
import sys
from pathlib import Path

import numpy as np
from PIL import Image
import mediapipe as mp
from mediapipe.tasks import python as mp_python
from mediapipe.tasks.python import vision

facedet, bisenet, expand, out_dir = sys.argv[1], sys.argv[2], float(sys.argv[3]), Path(sys.argv[4])
out_dir.mkdir(parents=True, exist_ok=True)
det = vision.FaceDetector.create_from_options(vision.FaceDetectorOptions(
    base_options=mp_python.BaseOptions(model_asset_path=facedet), running_mode=vision.RunningMode.IMAGE))
BISENET_PY = "/private/tmp/pixcake-segmentation-venv/bin/python"
RUNNER = str(Path(__file__).with_name("bisenet_crop.py"))
report = {}
for path in sys.argv[5:]:
    img = Image.open(path).convert("RGB")
    w, h = img.size
    rgb = np.asarray(img)
    faces = det.detect(mp.Image(image_format=mp.ImageFormat.SRGB, data=np.ascontiguousarray(rgb))).detections
    skin = np.zeros((h, w), np.uint8)
    crops = []
    for i, d in enumerate(faces):
        b = d.bounding_box
        cx, cy = b.origin_x + b.width / 2, b.origin_y + b.height / 2
        side = max(b.width, b.height) * expand
        box = [int(round(cx - side / 2)), int(round(cy - side / 2)), int(round(cx + side / 2)), int(round(cy + side / 2))]
        crop_path = out_dir / f"{Path(path).stem}_f{i}.png"
        img.crop(box).save(crop_path)  # PIL pads out-of-image areas with black
        crops.append((box, crop_path))
    if crops:
        args = [BISENET_PY, "-I", RUNNER, bisenet] + [str(c[1]) for c in crops]
        labels = json.loads(subprocess.run(args, capture_output=True, text=True, check=True).stdout)
        for (box, crop_path), lab_path in zip(crops, labels):
            lab = np.asarray(Image.open(lab_path))
            side = box[2] - box[0]
            lab = np.asarray(Image.fromarray(lab).resize((side, side), Image.NEAREST))
            m = np.isin(lab, [1, 10]).astype(np.uint8) * 255  # skin + nose
            x0, y0 = max(box[0], 0), max(box[1], 0)
            x1, y1 = min(box[2], w), min(box[3], h)
            sub = m[y0 - box[1]:y1 - box[1], x0 - box[0]:x1 - box[0]]
            skin[y0:y1, x0:x1] = np.maximum(skin[y0:y1, x0:x1], sub)
    overlay = rgb.copy()
    overlay[skin > 0] = (0.4 * overlay[skin > 0] + 0.6 * np.array([255, 0, 160])).astype(np.uint8)
    Image.fromarray(overlay).save(out_dir / f"{Path(path).stem}.faceskin.png")
    report[Path(path).stem] = {"faces": len(faces), "boxes": [c[0] for c in crops],
                               "face_skin_share": round(float((skin > 0).mean()), 4)}
print(json.dumps(report, indent=1))
