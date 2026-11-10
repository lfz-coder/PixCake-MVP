#include "sync_internal.hpp"
#include <algorithm>
#include <cctype>
#include <set>

namespace pixcake::cloud_sync {
namespace {
template <typename Body> ResultCode guarded(Body body) {
  try { body(); return ResultCode::Ok; }
  catch (const Failure& error) { return error.code; }
  catch (const fs::filesystem_error&) { return ResultCode::IoFailed; }
  catch (...) { return ResultCode::DocumentCorrupt; }
}
std::string field(const Json& json, const char* key) {
  require(json.contains(key) && json.at(key).is_string()); return json.at(key).get<std::string>();
}
std::int64_t integer(const Json& json, const char* key) {
  require(json.contains(key) && json.at(key).is_number_integer());
  if (json.at(key).is_number_unsigned()) require(json.at(key).get<std::uint64_t>() <= 0x7fffffffffffffffULL);
  const auto value = json.at(key).get<std::int64_t>(); require(value >= 0); return value;
}
Json upload_body(const Json& job) {
  Json body{{"space_id", job.at("space")}, {"project_id", job.at("project")},
            {"base_revision", job.at("revision")}, {"manifest", job.at("manifest")}};
  auto operation = encode(body);
  const auto nonce = field(job, "job"); operation.insert(operation.end(), nonce.begin(), nonce.end());
  body["operation_id"] = digest(operation); return body;
}
}
struct SyncEngine::Impl {
  fs::path root;
  std::string origin;
  net::HttpTransport& transport;
  const std::atomic_bool& cancelled;
  std::function<void(Progress)> progress;
  Impl(std::string data, std::string source, net::HttpTransport& network, const std::atomic_bool& cancel,
       std::function<void(Progress)> callback)
      : origin(std::move(source)), transport(network), cancelled(cancel), progress(std::move(callback)) {
    require(!data.empty() && !origin.empty(), ResultCode::StateConflict);
    root = fs::canonical(data) / "cloud-sync"; folder(root);
    root /= digest(Bytes(origin.begin(), origin.end())); folder(root);
  }
  void check() const { require(!cancelled.load(), ResultCode::Cancelled); }
  void notify(Phase phase, std::uint64_t done = 0, std::uint64_t total = 0) const {
    check(); if (progress) progress({phase, done, total});
  }
  Json load() const {
    const auto job = parse(read(root / "pending.json", kManifest));
    require(job.is_object() && field(job, "origin") == origin && integer(job, "format") == 1);
    const auto name = field(job, "job");
    require(identifier(name, "job-"));
    require(fs::is_directory(root / name) && !fs::is_symlink(root / name));
    folder(root / name / "chunks"); folder(root / name / "files");
    require(identifier(field(job, "space"), "sp_"));
    const auto remote = field(job, "project");
    require(remote.empty() || identifier(remote, "prj_"));
    require(field(job, "direction") == "upload" || field(job, "direction") == "download");
    integer(job, "revision"); integer(job, "local");
    if (!job.at("manifest").is_null()) validate_manifest(job.at("manifest"));
    return job;
  }
  void journal(const Json& job) const { save(root / "pending.json", encode(job)); }
  Json links() const {
    if (!fs::exists(root / "links.json")) return Json::array();
    auto rows = parse(read(root / "links.json", kManifest)); require(rows.is_array() && rows.size() <= 10000);
    for (const auto& row : rows) {
      integer(row, "local"); integer(row, "revision");
      require(identifier(field(row, "space"), "sp_") && identifier(field(row, "project"), "prj_"));
    }
    return rows;
  }
  net::HttpResponse request(const std::string& token, const std::string& method, const std::string& path,
                            Bytes body = {}, bool binary = false, bool verifying = false) {
    check();
    require(token.size() == 43 && token.find_first_not_of(
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-") == std::string::npos, ResultCode::PermissionDenied);
    net::HttpRequest request;
    request.method = method; request.url = origin + path;
    request.headers = {{"Authorization", "Bearer " + token}, {"Accept", binary ? "application/octet-stream" : "application/json"}};
    if (method == "POST") request.headers.push_back({"Content-Type", binary ? "application/octet-stream" : "application/json"});
    request.body = std::move(body); request.response_limits = {binary ? kChunk : kManifest, 32768, 128};
    auto reply = transport.send(request, std::chrono::milliseconds{verifying ? 60000 : 5000});
    check();
    require(reply.transport_code == ResultCode::Ok, reply.transport_code);
    require(reply.body.size() <= request.response_limits.body_bytes, ResultCode::NetworkFailed);
    if (reply.status_code == 401 || reply.status_code == 403) throw Failure{ResultCode::PermissionDenied};
    if (reply.status_code == 409) {
      bool conflict = false;
      try { conflict = parse(reply.body).at("error").at("code") == "revision_conflict"; } catch (...) {}
      throw Failure{ResultCode::StateConflict, conflict};
    }
    require(reply.status_code == 200 || reply.status_code == 204,
            reply.status_code == 413 ? ResultCode::DiskFull : ResultCode::NetworkFailed);
    if (reply.status_code == 204) require(reply.body.empty(), ResultCode::NetworkFailed);
    else {
      std::size_t types = 0;
      for (const auto& header : reply.headers) {
        auto name = header.first;
        std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return std::tolower(c); });
        if (name == "content-type") {
          ++types;
          const auto media = header.second.substr(0, header.second.find(';'));
          require(media == (binary ? "application/octet-stream" : "application/json"), ResultCode::NetworkFailed);
        }
      }
      require(types == 1, ResultCode::NetworkFailed);
    }
    return reply;
  }
  Json json(const std::string& token, const std::string& method, const std::string& path, const Json* body = nullptr,
            bool verifying = false) {
    auto reply = request(token, method, path, body ? encode(*body) : Bytes{}, false, verifying);
    require(reply.status_code == 200, ResultCode::NetworkFailed);
    auto value = parse(reply.body);
    require(value.is_object() && integer(value, "api_version") == 1, ResultCode::NetworkFailed);
    return value;
  }
};
SyncEngine::SyncEngine(std::string data, std::string origin, net::HttpTransport& transport,
                       const std::atomic_bool& cancelled, std::function<void(Progress)> progress)
    : impl_(std::make_unique<Impl>(std::move(data), std::move(origin), transport, cancelled, std::move(progress))) {}
SyncEngine::~SyncEngine() = default;
bool SyncEngine::pending() const { return fs::exists(impl_->root / "pending.json"); }
ResultCode SyncEngine::prepare_upload(const project_session::ProjectSnapshot& snapshot, std::int64_t local,
                                     const std::string& space) {
  fs::path created;
  const auto code = guarded([&] {
    require(!pending() && local > 0 && identifier(space, "sp_"), ResultCode::StateConflict);
    const auto name = new_job(impl_->root); created = impl_->root / name;
    impl_->notify(Phase::Preparing);
    auto manifest = capture(snapshot, created, impl_->cancelled, impl_->progress);
    Json job{{"format", 1}, {"origin", impl_->origin}, {"job", name}, {"direction", "upload"}, {"local", local},
             {"space", space}, {"project", ""}, {"revision", 0}, {"manifest", std::move(manifest)}};
    for (const auto& link : impl_->links()) {
      if (integer(link, "local") == local && field(link, "space") == space) {
        job["project"] = link.at("project"); job["revision"] = link.at("revision"); break;
      }
    }
    impl_->check(); impl_->journal(job);
  });
  if (code != ResultCode::Ok && !created.empty()) { std::error_code error; fs::remove_all(created, error); }
  return code;
}
ResultCode SyncEngine::prepare_download(const std::string& project, const std::string& space, std::int64_t revision) {
  return guarded([&] {
    require(!pending() && identifier(project, "prj_") && identifier(space, "sp_") && revision > 0, ResultCode::StateConflict);
    const auto name = new_job(impl_->root);
    impl_->journal({{"format", 1}, {"origin", impl_->origin}, {"job", name}, {"direction", "download"}, {"local", 0},
                    {"space", space}, {"project", project}, {"revision", revision}, {"manifest", nullptr}});
  });
}
ResultCode SyncEngine::keep_both(const std::string& token) {
  return guarded([&] {
    auto job = impl_->load(); require(field(job, "direction") == "upload", ResultCode::StateConflict);
    if (job.contains("upload_id")) {
      const auto upload = field(job, "upload_id"); require(identifier(upload, "up_"));
      const auto reply = impl_->request(token, "DELETE", "/v1/sync/uploads/" + upload);
      require(reply.status_code == 204, ResultCode::NetworkFailed);
      job.erase("upload_id");
    }
    job.erase("attempted");
    job["project"] = ""; job["revision"] = 0; impl_->journal(job);
  });
}
ResultCode SyncEngine::discard(const std::string& token) {
  return guarded([&] {
    auto job = impl_->load();
    job["discard"] = true; impl_->journal(job);
    if (field(job, "direction") == "upload" && job.value("attempted", false)) {
      if (!job.contains("upload_id")) {
        auto body = upload_body(job);
        try {
          const auto started = impl_->json(token, "POST", "/v1/sync/uploads", &body, true);
          job["upload_id"] = field(started, "upload_id"); impl_->journal(job);
        } catch (const Failure& error) { if (!error.conflict) throw; }
      }
      if (job.contains("upload_id")) {
        const auto upload = field(job, "upload_id"); require(identifier(upload, "up_"));
        const auto reply = impl_->request(token, "DELETE", "/v1/sync/uploads/" + upload);
        require(reply.status_code == 204, ResultCode::NetworkFailed);
      }
    }
    require(fs::remove(impl_->root / "pending.json"), ResultCode::IoFailed);
    sync_directory(impl_->root);
    const auto directory = impl_->root / field(job, "job");
    if (field(job, "direction") == "upload") fs::remove_all(directory);
    else fs::remove_all(directory / "chunks"); // A previous local commit may already reference files.
  });
}
TransferResult SyncEngine::run(const std::string& token) {
  TransferResult result;
  try {
    auto job = impl_->load(); const auto directory = impl_->root / field(job, "job");
    if (job.value("discard", false)) { result.code = discard(token); result.discarded = result.code == ResultCode::Ok; return result; }
    const bool downloading = field(job, "direction") == "download";
    result.downloading = downloading; result.local_id = integer(job, "local");
    const auto space = field(job, "space"); const auto project = field(job, "project");
    auto revision = integer(job, "revision");
    if (!downloading) {
      impl_->notify(Phase::Uploading);
      auto body = upload_body(job);
      job["attempted"] = true; impl_->journal(job);
      const auto started = impl_->json(token, "POST", "/v1/sync/uploads", &body, true);
      const auto upload = field(started, "upload_id");
      require(identifier(upload, "up_") && identifier(field(started, "project_id"), "prj_"), ResultCode::NetworkFailed);
      job["upload_id"] = upload; impl_->journal(job);
      if (!project.empty()) require(field(started, "project_id") == project, ResultCode::NetworkFailed);
      const auto sizes = chunks(job.at("manifest"));
      const auto& missing = started.at("missing");
      require(missing.is_array() && missing.size() <= sizes.size(), ResultCode::NetworkFailed);
      std::set<std::string> seen;
      std::uint64_t total = 0, done = 0;
      for (const auto& entry : missing) {
        require(entry.is_string(), ResultCode::NetworkFailed); const auto hash = entry.get<std::string>();
        require(sizes.count(hash) && seen.insert(hash).second, ResultCode::NetworkFailed); total += sizes.at(hash);
      }
      for (const auto& hash : seen) {
        auto bytes = read(directory / "chunks" / hash, kChunk);
        require(bytes.size() == sizes.at(hash) && digest(bytes) == hash);
        const auto response = impl_->request(token, "POST", "/v1/sync/uploads/" + upload + "/chunks/" + hash,
                                              std::move(bytes), true);
        require(response.status_code == 204, ResultCode::NetworkFailed);
        done += sizes.at(hash); impl_->notify(Phase::Uploading, done, total);
      }
      Json empty = Json::object();
      const auto committed = impl_->json(token, "POST", "/v1/sync/uploads/" + upload + "/commit", &empty, true);
      require(field(committed, "project_id") == field(started, "project_id") &&
              integer(committed, "revision") == revision + 1, ResultCode::NetworkFailed);
      // Preserve the original base revision/operation for retries, including a
      // crash between server commit and local association persistence.
      job["committed_project"] = committed.at("project_id"); job["committed_revision"] = committed.at("revision");
      impl_->journal(job); result.project_id = field(committed, "project_id"); result.revision = integer(committed, "revision");
    } else {
      impl_->notify(Phase::Downloading);
      const auto path = "/v1/sync/projects/" + project + "/revisions/" + std::to_string(revision);
      const auto remote = impl_->json(token, "GET", path);
      require(field(remote, "project_id") == project && field(remote, "space_id") == space &&
              integer(remote, "revision") == revision, ResultCode::NetworkFailed);
      validate_manifest(remote.at("manifest"));
      if (!job.at("manifest").is_null()) require(job.at("manifest") == remote.at("manifest"), ResultCode::NetworkFailed);
      job["manifest"] = remote.at("manifest"); impl_->journal(job);
      const auto sizes = chunks(job.at("manifest"));
      std::uint64_t total = 0, done = 0; for (const auto& size : sizes) total += size.second;
      for (const auto& size : sizes) {
        impl_->check(); const auto file = directory / "chunks" / size.first;
        bool cached = false;
        if (regular(file)) {
          try {
            const auto bytes = read(file, kChunk); cached = bytes.size() == size.second && digest(bytes) == size.first;
          } catch (const Failure&) { cached = false; }
        }
        if (!cached) {
          auto response = impl_->request(token, "GET", path + "/chunks/" + size.first, {}, true);
          require(response.status_code == 200 && response.body.size() == size.second && digest(response.body) == size.first,
                  ResultCode::NetworkFailed);
          save(file, response.body);
        }
        done += size.second; impl_->notify(Phase::Downloading, done, total);
      }
      impl_->notify(Phase::Restoring);
      result.snapshot = materialize(job.at("manifest"), directory, impl_->cancelled);
      result.owned_root = (directory / "files").string(); result.receipt = directory.string();
      result.project_id = project; result.revision = revision;
    }
    result.space_id = space; impl_->check();
  } catch (const Failure& failure) { result.code = failure.code; result.conflict = failure.conflict; }
  catch (const fs::filesystem_error&) { result.code = ResultCode::IoFailed; }
  catch (...) { result.code = ResultCode::DocumentCorrupt; }
  return result;
}
ResultCode SyncEngine::complete(std::int64_t local) {
  return guarded([&] {
    impl_->check(); require(local > 0, ResultCode::StateConflict);
    auto job = impl_->load(); auto links = impl_->links();
    const bool downloading = field(job, "direction") == "download";
    const auto project = field(job, downloading ? "project" : "committed_project");
    const auto revision = integer(job, downloading ? "revision" : "committed_revision");
    const auto space = field(job, "space"); require(identifier(project, "prj_") && revision > 0);
    links.erase(std::remove_if(links.begin(), links.end(), [&](const Json& link) {
      return integer(link, "local") == local && field(link, "space") == space;
    }), links.end());
    require(links.size() < 10000);
    links.push_back({{"local", local}, {"space", space}, {"project", project}, {"revision", revision}});
    save(impl_->root / "links.json", encode(links));
    const auto directory = impl_->root / field(job, "job");
    // Unlink the pending journal first. Its chunks/files are only removed once
    // there is no resumable job referring to them.
    require(fs::remove(impl_->root / "pending.json"), ResultCode::IoFailed);
    sync_directory(impl_->root);
    if (!downloading) fs::remove_all(directory);
    else fs::remove_all(directory / "chunks");
  });
}
}  // namespace pixcake::cloud_sync
