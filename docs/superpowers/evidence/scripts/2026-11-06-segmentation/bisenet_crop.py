"""Run BiSeNet on square crops; writes <crop>.labels.png (512x512 class ids) and
prints the list of written paths as JSON."""
import json
import sys

import numpy as np
import onnxruntime as ort
from PIL import Image

sess = ort.InferenceSession(sys.argv[1], providers=["CPUExecutionProvider"])
outs = []
for path in sys.argv[2:]:
    img = Image.open(path).convert("RGB").resize((512, 512), Image.Resampling.BILINEAR)
    arr = np.asarray(img, np.float32) / 255.0
    arr = (arr - np.array([0.485, 0.456, 0.406], np.float32)) / np.array([0.229, 0.224, 0.225], np.float32)
    logits = sess.run(None, {sess.get_inputs()[0].name: np.transpose(arr, (2, 0, 1))[None]})[0][0]
    out = path.rsplit(".", 1)[0] + ".labels.png"
    Image.fromarray(logits.argmax(0).astype(np.uint8)).save(out)
    outs.append(out)
print(json.dumps(outs))
