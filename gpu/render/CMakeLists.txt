add_library(pixcake_gpu_render src/render_executor.cpp src/geometry.cpp src/camera_raw.cpp src/brightness.cpp src/adjust.cpp src/camera_raw_spatial.cpp src/camera_raw_gl.cpp
  src/lens.cpp)
target_include_directories(pixcake_gpu_render PUBLIC include)
target_link_libraries(pixcake_gpu_render PUBLIC pixcake_gpu_facade pixcake_render pixcake_warnings)
# PhotoCraft (Rust) never fuses a multiply and an add; clang does by default.  Keeping every
# rounding step makes the ported formulas bit-exact with PhotoCraft and the CPU paths agree
# with each other whatever the compiler inlines.
target_compile_options(pixcake_gpu_render PRIVATE $<$<CXX_COMPILER_ID:Clang,AppleClang,GNU>:-ffp-contract=off>)
