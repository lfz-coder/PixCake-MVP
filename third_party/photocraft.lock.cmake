# PixCake third-party lock: PhotoCraft (algorithm reference copy, not a build input).
#
# third_party/photocraft is a local clone used as the behaviour source for
# PhotoCraft-backed pixel capabilities (see
# docs/superpowers/specs/2026-11-09-photocraft-reference-and-porting-policy.md).
# It is not compiled, linked or shipped, and it is git-ignored. Fetch it with:
#
#   git clone --filter=blob:none https://github.com/storytold/photocraft.git third_party/photocraft
#   git -C third_party/photocraft checkout ${PIXCAKE_PHOTOCRAFT_COMMIT}
#
# When this commit changes, re-check every path and line number cited in the
# porting policy, the status matrix and the formula registry.

set(PIXCAKE_PHOTOCRAFT_URL "https://github.com/storytold/photocraft.git")
set(PIXCAKE_PHOTOCRAFT_BRANCH "main")
set(PIXCAKE_PHOTOCRAFT_COMMIT "885a6ddf27cc58c07faf1722e5c327cd38953657")
set(PIXCAKE_PHOTOCRAFT_COMMIT_DATE "2026-11-09T11:55:33+01:00")

# Upstream licence is "MIT OR Apache-2.0" (Cargo.toml, NOTICE: "at your option").
# PixCake elects MIT for any code or shader ported from PhotoCraft: keep the
# copyright line below and LICENSE-MIT with distributions that contain ported code.
# The MIT text is committed at third_party/photocraft-LICENSES/LICENSE-MIT.
set(PIXCAKE_PHOTOCRAFT_LICENSE "MIT OR Apache-2.0")
set(PIXCAKE_PHOTOCRAFT_LICENSE_ELECTED "MIT")
set(PIXCAKE_PHOTOCRAFT_COPYRIGHT "Copyright (c) 2026 ArtCraft Team and the PhotoCraft contributors")
# Not covered by the licence above and not to be reused: docs/brand/ (ArtCraft
# trademarks). Bundled fonts, icons, word lists and libfacedetection weights
# carry their own licences (PhotoCraft NOTICE, ATTRIBUTION.md).
set(PIXCAKE_PHOTOCRAFT_LICENSE_FILE "third_party/photocraft-LICENSES/LICENSE-MIT")
set(PIXCAKE_PHOTOCRAFT_USE "reference-only")
