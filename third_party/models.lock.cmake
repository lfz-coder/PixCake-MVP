# PixCake model lock: on-device inference models used by apps/renderd.
#
# Models are not committed.  With PIXCAKE_ENABLE_INFERENCE=ON, configure downloads
# each file into <build>/models and verifies its SHA-256 (a mismatch fails the
# configure step).  Versioned URLs only; "latest" URLs are not allowed.
# Licence: the MediaPipe model cards state "Apache License, Version 2.0".  A
# model under any other terms must also record PIXCAKE_MODEL_<NAME>_USE_TERMS.
# Evidence: docs/superpowers/evidence/2026-11-06-segmentation-license-audit-mediapipe-vision.md
#           docs/superpowers/evidence/2026-11-06-segmentation-face-detection.md

set(PIXCAKE_MODEL_SELFIE_MULTICLASS_FILE "selfie_multiclass_256x256.tflite")
set(PIXCAKE_MODEL_SELFIE_MULTICLASS_URL
    "https://storage.googleapis.com/mediapipe-models/image_segmenter/selfie_multiclass_256x256/float32/1/selfie_multiclass_256x256.tflite")
set(PIXCAKE_MODEL_SELFIE_MULTICLASS_SHA256 "c6748b1253a99067ef71f7e26ca71096cd449baefa8f101900ea23016507e0e0")
set(PIXCAKE_MODEL_SELFIE_MULTICLASS_CARD
    "https://storage.googleapis.com/mediapipe-assets/Model%20Card%20Multiclass%20Segmentation.pdf")
set(PIXCAKE_MODEL_SELFIE_MULTICLASS_LICENSE "Apache-2.0")

set(PIXCAKE_MODEL_BLAZEFACE_FULL_RANGE_FILE "blaze_face_full_range.tflite")
set(PIXCAKE_MODEL_BLAZEFACE_FULL_RANGE_URL
    "https://storage.googleapis.com/mediapipe-models/face_detector/blaze_face_full_range/float16/1/blaze_face_full_range.tflite")
set(PIXCAKE_MODEL_BLAZEFACE_FULL_RANGE_SHA256 "3698b18f063835bc609069ef052228fbe86d9c9a6dc8dcb7c7c2d69aed2b181b")
set(PIXCAKE_MODEL_BLAZEFACE_FULL_RANGE_CARD
    "https://storage.googleapis.com/mediapipe-assets/MediaPipe%20BlazeFace%20Model%20Card%20(Full%20Range).pdf")
set(PIXCAKE_MODEL_BLAZEFACE_FULL_RANGE_LICENSE "Apache-2.0")

# BiSeNet face parsing (yakhyo/face-parsing v0.0.2, commit 5a8613c), run with ONNX Runtime.
set(PIXCAKE_MODEL_BISENET_RESNET18_FILE "bisenet_resnet18.onnx")
set(PIXCAKE_MODEL_BISENET_RESNET18_URL
    "https://github.com/yakhyo/face-parsing/releases/download/v0.0.2/resnet18.onnx")
set(PIXCAKE_MODEL_BISENET_RESNET18_SHA256 "0d9bd318e46987c3bdbfacae9e2c0f461cae1c6ac6ea6d43bbe541a91727e33f")
set(PIXCAKE_MODEL_BISENET_RESNET18_LICENSE "MIT (code and release)")
# The weights are trained on CelebAMask-HQ, whose terms allow non-commercial
# research and educational use only.  Acceptable because PixCake is not used
# commercially (user decision 2026-11-06, dependency baseline section 2.2);
# must be re-audited before any commercial use.
set(PIXCAKE_MODEL_BISENET_RESNET18_USE_TERMS
    "CelebAMask-HQ training data: non-commercial research/education only; attribution required")

set(PIXCAKE_MODEL_LIST "SELFIE_MULTICLASS;BLAZEFACE_FULL_RANGE;BISENET_RESNET18")

# Test-only fixture (not shipped): MediaPipe's own test image, an official US
# government portrait (public domain).  Downloaded with the models.
set(PIXCAKE_TESTDATA_PORTRAIT_FILE "portrait.jpg")
set(PIXCAKE_TESTDATA_PORTRAIT_URL "https://storage.googleapis.com/mediapipe-assets/portrait.jpg")
set(PIXCAKE_TESTDATA_PORTRAIT_SHA256 "a6f11efaa834706db23f275b6115058fa87fc7f14362681e6abe14e82749de3e")

set(PIXCAKE_MODELS_SHA256_LICENSE_APACHE "cfc7749b96f63bd31c3c42b5c471bf756814053e847c10f3eb003417bc523d30")
set(PIXCAKE_MODELS_RELEASE_BLOCKERS "")
