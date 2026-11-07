// The shell's export flow without Qt, against the real exportd: enqueue through
// the command entry, exportd draws and commits, the export list reads the log
// back (status, failure reason kept across a restart, retry), names collide
// into "name(1)", "original format" keeps the container, TIFF 16-bit, and
// nothing while signed out (S-25).
#include "pixcake/export_session/export_session.hpp"

#include <sys/stat.h>
#include <unistd.h>

#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>
#include <vector>

using namespace pixcake;
using namespace pixcake::export_session;

namespace {

struct Session final : SessionState {
  bool in = true;
  bool signed_in() const override { return in; }
};

struct PathAccess final : SourceAccess {
  ResolvedSource resolve(const std::vector<std::uint8_t>& bookmark) override {
    ResolvedSource out;
    out.absolute_path.assign(bookmark.begin(), bookmark.end());
    struct stat info {};
    out.code = ::stat(out.absolute_path.c_str(), &info) == 0 ? ResultCode::Ok : ResultCode::SourceMissing;
    return out;
  }
};

ResultCode path_bookmark(const std::string& path, std::vector<std::uint8_t>& out) {
  out.assign(path.begin(), path.end());
  return ResultCode::Ok;
}

void write_bmp(const std::string& path, int w, int h) {
  const int row = (w * 3 + 3) / 4 * 4;
  const int size = 54 + row * h;
  std::vector<unsigned char> b(static_cast<std::size_t>(size), 80);
  auto put = [&](int at, int value, int bytes) {
    for (int i = 0; i < bytes; ++i) b[static_cast<std::size_t>(at + i)] = static_cast<unsigned char>(value >> (8 * i));
  };
  std::fill(b.begin(), b.begin() + 54, 0);
  b[0] = 'B'; b[1] = 'M';
  put(2, size, 4); put(10, 54, 4); put(14, 40, 4); put(18, w, 4); put(22, h, 4);
  put(26, 1, 2); put(28, 24, 2); put(34, row * h, 4);
  std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(b.data()), size);
}

std::vector<unsigned char> read_all(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

// Waits until no task of the log is queued or running.
std::vector<ExportRow> settle(ExportSession& exports) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
  for (;;) {
    exports.read_progress();
    auto rows = exports.rows();
    bool busy = false;
    for (const auto& row : rows) busy = busy || (row.status != "成功" && row.status != "失败" && row.status != "取消");
    if (!busy) return rows;
    assert(std::chrono::steady_clock::now() < deadline);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
}

}  // namespace

int main() {
  const std::string root = std::string(PIXCAKE_TEST_WORK_DIR) + "/export-session-" + std::to_string(::getpid());
  ::mkdir(root.c_str(), 0700);
  const std::string shoot = root + "/shoot";
  ::mkdir(shoot.c_str(), 0700);
  write_bmp(shoot + "/a.bmp", 40, 30);
  // PIXCAKE_TEST_OTHER_VOLUME=<a folder on another volume> (a mounted disk
  // image) checks exporting to an external drive with the real exportd.
  const char* other = std::getenv("PIXCAKE_TEST_OTHER_VOLUME");
  const std::string out = other != nullptr && *other != '\0' ? std::string(other) + "/out-" + std::to_string(::getpid())
                                                              : root + "/out";
  ::mkdir(out.c_str(), 0700);

  Session session;
  PathAccess access;
  std::unique_ptr<project_session::ProjectSession> projects;
  std::string diagnostic;
  assert(project_session::ProjectSession::open(root + "/data", session, access, path_bookmark, projects,
                                               diagnostic) == ResultCode::Ok);
  const auto imported = projects->import_new_project({shoot});
  assert(imported.code == ResultCode::Ok && imported.photo_ids.size() == 1);
  const std::int64_t photo = imported.photo_ids[0];

  ExportOptions options;
  options.exportd_executable = PIXCAKE_EXPORTD_EXE;
  options.log_directory = root + "/logs";
  options.data_directory = root + "/data";
  std::unique_ptr<ExportSession> exports;
  assert(ExportSession::open(*projects, session, options, exports, diagnostic) == ResultCode::Ok);

  // Original format of a BMP is JPG; a sub-folder named after the project.
  ExportRequest request;
  request.photo_ids = {photo};
  request.folder = out;
  request.subfolder = "shoot";
  request.settings.quality = 12;
  std::vector<std::string> notices;
  assert(exports->submit(request, notices, diagnostic) == ResultCode::Ok && notices.empty());
  auto rows = settle(*exports);
  assert(rows.size() == 1 && rows[0].status == "成功" && rows[0].reason.empty() && rows[0].photo_id == photo);
  assert(rows[0].destination_path == out + "/shoot/a.jpg");
  auto bytes = read_all(out + "/shoot/a.jpg");
  assert(bytes.size() > 100 && bytes[0] == 0xFF && bytes[1] == 0xD8);

  // The same again does not overwrite: a(1).jpg.
  assert(exports->submit(request, notices, diagnostic) == ResultCode::Ok);
  rows = settle(*exports);
  assert(rows.size() == 2 && rows[0].status == "成功" && rows[0].file_name == "a(1).jpg");

  // TIFF 16-bit, next to the original.
  ExportRequest tiff = request;
  tiff.original_folder = true;
  tiff.folder.clear();
  tiff.subfolder.clear();
  tiff.settings.format = ExportFormat::Tiff16;
  assert(exports->submit(tiff, notices, diagnostic) == ResultCode::Ok);
  rows = settle(*exports);
  assert(rows[0].status == "成功" && rows[0].destination_path == shoot + "/a.tif");
  bytes = read_all(shoot + "/a.tif");
  assert(bytes.size() > 40 * 30 * 6 && ((bytes[0] == 'M' && bytes[1] == 'M') || (bytes[0] == 'I' && bytes[1] == 'I')));

  // A setting exportd cannot honour fails with its reason; retry hands it over again.
  ExportRequest limited = request;
  limited.settings.quality.reset();
  limited.settings.size_limit_kb = 500;
  assert(exports->submit(limited, notices, diagnostic) == ResultCode::Ok);
  rows = settle(*exports);
  assert(rows[0].status == "失败" && rows[0].can_retry && rows[0].reason.find("文件大小") != std::string::npos);
  const std::string failed_id = rows[0].export_id;
  assert(exports->retry(failed_id, diagnostic) == ResultCode::Ok);
  rows = settle(*exports);
  assert(rows[0].export_id == failed_id && rows[0].status == "失败");

  // The log survives the session: statuses and the reason are read back.
  exports.reset();
  assert(ExportSession::open(*projects, session, options, exports, diagnostic) == ResultCode::Ok);
  rows = exports->rows();
  assert(rows.size() == 4 && rows[0].export_id == failed_id && rows[0].reason.find("文件大小") != std::string::npos);
  assert(rows[3].status == "成功");

  // 设为默认导出路径 / 设为默认格式 are remembered across sessions.
  assert(exports->defaults().folder.empty() && !exports->defaults().format);
  ExportDefaults remembered;
  remembered.folder = out;
  remembered.format = ExportFormat::Tiff16;
  assert(exports->remember_defaults(remembered) == ResultCode::Ok);
  exports.reset();
  assert(ExportSession::open(*projects, session, options, exports, diagnostic) == ResultCode::Ok);
  assert(exports->defaults().folder == out && exports->defaults().format == ExportFormat::Tiff16);

  // A missing original is refused with a notice; signed out nothing is queued.
  ::rename((shoot + "/a.bmp").c_str(), (shoot + "/moved.bmp").c_str());
  assert(exports->submit(request, notices, diagnostic) == ResultCode::StateConflict);
  assert(notices.size() == 1 && notices[0].find("a.bmp") != std::string::npos);
  ::rename((shoot + "/moved.bmp").c_str(), (shoot + "/a.bmp").c_str());
  session.in = false;
  assert(exports->submit(request, notices, diagnostic) == ResultCode::StateConflict);
  assert(exports->rows().size() == 4);

  std::printf("export session ok\n");
  return 0;
}
