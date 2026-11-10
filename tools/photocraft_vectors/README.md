# photocraft_vectors

Generates `tests/gpu/data/camera_raw_per_pixel_vectors.inc`: PhotoCraft's own Camera Raw
develop (`photocraft_algo::camera_raw::develop`, `float = false`, sRGB encoding) of 73 fixed
pixels under 28 settings that touch only the per-pixel stages (white balance and exposure,
Contrast, Presence, Color Mixer, Color Grading). `tests/gpu/test_camera_raw_port.cpp` checks
PixCake's C++ port against it.

- PhotoCraft commit: `885a6ddf27cc58c07faf1722e5c327cd38953657` (`third_party/photocraft.lock.cmake`).
- Not part of the CMake build and not shipped. Needs Rust ≥ 1.95 and the reference clone in
  `third_party/photocraft`.

Regenerate (after changing the PhotoCraft commit or the cases):

```sh
CARGO_TARGET_DIR=tools/photocraft_vectors/target \
  cargo run --release --manifest-path tools/photocraft_vectors/Cargo.toml \
  > tests/gpu/data/camera_raw_per_pixel_vectors.inc
```

The settings in each row follow the order of `settings()` in `src/main.rs` (44 values).

`cargo run ... -- brightness > tests/gpu/data/modern_brightness_vectors.inc` writes samples of
PhotoCraft's `compose::adjust::modern_lut(b, 0)` (Photoshop modern brightness) for ten slider
values, checked by `tests/gpu/test_brightness_port.cpp`.
