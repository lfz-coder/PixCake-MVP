# PixCake third-party dependency lock: ONNX Runtime (official macOS arm64 release package).
#
# Used by apps/renderd to run the BiSeNet face-parsing model.  The official
# release binary is downloaded at configure time (PIXCAKE_ENABLE_INFERENCE=ON)
# and verified by SHA-256; nothing is committed except the licence text.
# Only macOS arm64 is in scope (user decision 2026-11-06).
# Evidence: docs/superpowers/evidence/2026-11-06-segmentation-runtime-probe.md,
#           docs/superpowers/evidence/2026-11-06-segmentation-onnxruntime-integration.md

set(PIXCAKE_ONNXRUNTIME_VERSION "1.30.0")
set(PIXCAKE_ONNXRUNTIME_TAG "v1.30.0")
set(PIXCAKE_ONNXRUNTIME_COMMIT "f2c39fe2f838cf35ce7da92824f5a5e3ee6e88a7")  # GIT_COMMIT_ID in the package
set(PIXCAKE_ONNXRUNTIME_PACKAGE_URL
    "https://github.com/microsoft/onnxruntime/releases/download/v1.30.0/onnxruntime-osx-arm64-1.30.0.tgz")
set(PIXCAKE_ONNXRUNTIME_PACKAGE_SHA256 "6ebb5062a934537c352937821f9fe9718e7de1a2db1122a93dd363ffd53a7012")
set(PIXCAKE_ONNXRUNTIME_LIBRARY "lib/libonnxruntime.1.30.0.dylib")
set(PIXCAKE_ONNXRUNTIME_LICENSE "MIT")

# Licence text shipped with the app (copy of the package LICENSE).  The
# package's ThirdPartyNotices.txt (338,088 bytes) is verified by SHA-256 and
# copied into <build>/third_party_notices/onnxruntime at configure time.
set(PIXCAKE_ONNXRUNTIME_SHA256_LICENSE "2f07c72751aed99790b8a4869cf2311df85a860b22ded05fa22803587a48922c")
set(PIXCAKE_ONNXRUNTIME_SHA256_THIRD_PARTY_NOTICES "143764b952fdb1a7c69ce653bfba74a7744d6a8a573bfb73e235fba356c83de3")

# The package's lib/cmake config points INTERFACE_INCLUDE_DIRECTORIES at
# include/onnxruntime, but the headers are in include/; cmake/PixcakeOnnxRuntime.cmake
# therefore defines its own imported target instead of find_package().

set(PIXCAKE_ONNXRUNTIME_RELEASE_BLOCKERS "")
