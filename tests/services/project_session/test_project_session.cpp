// The shell's project flow without Qt: import into a new project (5.2 naming,
// folders expanded, refused files explained), list, resolve originals through
// bookmarks (S-10), mark edited, and nothing at all while signed out (S-25).
#include "pixcake/project_session/project_session.hpp"

#include <sys/stat.h>
#include <unistd.h>

#include <cassert>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

using namespace pixcake;
using namespace pixcake::project_session;

namespace {

struct Session final : SessionState {
  bool in = true;
  bool signed_in() const override { return in; }
};

// Test bookmarks are the path bytes; resolving checks the file is still there.
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

}  // namespace

int main() {
  const std::string root = std::string(PIXCAKE_TEST_WORK_DIR) + "/project-session-" + std::to_string(::getpid());
  ::mkdir(root.c_str(), 0700);
  const std::string shoot = root + "/婚礼 2026";
  ::mkdir(shoot.c_str(), 0700);
  write_bmp(shoot + "/b.bmp", 8, 4);
  write_bmp(shoot + "/a.BMP", 8, 4);
  std::ofstream(shoot + "/notes.txt") << "not a photo";
  write_bmp(shoot + "/.hidden.bmp", 8, 4);
  write_bmp(root + "/single.bmp", 8, 4);
  std::ofstream(root + "/broken.jpg") << "\xff\xd8 broken";

  // Naming and folder expansion (requirement 5.2).
  assert(project_name_for({shoot, root + "/single.bmp"}) == "婚礼 2026");
  assert(project_name_for({root + "/single.bmp", shoot}) == "婚礼 2026");
  assert(project_name_for({root + "/single.bmp"}) == "single");
  const auto files = expand_import_paths({shoot});
  assert(files.size() == 2 && files[0] == shoot + "/a.BMP" && files[1] == shoot + "/b.bmp");

  Session session;
  PathAccess access;
  std::unique_ptr<ProjectSession> projects;
  std::string diagnostic;
  assert(ProjectSession::open(root + "/data", session, access, path_bookmark, projects, diagnostic) == ResultCode::Ok);
  assert(ProjectSession::document_id(12) == "photo-12");

  // Signed out: nothing is created.
  session.in = false;
  auto refused = projects->import_new_project({shoot});
  assert(refused.code == ResultCode::StateConflict && refused.project_id == 0);
  std::vector<ProjectRow> list;
  assert(projects->list_projects(list) == ResultCode::Ok && list.empty());
  session.in = true;

  // A folder: one project named after it, both images in name order.
  auto imported = projects->import_new_project({shoot});
  assert(imported.code == ResultCode::Ok && imported.project_id > 0);
  assert(imported.project_name == "婚礼 2026" && imported.accepted == 2 && imported.photo_ids.size() == 2);
  std::vector<PhotoRow> photos;
  assert(projects->list_photos(imported.project_id, photos) == ResultCode::Ok && photos.size() == 2);
  assert(photos[0].file_name == "a.BMP" && photos[1].file_name == "b.bmp");

  // Chosen files: a broken one is explained, the good one imported.
  auto mixed = projects->import_new_project({root + "/single.bmp", root + "/broken.jpg"});
  assert(mixed.project_name == "single" && mixed.accepted == 1);
  assert(mixed.notices.size() == 1 && mixed.notices[0].find("broken.jpg") != std::string::npos);
  assert(projects->list_projects(list) == ResultCode::Ok && list.size() == 2);

  // Appending to a project.
  auto appended = projects->import_into(mixed.project_id, {shoot + "/a.BMP"});
  assert(appended.accepted == 1);
  assert(projects->list_photos(mixed.project_id, photos) == ResultCode::Ok && photos.size() == 2);

  // Originals through bookmarks.
  std::string path;
  assert(projects->resolve_photo(imported.photo_ids[0], path, diagnostic) == ResultCode::Ok && path == shoot + "/a.BMP");
  ::rename((shoot + "/b.bmp").c_str(), (root + "/moved.bmp").c_str());
  assert(projects->resolve_photo(imported.photo_ids[1], path, diagnostic) == ResultCode::SourceMissing);
  std::optional<PhotoRow> row;
  assert(projects->load_photo(imported.photo_ids[1], row) == ResultCode::Ok && row && row->presence == kPresenceMissing);

  // Edited state for the library.
  assert(projects->mark_edited(imported.photo_ids[0]) == ResultCode::Ok);
  assert(projects->load_photo(imported.photo_ids[0], row) == ResultCode::Ok && row->edit_status == kEditStatusEdited);

  // Never edited: no document bytes; the library does not invent one.
  std::vector<std::uint8_t> document;
  assert(projects->document_bytes(imported.photo_ids[0], document) == ResultCode::Ok && document.empty());

  // Where a project was left: refine by default, then whatever was remembered.
  const auto first_project = imported.project_id;
  const auto first_photo = imported.photo_ids[0];
  assert(projects->last_place(first_project).workspace == Workspace::Refine);
  assert(!projects->last_place(first_project).photo_id);
  assert(projects->remember_place(first_project, {Workspace::Library, first_photo}) == ResultCode::Ok);
  assert(projects->remember_place(first_project, {Workspace::Library, first_photo}) == ResultCode::Ok);  // overwrite
  // A photo id from another project is not trusted.
  assert(projects->remember_place(list[1].id == first_project ? list[0].id : list[1].id,
                                  {Workspace::Library, first_photo}) == ResultCode::Ok);
  assert(!projects->last_place(list[1].id == first_project ? list[0].id : list[1].id).photo_id);

  // Reopening the library keeps everything.
  projects.reset();
  assert(ProjectSession::open(root + "/data", session, access, path_bookmark, projects, diagnostic) == ResultCode::Ok);
  assert(projects->list_projects(list) == ResultCode::Ok && list.size() == 2);
  const ProjectPlace place = projects->last_place(first_project);
  assert(place.workspace == Workspace::Library && place.photo_id == first_photo);
  std::printf("project session ok\n");
  return 0;
}
