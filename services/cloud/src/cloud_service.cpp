#include "pixcake/cloud/cloud_service.hpp"

#include "cloud_validation.hpp"

#include <utility>

namespace pixcake::cloud {

CloudService::CloudService(CloudSession& session, CloudGateway& gateway, LogSink* log)
    : session_(session), gateway_(gateway), log_(log) {}

ResultCode CloudService::record(const char* event, ResultCode code) const {
  LogRecord record;
  record.module = "cloud";
  record.event = event;
  record.code = code;
  // Do not copy any remote text or credentials into diagnostic records.
  write_log(log_, record);
  return code;
}

ResultCode CloudService::refresh_spaces(std::chrono::milliseconds timeout) {
  const auto finish = [this](ResultCode code) { return record("cloud.spaces.refresh", code); };
  if (!session_.signed_in() || !detail::valid_timeout(timeout)) return finish(ResultCode::StateConflict);
  const auto generation = session_.directory_generation_;
  const auto identity = session_.identity();
  auto reply = gateway_.list_spaces(identity, timeout);
  if (!session_.signed_in() || generation != session_.directory_generation_) return finish(ResultCode::StateConflict);
  if (reply.code == ResultCode::PermissionDenied) session_.replace_spaces({});
  if (reply.code != ResultCode::Ok) return finish(reply.code);
  if (!detail::valid_spaces(reply.spaces)) return finish(ResultCode::NetworkFailed);
  session_.replace_spaces(std::move(reply.spaces));
  return finish(ResultCode::Ok);
}

CloudProjectsReply CloudService::list_projects(const std::string& space_id, std::chrono::milliseconds timeout) {
  const auto finish = [this](ResultCode code, std::vector<CloudProjectSummary> projects = {}) {
    return CloudProjectsReply{record("cloud.projects.list", code), std::move(projects)};
  };
  if (!session_.signed_in() || !detail::valid_timeout(timeout) || !detail::valid_text(space_id, kMaxCloudIdBytes)) {
    return finish(ResultCode::StateConflict);
  }
  if (!session_.can_read(space_id)) return finish(ResultCode::PermissionDenied);
  const auto generation = session_.directory_generation_;
  const auto identity = session_.identity();
  auto reply = gateway_.list_projects(identity, space_id, timeout);
  if (!session_.signed_in() || generation != session_.directory_generation_) return finish(ResultCode::StateConflict);
  if (reply.code == ResultCode::PermissionDenied) session_.replace_spaces({});
  if (reply.code != ResultCode::Ok) return finish(reply.code);
  if (!detail::valid_projects(space_id, reply.projects)) return finish(ResultCode::NetworkFailed);
  return finish(ResultCode::Ok, std::move(reply.projects));
}

}  // namespace pixcake::cloud
