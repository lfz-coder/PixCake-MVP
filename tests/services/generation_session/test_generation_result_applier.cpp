#include "pixcake/generation_session/generation_result_applier.hpp"
#include "pixcake/support/checksum.hpp"
#include "support/fake_file_system.hpp"

#include <cstdio>
#include <string>

namespace {

int g_failures = 0;

void expect(bool condition, const char* expression, int line) {
  if (!condition) {
    std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, line, expression);
    ++g_failures;
  }
}

#define EXPECT(expression) expect(static_cast<bool>(expression), #expression, __LINE__)

pixcake::generate::GenerationTask task() {
  pixcake::generate::GenerationTask value;
  value.task_id = "apply-task";
  value.image_id = "image-1";
  value.source_revision = 2;
  value.tool.tool_id = "creative.fixture";
  value.tool.parameter_mode_id = "fixture-default";
  value.tool.billing_rule_id = "fixture-points";
  value.tool.output_count = 1;
  return value;
}

std::string checksum(const std::vector<std::uint8_t>& bytes) {
  char buffer[17];
  std::snprintf(buffer, sizeof(buffer), "%016llx",
                static_cast<unsigned long long>(pixcake::checksum_fnv1a64(bytes.data(), bytes.size())));
  return buffer;
}

pixcake::ipc::GenerationProgressMessage progress_for(const std::vector<std::uint8_t>& bytes) {
  pixcake::ipc::GenerationProgressMessage progress;
  progress.task_id = "apply-task";
  progress.state = pixcake::generate::TaskState::Succeeded;
  progress.code = pixcake::ResultCode::Ok;
  progress.percent = 100;
  progress.results.push_back({0, "apply-task-1", "/creative/apply-task-1.result", checksum(bytes), 1, 1, {9, 10}});
  return progress;
}

void test_verify_place_and_persist_continuation() {
  auto document = pixcake::LayerDocument::create("image-1", 2);
  EXPECT(document.has_value());
  pixcake::test::FakeFileSystem files;
  const std::vector<std::uint8_t> pixels = {1, 2, 3, 4};
  EXPECT(files.write_new("/creative/apply-task-1.result", pixels).ok);
  auto progress = progress_for(pixels);
  EXPECT(pixcake::generation_session::GenerationResultApplier::apply(
             task(), progress, *document, files, "/layers/apply.pxlayer") == pixcake::ResultCode::Ok);
  EXPECT(files.files.count("/layers/apply.pxlayer") == 1);
  const auto generations = document->generations();
  EXPECT(generations.size() == 1 && generations.front().result_id == "apply-task-1");
  EXPECT(generations.front().continuation_parameters == std::vector<std::uint8_t>({9, 10}));
  EXPECT(generations.front().pixel_file == "/creative/apply-task-1.result" &&
         generations.front().checksum == checksum(pixels) && generations.front().width == 1 &&
         generations.front().height == 1 && generations.front().layer_id == "apply-task-1");

  auto decoded = pixcake::LayerDocument::decode(files.files["/layers/apply.pxlayer"]);
  EXPECT(decoded.has_value());
  EXPECT(decoded->generations().size() == 1 &&
         decoded->generations().front().continuation_parameters == std::vector<std::uint8_t>({9, 10}));
  EXPECT(decoded->generations().front().pixel_file == "/creative/apply-task-1.result");
}

void test_bad_checksum_does_not_place() {
  auto document = pixcake::LayerDocument::create("image-1", 2);
  pixcake::test::FakeFileSystem files;
  const std::vector<std::uint8_t> pixels = {1, 2, 3, 4};
  EXPECT(files.write_new("/creative/apply-task-1.result", pixels).ok);
  auto progress = progress_for(pixels);
  progress.results.front().checksum = "bad";
  EXPECT(pixcake::generation_session::GenerationResultApplier::apply(
             task(), progress, *document, files, "/layers/apply.pxlayer") == pixcake::ResultCode::ImageCorrupt);
  EXPECT(document->generations().empty());
  EXPECT(files.files.count("/layers/apply.pxlayer") == 0);
}

void test_stale_layer_source_does_not_place() {
  auto document = pixcake::LayerDocument::create("image-1", 1);
  pixcake::test::FakeFileSystem files;
  const std::vector<std::uint8_t> pixels = {1, 2, 3, 4};
  EXPECT(files.write_new("/creative/apply-task-1.result", pixels).ok);
  EXPECT(pixcake::generation_session::GenerationResultApplier::apply(
             task(), progress_for(pixels), *document, files, "/layers/apply.pxlayer") ==
         pixcake::ResultCode::StateConflict);
  EXPECT(document->generations().empty());
  EXPECT(files.files.count("/layers/apply.pxlayer") == 0);
}

void test_verified_result_moves_to_document_thread_without_pixel_reads() {
  class CountingFiles final : public pixcake::test::FakeFileSystem {
   public:
    int pixel_reads = 0;
    pixcake::IoResult read_file(const std::string& path, std::vector<std::uint8_t>& out) override {
      if (path == "/creative/apply-task-1.result") ++pixel_reads;
      return pixcake::test::FakeFileSystem::read_file(path, out);
    }
  } files;
  const std::vector<std::uint8_t> pixels = {1, 2, 3, 4};
  EXPECT(files.write_new("/creative/apply-task-1.result", pixels).ok);
  pixcake::generation_session::VerifiedGenerationResult verified;
  auto document = pixcake::LayerDocument::create("image-1", 2);
  EXPECT(pixcake::generation_session::GenerationResultApplier::place_verified(
      verified, *document, files, "/layers/apply.pxlayer") == pixcake::ResultCode::StateConflict);
  EXPECT(pixcake::generation_session::GenerationResultApplier::verify(task(), progress_for(pixels), files, verified) ==
         pixcake::ResultCode::Ok);
  EXPECT(files.pixel_reads == 1 && document->generations().empty());
  auto stale = pixcake::LayerDocument::create("image-1", 3);
  EXPECT(pixcake::generation_session::GenerationResultApplier::place_verified(
      verified, *stale, files, "/layers/apply.pxlayer") == pixcake::ResultCode::StateConflict);
  EXPECT(!files.exists("/layers/apply.pxlayer"));
  EXPECT(pixcake::generation_session::GenerationResultApplier::place_verified(
      verified, *document, files, "/layers/apply.pxlayer") == pixcake::ResultCode::Ok);
  EXPECT(files.pixel_reads == 1 && document->generations().size() == 1);
}

}  // namespace

int main() {
  test_verify_place_and_persist_continuation();
  test_bad_checksum_does_not_place();
  test_stale_layer_source_does_not_place();
  test_verified_result_moves_to_document_thread_without_pixel_reads();
  return g_failures == 0 ? 0 : 1;
}
