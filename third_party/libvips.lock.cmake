# PixCake third-party dependency lock.
#
# This file records the source identity before CMake integration.  It is not a
# FetchContent declaration: builds must not download an unpinned "latest"
# dependency, and the project does not yet have a repository-local dependency
# cache for libvips.

set(PIXCAKE_LIBVIPS_VERSION "8.18.7")
set(PIXCAKE_LIBVIPS_TAG "v8.18.7")
set(PIXCAKE_LIBVIPS_COMMIT "24ad4d0")
set(PIXCAKE_LIBVIPS_SOURCE_URL
    "https://github.com/libvips/libvips/releases/download/v8.18.7/vips-8.18.7.tar.xz")
set(PIXCAKE_LIBVIPS_SOURCE_SHA256
    "5baaead3b0bb20ffdb9e9ff09aa9fda08620923df77b63b436654cb5e0b3bf94")
set(PIXCAKE_LIBVIPS_LICENSE "LGPL-2.1-or-later")
set(PIXCAKE_LIBVIPS_BUILD_SYSTEM "meson>=0.56")
set(PIXCAKE_LIBVIPS_REQUIRED_DEPS "glib-2.0>=2.52;gio-2.0;gobject-2.0;expat;threads")
set(PIXCAKE_LIBVIPS_FORMAT_DEPS "jpeg;png;tiff;raw;lcms")

# The production integration must keep optional loaders explicit.  In
# particular, ImageMagick/PDF/poppler modules are not enabled by accident for
# hostile input, and module deployment must be validated on each target.
set(PIXCAKE_LIBVIPS_DISABLED_OPTIONAL_DEPS "magick;pdfium;poppler;openslide")
