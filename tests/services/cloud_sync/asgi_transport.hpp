#pragma once
#include "pixcake/net/http_transport.hpp"
#include <nlohmann/json.hpp>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
#include <cassert>
#include <cstdio>
#include <cstdlib>
extern char** environ;

// A test-only pipe adapter. Request/response bytes run through the actual
// FastAPI application; this deliberately makes no claim about TLS transport.
struct AsgiTransport : pixcake::net::HttpTransport {
  pid_t pid = -1;
  FILE* input = nullptr;
  FILE* output = nullptr;
  std::string token, space;
  AsgiTransport(const char* python, const char* script) {
    int incoming[2], outgoing[2]; assert(::pipe(incoming) == 0 && ::pipe(outgoing) == 0);
    posix_spawn_file_actions_t actions; assert(posix_spawn_file_actions_init(&actions) == 0);
    posix_spawn_file_actions_adddup2(&actions, incoming[0], STDIN_FILENO);
    posix_spawn_file_actions_adddup2(&actions, outgoing[1], STDOUT_FILENO);
    for (int descriptor : {incoming[0], incoming[1], outgoing[0], outgoing[1]})
      posix_spawn_file_actions_addclose(&actions, descriptor);
    char* arguments[] = {const_cast<char*>(python), const_cast<char*>(script), nullptr};
    const int code = ::posix_spawn(&pid, python, &actions, nullptr, arguments, environ);
    posix_spawn_file_actions_destroy(&actions); assert(code == 0);
    ::close(incoming[0]); ::close(outgoing[1]);
    input = ::fdopen(incoming[1], "w"); output = ::fdopen(outgoing[0], "r"); assert(input && output);
    const auto greeting = receive(); token = greeting.at("token"); space = greeting.at("space");
  }
  ~AsgiTransport() override {
    if (input) std::fclose(input);
    if (output) std::fclose(output);
    int status = 0; if (pid > 0) ::waitpid(pid, &status, 0);
  }
  nlohmann::json receive() {
    char* data = nullptr; std::size_t capacity = 0;
    const auto count = ::getline(&data, &capacity, output); assert(count > 0 && count <= 16 * 1024 * 1024);
    auto value = nlohmann::json::parse(data, data + count); std::free(data); return value;
  }
  pixcake::net::HttpResponse send(const pixcake::net::HttpRequest& request, std::chrono::milliseconds) override {
    const auto slash = request.url.find('/', 8); assert(slash != std::string::npos);
    nlohmann::json message{{"method", request.method}, {"path", request.url.substr(slash)},
                           {"headers", request.headers}, {"body", request.body}};
    const auto line = message.dump() + "\n";
    assert(std::fwrite(line.data(), 1, line.size(), input) == line.size() && std::fflush(input) == 0);
    const auto value = receive();
    pixcake::net::HttpResponse response; response.status_code = value.at("status");
    response.headers = value.at("headers").get<std::vector<pixcake::net::Header>>();
    response.body = value.at("body").get<std::vector<std::uint8_t>>(); return response;
  }
};
