"""Reference shares of BiSeNet face parts on an image, given face boxes.
usage: bisenet_reference.py <bisenet.onnx> <image> <x,y,w,h>...   (boxes from the official detector)
Crop: square, side = longer box side * 2.0, black outside the image; 512x512 bilinear;
ImageNet normalisation; softmax; per-class probability pasted back with max(); share = p > 0.5."""
import json, sys
import numpy as np, onnxruntime as ort
from PIL import Image
LABELS = ['background','skin','left_eyebrow','right_eyebrow','left_eye','right_eye','eyeglasses','left_ear','right_ear','earring','nose','mouth','upper_lip','lower_lip','neck','necklace','cloth','hair','hat']
sess = ort.InferenceSession(sys.argv[1], providers=['CPUExecutionProvider'])
img = Image.open(sys.argv[2]).convert('RGB'); W, H = img.size
full = np.zeros((19, H, W), np.float32)
for spec in sys.argv[3:]:
    x, y, w, h = map(float, spec.split(','))
    cx, cy, side = x + w / 2, y + h / 2, max(w, h) * 2.0
    x0, y0 = int(round(cx - side / 2)), int(round(cy - side / 2)); s = int(round(side))
    crop = img.crop((x0, y0, x0 + s, y0 + s)).resize((512, 512), Image.Resampling.BILINEAR)
    a = (np.asarray(crop, np.float32) / 255 - [0.485, 0.456, 0.406]) / [0.229, 0.224, 0.225]
    logits = sess.run(None, {'input': np.transpose(a.astype(np.float32), (2, 0, 1))[None]})[0][0]
    e = np.exp(logits - logits.max(0)); p = e / e.sum(0)
    for c in range(19):
        pc = np.asarray(Image.fromarray(p[c]).resize((s, s), Image.BILINEAR))
        X0, Y0, X1, Y1 = max(x0, 0), max(y0, 0), min(x0 + s, W), min(y0 + s, H)
        full[c, Y0:Y1, X0:X1] = np.maximum(full[c, Y0:Y1, X0:X1], pc[Y0 - y0:Y1 - y0, X0 - x0:X1 - x0])
def share(*names):
    m = np.max([full[LABELS.index(n)] for n in names], 0)
    return round(float((m > 0.5).mean()), 5)
print(json.dumps({'face_skin': share('skin', 'nose'), 'eyebrow': share('left_eyebrow', 'right_eyebrow'),
                  'lips': share('upper_lip', 'lower_lip'), 'ear': share('left_ear', 'right_ear'),
                  'neck': share('neck')}))
