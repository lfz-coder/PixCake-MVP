# PixCake third-party dependency lock: LiteRT (TensorFlow Lite runtime).
#
# Used by apps/renderd for on-device inference (MediaPipe models).  Built from
# source through cmake/PixcakeLiteRT.cmake (via cmake/PixcakeInference.cmake) only when PIXCAKE_ENABLE_INFERENCE=ON.
# Evidence: docs/superpowers/evidence/2026-11-06-segmentation-litert-runtime-and-samples.md
# and docs/superpowers/evidence/2026-11-06-segmentation-litert-integration.md.

set(PIXCAKE_LITERT_VERSION "2.3.2")
set(PIXCAKE_LITERT_TAG "v2.3.2")
set(PIXCAKE_LITERT_COMMIT "d30595a09f74b4c4c4f669eafeb26928a92deb5a")
set(PIXCAKE_LITERT_SOURCE_URL
    "https://github.com/google-ai-edge/LiteRT/archive/d30595a09f74b4c4c4f669eafeb26928a92deb5a.tar.gz")
# Downloaded twice on 2026-11-06 with identical results (197,003,334 bytes).
set(PIXCAKE_LITERT_SOURCE_SHA256 "e32a91b759ca6f9dce3e5387f300de931f4e776aacd8ab8fee82e1efea98a6ec")
set(PIXCAKE_LITERT_LICENSE "Apache-2.0")

# LiteRT v2.3.2 builds TFLite against TensorFlow sources at tag v2.21.0-rc0
# (tflite/CMakeLists.txt).  The tag is a pre-release; we pin the commit that
# tag pointed to on 2026-11-06 instead of fetching the tag through git.
set(PIXCAKE_TENSORFLOW_TAG "v2.21.0-rc0")
set(PIXCAKE_TENSORFLOW_COMMIT "a344c313f75bbe28d00abe63f5d49cd6f233b6d0")
set(PIXCAKE_TENSORFLOW_SOURCE_URL
    "https://github.com/tensorflow/tensorflow/archive/a344c313f75bbe28d00abe63f5d49cd6f233b6d0.tar.gz")
set(PIXCAKE_TENSORFLOW_SOURCE_SHA256 "c0a4aa008aa6b626915de45ea7fd70488b74bcfef1e76f1fa9947a06516b4079")
set(PIXCAKE_TENSORFLOW_LICENSE "Apache-2.0")

# Transitive sources fetched by LiteRT's own CMake modules
# (tflite/tools/cmake/modules/*.cmake), each pinned there to a tag or commit.
# Recorded here so a change in the LiteRT pin is visible in review.
set(PIXCAKE_LITERT_TRANSITIVE
    "abseil-cpp=20260526.0@5650e9cf76d3be4318d5fa3af38ee483ddfd5e4a:Apache-2.0"
    "flatbuffers=v25.9.23@187240970746d00bbd26b0f5873ed54d2477f9f3:Apache-2.0"
    "XNNPACK=d89ef6669a14db203b3b7935b1b3862cb63fb6df:BSD-3-Clause"
    "eigen=ea13a98decd497a8c5588fb5de71b57bcf10d864:MPL-2.0(EIGEN_MPL2_ONLY)"
    "ruy=3286a34cc8de6149ac6844107dfdffac91531e72:Apache-2.0"
    "gemmlowp=16e8662c34917be0065110bfcd9cc27d30f52fdf:Apache-2.0"
    "cpuinfo=8ce83db858065145192c97af90cb668ad72a12e9:BSD-2-Clause"
    "pthreadpool=15a6644ba1c45f1acc16ac1e883efc3e56c6bed2(zip sha256 9b9fb1179b71021c0c048504eab636424c58e9c8374404754ece6d7cd90f26d4):BSD-2-Clause"
    "FP16=0a92994d729ff76a58f692d3028ca1b64b145d91(zip sha256 e66e65515fa09927b348d3d584c68be4215cfe664100d01c9dbc7655a5716d70):MIT"
    "FXdiv=63058eff77e11aa15bf531df5dd34395ec3017c8:MIT"
    "farmhash=0d859a811870d10f53a594927d0d0b97573ad06d:MIT"
    "ml_dtypes=00d98cd92ade342fef589c0470379abb27baebe9:Apache-2.0"
    "protobuf=v3.21.9@90b73ac3f0b10320315c2ca0d03a5a9b095d2f66:BSD-3-Clause"
    "fft2d=OouraFFT-v1.0:Ooura-permissive")

# Build configuration used for the evidence runs.
set(PIXCAKE_LITERT_CMAKE_OPTIONS
    "TFLITE_ENABLE_XNNPACK=ON;TFLITE_ENABLE_GPU=OFF;TFLITE_ENABLE_INSTALL=OFF;TFLITE_ENABLE_BENCHMARK_MODEL=OFF")

# Licence text shipped with the app (copy of LiteRT's own LICENSE at the commit
# above).  Transitive licence texts are collected from the fetched sources into
# <build>/third_party_notices/litert at configure time; configure fails if one
# is missing.
set(PIXCAKE_LITERT_SHA256_LICENSE "c71d239df91726fc519c6eb72d318ec65820627232b2f796219e87dcf35d0ab4")

# Release blockers: none.  Accepted risk: the TensorFlow pin comes from a
# pre-release tag chosen by LiteRT upstream (see above).
set(PIXCAKE_LITERT_RELEASE_BLOCKERS "")
