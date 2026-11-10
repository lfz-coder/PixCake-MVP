#include "pixcake/generated/fixture_wire_codec.hpp"
#include "pixcake/generated/generation_service.hpp"
#include "pixcake/generated/remote_tool_router.hpp"
#include "pixcake/storage/sqlite_ledger_log.hpp"
#include "support/fake_file_system.hpp"
#include "support/temp_dir.hpp"

#include <chrono>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>
#include <memory>

namespace {

int g_failures = 0;
constexpr const char* kCreative = "/fixture/creative";

void expect(bool condition, const char* expression, int line) {
  if (!condition) {
    std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, line, expression);
    ++g_failures;
  }
}

#define EXPECT(expression) expect(static_cast<bool>(expression), #expression, __LINE__)

pixcake::generate::GenerationTask task() {
  pixcake::generate::GenerationTask value;
  value.task_id = "task-1";
  value.image_id = "image-1";
  value.source_revision = 4;
  value.tool.tool_id = "official.sky-replace";
  value.tool.parameter_mode_id = "sky-default";
  value.tool.billing_rule_id = "official.sky-replace.v1";
  value.tool.output_count = 1;
  value.tool.cancellable = true;
  return value;
}

class FakeTransport final : public pixcake::net::HttpTransport {
 public:
  pixcake::net::HttpRequest request;
  std::chrono::milliseconds timeout{0};
  pixcake::net::HttpResponse response;

  pixcake::net::HttpResponse send(const pixcake::net::HttpRequest& value,
                                  std::chrono::milliseconds value_timeout) override {
    request = value;
    timeout = value_timeout;
    return response;
  }
};

class FakeCodec final : public pixcake::generated::GenerationWireCodec {
 public:
  pixcake::ResultCode encode_code = pixcake::ResultCode::Ok;
  pixcake::ResultCode decode_code = pixcake::ResultCode::Ok;
  bool encoded = false;
  bool decoded = false;
  pixcake::generated::RemoteReply decoded_reply;

  pixcake::ResultCode encode_request(const pixcake::generate::GenerationTask& value,
                                     pixcake::net::HttpRequest& out) const override {
    auto* self = const_cast<FakeCodec*>(this);
    self->encoded = true;
    if (encode_code != pixcake::ResultCode::Ok) return encode_code;
    out.method = "POST";
    out.url = "https://provider.invalid/generate";
    out.body = {static_cast<std::uint8_t>(value.tool.tool_id.size())};
    return pixcake::ResultCode::Ok;
  }

  pixcake::ResultCode decode_response(const pixcake::net::HttpResponse& response,
                                      pixcake::generated::RemoteReply& out) const override {
    auto* self = const_cast<FakeCodec*>(this);
    self->decoded = true;
    if (decode_code != pixcake::ResultCode::Ok) return decode_code;
    out = decoded_reply;
    if (response.status_code < 200 || response.status_code >= 300) {
      return pixcake::ResultCode::NetworkFailed;
    }
    return pixcake::ResultCode::Ok;
  }
};

void test_success_keeps_wire_details_in_adapter() {
  FakeTransport transport;
  transport.response.status_code = 200;
  FakeCodec codec;
  codec.decoded_reply.results.push_back({{1, 2, 3, 4}, 1, 1, {}});
  pixcake::generated::HttpRemoteTool tool(transport, codec, std::chrono::milliseconds(2500));

  const auto reply = tool.run(task());
  EXPECT(reply.code == pixcake::ResultCode::Ok);
  EXPECT(reply.results.size() == 1 && reply.results.front().image == std::vector<std::uint8_t>({1, 2, 3, 4}));
  EXPECT(codec.encoded);
  EXPECT(codec.decoded);
  EXPECT(transport.request.method == "POST");
  EXPECT(transport.request.url == "https://provider.invalid/generate");
  EXPECT(transport.timeout == std::chrono::milliseconds(2500));
}

void test_transport_failure_does_not_decode() {
  FakeTransport transport;
  transport.response.transport_code = pixcake::ResultCode::NetworkFailed;
  FakeCodec codec;
  pixcake::generated::HttpRemoteTool tool(transport, codec, std::chrono::milliseconds(100));

  const auto reply = tool.run(task());
  EXPECT(reply.code == pixcake::ResultCode::NetworkFailed);
  EXPECT(codec.encoded);
  EXPECT(!codec.decoded);
}

void test_codec_failures_are_returned_without_transport_side_effects() {
  FakeTransport transport;
  FakeCodec codec;
  codec.encode_code = pixcake::ResultCode::StateConflict;
  pixcake::generated::HttpRemoteTool tool(transport, codec, std::chrono::milliseconds(100));

  auto reply = tool.run(task());
  EXPECT(reply.code == pixcake::ResultCode::StateConflict);
  EXPECT(codec.encoded);
  EXPECT(!codec.decoded);

  codec.encode_code = pixcake::ResultCode::Ok;
  codec.decode_code = pixcake::ResultCode::ImageCorrupt;
  transport.response.status_code = 200;
  reply = tool.run(task());
  EXPECT(reply.code == pixcake::ResultCode::ImageCorrupt);
  EXPECT(codec.decoded);
}

class FixtureTransport final : public pixcake::net::HttpTransport {
 public:
  pixcake::net::HttpResponse response;
  pixcake::net::HttpRequest request;
  std::chrono::milliseconds timeout{0};

  pixcake::net::HttpResponse send(const pixcake::net::HttpRequest& value,
                                  std::chrono::milliseconds value_timeout) override {
    request = value;
    timeout = value_timeout;
    return response;
  }
};

void test_fixture_protocol_round_trip() {
  pixcake::generated::FixtureWireCodec codec;
  pixcake::net::HttpRequest request;
  EXPECT(codec.encode_request(task(), request) == pixcake::ResultCode::Ok);
  EXPECT(request.method == "POST");
  EXPECT(request.url == "fixture://generation/v1/tasks");
  EXPECT(request.headers.size() == 2);
  EXPECT(request.body.size() > 8);
  EXPECT(request.body.back() == static_cast<std::uint8_t>('1'));

  pixcake::generated::RemoteReply expected;
  expected.results.push_back({{7, 8, 9, 10}, 1, 1, {}});
  expected.results.push_back({{11, 12, 13, 14}, 2, 1, {42, 43}});
  pixcake::net::HttpResponse response;
  response.status_code = 200;
  response.body = pixcake::generated::FixtureWireCodec::encode_response(expected);
  pixcake::generated::RemoteReply actual;
  EXPECT(codec.decode_response(response, actual) == pixcake::ResultCode::Ok);
  EXPECT(actual.code == pixcake::ResultCode::Ok);
  EXPECT(actual.results.size() == 2 && actual.results.front().image == expected.results.front().image &&
         actual.results.front().width == 1 && actual.results.front().height == 1);
  EXPECT(actual.results[1].image == expected.results[1].image && actual.results[1].width == 2 &&
         actual.results[1].height == 1 && actual.results[1].continuation_parameters == std::vector<std::uint8_t>({42, 43}));
}

void test_fixture_adapter_handles_bad_wire_response() {
  FixtureTransport transport;
  transport.response.status_code = 200;
  transport.response.body = {1, 2, 3};
  pixcake::generated::FixtureWireCodec codec;
  pixcake::generated::HttpRemoteTool tool(transport, codec, std::chrono::milliseconds(1000));
  const auto reply = tool.run(task());
  EXPECT(reply.code == pixcake::ResultCode::StateConflict);
  EXPECT(transport.request.url == "fixture://generation/v1/tasks");
}

void test_fixture_transport_expands_multi_output_requests() {
  pixcake::generated::RemoteReply fixture;
  fixture.results.push_back({{1, 2, 3, 4}, 1, 1, {9}});
  pixcake::generated::FixtureHttpTransport transport(fixture);
  pixcake::generated::FixtureWireCodec codec;
  pixcake::generated::HttpRemoteTool tool(transport, codec, std::chrono::milliseconds(1000));
  auto value = task();
  value.tool.output_count = 3;

  const auto reply = tool.run(value);
  EXPECT(reply.code == pixcake::ResultCode::Ok);
  EXPECT(reply.results.size() == 3);
  EXPECT(reply.results[0].image == fixture.results[0].image &&
         reply.results[1].continuation_parameters == fixture.results[0].continuation_parameters);
}

struct Book {
  pixcake::test::TempDir dir;
  std::unique_ptr<pixcake::SqliteLedgerLog> log;
  std::unique_ptr<pixcake::LedgerBook> book;

  Book() {
    EXPECT(pixcake::SqliteLedgerLog::open(dir.path() + "/ledger.db", log) == pixcake::ResultCode::Ok);
    EXPECT(pixcake::LedgerBook::open(pixcake::FreeQueuePolicy{}, *log, book) == pixcake::ResultCode::Ok);
  }

  void queue(const std::string& task_id) {
    EXPECT(book->credit_points(20) == pixcake::ResultCode::Ok);
    pixcake::TaskQuote quote;
    quote.task_id = task_id;
    quote.tool_id = "official.sky-replace";
    quote.origin = "官方";
    quote.quoted_points = 4;
    EXPECT(book->quote(quote) == pixcake::ResultCode::Ok);
    EXPECT(book->enqueue(task_id) == pixcake::ResultCode::Ok);
  }
};

void test_fixture_adapter_runs_generation_service_end_to_end() {
  Book ledger;
  ledger.queue("fixture-task");
  pixcake::test::FakeFileSystem files;

  pixcake::generated::RemoteReply expected;
  expected.results.push_back({{11, 12, 13, 14}, 1, 1, {}});

  FixtureTransport transport;
  transport.response.status_code = 200;
  transport.response.body = pixcake::generated::FixtureWireCodec::encode_response(expected);
  pixcake::generated::FixtureWireCodec codec;
  pixcake::generated::HttpRemoteTool remote(transport, codec, std::chrono::milliseconds(1500));
  pixcake::generated::GenerationService service(*ledger.book, files, kCreative, remote);

  auto generation_task = task();
  generation_task.task_id = "fixture-task";
  const auto progress = service.run(generation_task);
  EXPECT(progress.state == pixcake::generate::TaskState::Succeeded);
  EXPECT(progress.code == pixcake::ResultCode::Ok);
  EXPECT(files.files.count(progress.pixel_file) == 1);
  EXPECT(files.files[progress.pixel_file] == expected.results.front().image);
  EXPECT(ledger.book->ledger().task_status("fixture-task") == "成功");
  EXPECT(ledger.book->ledger().creative_points() == 16);
  EXPECT(transport.request.url == "fixture://generation/v1/tasks");
}

void test_provider_neutral_router_dispatches_by_tool_id() {
  FakeTransport transport;
  transport.response.status_code = 200;
  FakeCodec codec;
  codec.decoded_reply.results.push_back({{1, 2, 3, 4}, 1, 1, {}});
  pixcake::generated::HttpRemoteTool adapter(transport, codec, std::chrono::milliseconds(100));
  pixcake::generated::RemoteToolRegistry registry;
  EXPECT(registry.add("official.sky-replace", adapter) == pixcake::ResultCode::Ok);
  EXPECT(registry.add("official.sky-replace", adapter) == pixcake::ResultCode::StateConflict);
  pixcake::generated::RoutedRemoteTool routed(registry);
  auto value = task();
  const auto reply = routed.run(value);
  EXPECT(reply.code == pixcake::ResultCode::Ok && codec.encoded);
  value.tool.tool_id = "missing.provider";
  EXPECT(routed.run(value).code == pixcake::ResultCode::UnsupportedFormat);
}

void test_catalog_descriptor_registers_router_entry() {
  const auto catalog = pixcake::generate::ToolCatalog::development_fixture();
  const auto* descriptor = catalog.find(pixcake::generate::kDevelopmentFixtureToolId);
  EXPECT(descriptor != nullptr);
  FakeTransport transport;
  transport.response.status_code = 200;
  FakeCodec codec;
  codec.decoded_reply.results.push_back({{1, 2, 3, 4}, 1, 1, {}});
  pixcake::generated::HttpRemoteTool adapter(transport, codec, std::chrono::milliseconds(100));
  pixcake::generated::RemoteToolRegistry registry;
  EXPECT(descriptor != nullptr && registry.add(*descriptor, adapter) == pixcake::ResultCode::Ok);
  pixcake::generated::RoutedRemoteTool routed(registry);
  auto value = task();
  value.tool.tool_id = pixcake::generate::kDevelopmentFixtureToolId;
  EXPECT(routed.run(value).code == pixcake::ResultCode::Ok);
}

}  // namespace

int main() {
  test_success_keeps_wire_details_in_adapter();
  test_transport_failure_does_not_decode();
  test_codec_failures_are_returned_without_transport_side_effects();
  test_fixture_protocol_round_trip();
  test_fixture_adapter_handles_bad_wire_response();
  test_fixture_transport_expands_multi_output_requests();
  test_fixture_adapter_runs_generation_service_end_to_end();
  test_provider_neutral_router_dispatches_by_tool_id();
  test_catalog_descriptor_registers_router_entry();
  return g_failures == 0 ? 0 : 1;
}
