#include <pixcake/gl/qt_backend.hpp>
#include <pixcake/gpu/render_executor.hpp>

#include <QGuiApplication>

#include <cassert>
#include <cmath>
#include <iostream>

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  pixcake::gl::Context context = pixcake::gl::qt::create_context();
  pixcake::gpu::RgbaImage input;
  input.width = 1; input.height = 1; input.row_stride = 4; input.pixels = {96, 128, 160, 255};
  pixcake::render::RenderDescription description;
  description.document_revision = 19;
  pixcake::render::RenderOperation operation;
  operation.capability_id = "color.global";
  operation.stage = "color";
  operation.program = "pixcake.color.global";
  // Exercise all four channels whose formulas and bindings are registered:
  // Bradford white balance, EV exposure, and affine contrast.
  operation.values = {{"temperature", "temperature", "25", 25.0},
                      {"tint", "tint", "-20", -20.0},
                      {"exposure", "exposure", "0.25", 0.25},
                      {"contrast", "contrast", "20", 20.0}};
  description.operations.push_back(operation);

  pixcake::gpu::RenderExecutor executor;
  pixcake::gpu::RenderInputs inputs;
  pixcake::gpu::RenderResult gpu_result;
  std::string error;
  if (!executor.execute(context, description, input, inputs, gpu_result, error)) {
    // Some CI machines have Qt but no usable offscreen OpenGL implementation.
    std::cerr << "GPU shader path skipped: " << error << '\n';
    return 0;
  }
  std::cout << "GPU shader path active\n";
  pixcake::gpu::RenderResult reference;
  assert(executor.renderReference(description, input, inputs, reference));
  assert(gpu_result.success);
  assert(!gpu_result.reference_fallback);
  assert(gpu_result.image.revision == description.document_revision);
  for (std::size_t i = 0; i < 3; ++i) {
    assert(std::abs(static_cast<int>(gpu_result.image.pixels[i]) - static_cast<int>(reference.image.pixels[i])) <= 3);
  }
  return 0;
}
