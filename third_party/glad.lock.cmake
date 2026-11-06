# PixCake third-party dependency lock: Glad OpenGL loader (generated sources).
#
# These are generated files committed to the repository, not a download.
# The values below are copied from the header of third_party/glad/include/glad/gl.h
# and from `shasum -a 256` of the committed files (2026-11-05).

set(PIXCAKE_GLAD_GENERATOR "glad")
set(PIXCAKE_GLAD_GENERATOR_VERSION "2.0.8")
set(PIXCAKE_GLAD_GENERATED_AT "Fri Sep 25 08:13:49 2026")
set(PIXCAKE_GLAD_COMMANDLINE "--api='gl:core=3.3' --extensions='' c --loader")
set(PIXCAKE_GLAD_ONLINE_URL "http://glad.sh/#api=gl%3Acore%3D3.3&extensions=&generator=c&options=LOADER")
set(PIXCAKE_GLAD_OPTIONS "ALIAS=False;DEBUG=False;HEADER_ONLY=False;LOADER=True;MX=False;ON_DEMAND=False")

# SPDX expression printed in gl.h and gl.c. khrplatform.h carries the Khronos
# MIT-style notice inline in its own header comment.
set(PIXCAKE_GLAD_LICENSE "(WTFPL OR CC0-1.0) AND Apache-2.0")
set(PIXCAKE_GLAD_KHRPLATFORM_LICENSE "Khronos MIT-style notice embedded in KHR/khrplatform.h")

set(PIXCAKE_GLAD_SHA256_GL_H "b30136388eb5c291ff5fb5715e049298a57017625fdb8e706e09df8356336a0e")
set(PIXCAKE_GLAD_SHA256_KHRPLATFORM_H "7b1e01aaa7ad8f6fc34b5c7bdf79ebf5189bb09e2c4d2e79fc5d350623d11e83")
set(PIXCAKE_GLAD_SHA256_GL_C "651d10fd3028c0f60be22ef6db8bc30b8287b5121022a0da3e8c3f34f047881e")

# Release blockers (see docs/superpowers/evidence/2026-11-05-dependency-records.md):
#   1. Regenerate with glad 2.0.8 and the command line above, and confirm the output
#      matches these files apart from the timestamp line.
#   2. Ship the Apache-2.0 licence text (and the chosen CC0-1.0 or WTFPL text) with the app.
set(PIXCAKE_GLAD_RELEASE_BLOCKERS "regeneration-not-verified;licence-texts-not-bundled")
