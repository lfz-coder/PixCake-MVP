// LiteRT (TFLite C++ API) smoke test for the MediaPipe SelfieMulticlass model.
// Usage: litert_smoke <model.tflite> <input.f32> <output.f32> [threads] [runs]
// input.f32 : 1x256x256x3 float32, already normalized to [-1, 1] (RGB)
// output.f32: 1x256x256x6 float32 raw logits as produced by the model
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>
#include <vector>

#include "tflite/core/interpreter_builder.h"
#include "tflite/interpreter.h"
#include "tflite/kernels/register.h"
#include "tflite/model_builder.h"

int main(int argc, char** argv) {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  if (argc < 4) {
    std::fprintf(stderr, "usage: %s model input.f32 output.f32 [threads] [runs]\n", argv[0]);
    return 2;
  }
  const int threads = argc > 4 ? std::atoi(argv[4]) : 4;
  const int runs = argc > 5 ? std::atoi(argv[5]) : 10;

  auto model = tflite::FlatBufferModel::BuildFromFile(argv[1]);
  if (!model) { std::fprintf(stderr, "failed to load model\n"); return 1; }
#ifdef NO_DEFAULT_DELEGATE
  tflite::ops::builtin::BuiltinOpResolverWithoutDefaultDelegates resolver;
#else
  tflite::ops::builtin::BuiltinOpResolver resolver;
#endif
  std::printf("model loaded\n");
  std::unique_ptr<tflite::Interpreter> interpreter;
  tflite::InterpreterBuilder builder(*model, resolver);
  builder.SetNumThreads(threads);
  if (builder(&interpreter) != kTfLiteOk || !interpreter) { std::fprintf(stderr, "build failed\n"); return 1; }
  std::printf("interpreter built\n");
  if (interpreter->AllocateTensors() != kTfLiteOk) { std::fprintf(stderr, "allocate failed\n"); return 1; }

  TfLiteTensor* in = interpreter->input_tensor(0);
  const TfLiteTensor* out = interpreter->output_tensor(0);
  std::printf("input  %s type=%d dims=", in->name, in->type);
  for (int i = 0; i < in->dims->size; ++i) std::printf("%d ", in->dims->data[i]);
  std::printf("bytes=%zu\noutput %s type=%d dims=", in->bytes, out->name, out->type);
  for (int i = 0; i < out->dims->size; ++i) std::printf("%d ", out->dims->data[i]);
  std::printf("bytes=%zu\n", out->bytes);
  std::printf("execution plan nodes=%zu (1 node usually means fully delegated to XNNPACK)\n",
              interpreter->execution_plan().size());

  std::ifstream fin(argv[2], std::ios::binary);
  std::vector<char> data((std::istreambuf_iterator<char>(fin)), std::istreambuf_iterator<char>());
  if (data.size() != in->bytes) {
    std::fprintf(stderr, "input size %zu != tensor bytes %zu\n", data.size(), in->bytes);
    return 1;
  }
  float* input = interpreter->typed_input_tensor<float>(0);
  if (input == nullptr) { std::fprintf(stderr, "input tensor is not float32\n"); return 1; }
  std::memcpy(input, data.data(), data.size());

  double best = 1e9, total = 0;
  for (int r = 0; r < runs; ++r) {
    auto t0 = std::chrono::steady_clock::now();
    if (interpreter->Invoke() != kTfLiteOk) { std::fprintf(stderr, "invoke failed\n"); return 1; }
    double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    if (r == 0) std::printf("first invoke %.2f ms\n", ms);
    else { best = std::min(best, ms); total += ms; }
  }
  if (runs > 1) std::printf("warm invoke best %.2f ms, mean %.2f ms over %d runs, threads=%d\n", best,
                            total / (runs - 1), runs - 1, threads);

  // Writes every output tensor back to back, in output order.
  std::ofstream fout(argv[3], std::ios::binary);
  for (size_t i = 0; i < interpreter->outputs().size(); ++i) {
    const TfLiteTensor* t = interpreter->output_tensor(i);
    fout.write(reinterpret_cast<const char*>(t->data.raw), static_cast<std::streamsize>(t->bytes));
  }
  return fout ? 0 : 1;
}
