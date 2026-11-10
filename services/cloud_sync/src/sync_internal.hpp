#pragma once
#include "pixcake/cloud_sync/sync_engine.hpp"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <map>
#include <stdexcept>

namespace pixcake::cloud_sync {
namespace fs = std::filesystem;
using Json = nlohmann::json;
using Bytes = std::vector<std::uint8_t>;
constexpr std::size_t kChunk = 1024 * 1024;
constexpr std::size_t kManifest = 2 * kChunk;
constexpr std::uint64_t kMaximum = 8ull * 1024 * kChunk;
struct Failure { ResultCode code; bool conflict = false; };
void require(bool condition, ResultCode code = ResultCode::DocumentCorrupt);
bool key_valid(const std::string& value);
bool identifier(const std::string& value, const std::string& prefix);
bool digest_valid(const std::string& value);
std::string digest(const Bytes& bytes);
Json parse(const Bytes& bytes);
Bytes encode(const Json& json);
Bytes read(const fs::path& path, std::size_t limit);
void save(const fs::path& path, const Bytes& bytes);
void folder(const fs::path& path);
void sync_directory(const fs::path& path);
bool regular(const fs::path& path);
std::string new_job(const fs::path& root);
Json snapshot_json(const project_session::ProjectSnapshot& snapshot);
project_session::ProjectSnapshot snapshot_from_json(const Json& json);
void validate_manifest(const Json& manifest);
std::map<std::string, std::size_t> chunks(const Json& manifest);
Json capture(const project_session::ProjectSnapshot& source, const fs::path& job,
             const std::atomic_bool& cancelled, const std::function<void(Progress)>& progress);
project_session::ProjectSnapshot materialize(const Json& manifest, const fs::path& job,
                                             const std::atomic_bool& cancelled);
}
