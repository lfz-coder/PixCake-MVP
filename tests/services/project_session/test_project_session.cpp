// The shell's project flow without Qt: import into a new project (5.2 naming,
// folders expanded, refused files explained), list, resolve originals through
// bookmarks (S-10), mark edited, and nothing at all while signed out (S-25).
#include "pixcake/editdoc/edit_document.hpp"
#include "pixcake/persist/file_system.hpp"
#include "pixcake/project_session/project_session.hpp"

#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
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

  // 虚拟副本（需求 §7）：编号是负数；排在原图右边、按序号；继承已提交的调整；原图和图片标识不变。
  {
    const std::int64_t a = imported.photo_ids[0];
    const std::string a_document = projects->document_directory() + "/" + ProjectSession::document_id(a) + ".pxedit";
    PosixFileSystem files;
    auto edited = EditDocument::create(ProjectSession::document_id(a));
    assert(edited->set_preview("color.global", "exposure", "1.00") == ResultCode::Ok);
    assert(edited->commit_preview(0, "color.global", "exposure", phase_one_version_limits(), files, a_document) ==
           ResultCode::Ok);
    std::int64_t copy = 0;
    assert(projects->create_virtual_copy(a, copy, diagnostic) == ResultCode::Ok && copy < 0);
    assert(ProjectSession::is_copy(copy) && !ProjectSession::is_copy(a));
    assert(projects->source_photo(copy) == a && projects->copy_sequence(copy) == 1 && projects->copy_sequence(a) == 0);
    assert(projects->document_name(copy) == "photo-" + std::to_string(a) + "-copy-1");
    assert(projects->image_id(copy) == ProjectSession::document_id(a));
    std::vector<std::uint8_t> copied;
    assert(projects->document_bytes(copy, copied) == ResultCode::Ok && !copied.empty());
    assert(EditDocument::decode(copied) && EditDocument::decode(copied)->image_id() == ProjectSession::document_id(a));
    // A copy of the copy is the next one; the list is original, copies, then the next original.
    std::int64_t second = 0;
    assert(projects->create_virtual_copy(copy, second, diagnostic) == ResultCode::Ok && second < copy);
    assert(projects->copy_sequence(second) == 2 && projects->source_photo(second) == a);
    assert(projects->list_photos(imported.project_id, photos) == ResultCode::Ok && photos.size() == 4);
    assert(photos[0].id == a && photos[1].id == copy && photos[2].id == second && photos[3].id == imported.photo_ids[1]);
    assert(photos[1].file_name == photos[0].file_name);
    // Same original file; the edit state of the original is shared.
    std::string copy_path;
    assert(projects->resolve_photo(copy, copy_path, diagnostic) == ResultCode::Ok && copy_path == shoot + "/a.BMP");
    assert(projects->load_photo(copy, row) == ResultCode::Ok && row && row->id == copy && row->absolute_path == copy_path);
    assert(projects->mark_edited(copy) == ResultCode::Ok);
    // 标记：副本的标记记在原图上，原图和副本显示一样；越界被拒。
    assert(projects->set_rating({copy, a}, 3) == ResultCode::Ok);
    assert(projects->set_flag({copy}, kFlagPick) == ResultCode::Ok);
    assert(projects->set_color({a}, kColorBlue) == ResultCode::Ok);
    assert(projects->load_photo(a, row) == ResultCode::Ok && row->rating == 3 && row->flag == kFlagPick &&
           row->color_label == kColorBlue);
    assert(projects->load_photo(copy, row) == ResultCode::Ok && row->rating == 3 && row->flag == kFlagPick);
    assert(projects->set_rating({copy}, 6) == ResultCode::StateConflict);
    assert(projects->set_flag({a}, "") == ResultCode::Ok && projects->set_color({a}, "") == ResultCode::Ok);
    assert(projects->load_photo(a, row) == ResultCode::Ok && row->flag.empty() && row->color_label.empty());
    // 复制、粘贴效果和批量同步：读写已提交的文档；没编辑过的目标得到新文档，来源不进目标。
    const std::int64_t b = imported.photo_ids[1];
    EffectClipboard clipboard;
    assert(projects->copy_effect(a, clipboard) == ResultCode::Ok && !clipboard.fragments.empty());
    std::vector<ProjectSession::EffectOutcome> outcomes;
    assert(projects->document_bytes(b, copied) == ResultCode::Ok && copied.empty());
    assert(projects->paste_effect(clipboard, {b}, outcomes) == ResultCode::Ok && outcomes.size() == 1 &&
           outcomes[0].item == b && outcomes[0].code == ResultCode::Ok);
    assert(projects->document_bytes(b, copied) == ResultCode::Ok && !copied.empty());
    assert(EditDocument::decode(copied)->effective_parameter("color.global", "exposure") ==
           std::optional<std::string>("1.00"));
    assert(projects->load_photo(b, row) == ResultCode::Ok && row->edit_status == kEditStatusEdited);
    std::vector<std::string> checked;
    for (const auto& entry : sync_tree()) {
      if (entry.enabled) checked.push_back(entry.id);
    }
    assert(!checked.empty());
    assert(projects->sync_effect(a, checked, {a, copy, b}, outcomes) == ResultCode::Ok);
    assert(outcomes.size() == 2 && outcomes[0].item == copy && outcomes[1].item == b);  // the source is skipped
    assert(projects->sync_effect(a, {"no-such-item"}, {b}, outcomes) != ResultCode::Ok);
    session.in = false;
    assert(projects->copy_effect(a, clipboard) == ResultCode::StateConflict);
    assert(projects->paste_effect(clipboard, {b}, outcomes) == ResultCode::StateConflict);
    assert(projects->sync_effect(a, checked, {b}, outcomes) == ResultCode::StateConflict);
    session.in = true;

    // 预设：从已调整的图存下来（类型按已用能力推断），重命名、覆盖、套用到别的图；坏名字被拒。
    const auto adjustable = projects->adjusted_capabilities(a);
    assert(std::find(adjustable.begin(), adjustable.end(), "color.global") != adjustable.end());
    assert(projects->adjusted_capabilities(second).size() == adjustable.size());  // a copy shares the effect
    ProjectSession::PresetDraft draft;
    draft.name = "warm";
    draft.category = "wedding";
    draft.capability_ids = {"color.global", "portrait.skin"};  // the photo has no skin edit: not stored
    assert(projects->save_preset(a, draft, diagnostic) == ResultCode::Ok);
    auto listed = projects->list_presets();
    assert(listed.size() == 1 && listed[0].id == "preset-1" && listed[0].name == "warm" &&
           listed[0].type == kPresetColor && listed[0].category == "wedding");
    ProjectSession::PresetDraft nothing = draft;
    nothing.capability_ids = {"portrait.skin"};
    assert(projects->save_preset(a, nothing, diagnostic) == ResultCode::StateConflict);
    ProjectSession::PresetDraft too_long = draft;
    too_long.name = std::string(21, 'x');
    assert(projects->save_preset(a, too_long, diagnostic) == ResultCode::StateConflict && projects->list_presets().size() == 1);
    ProjectSession::PresetDraft again = draft;
    assert(projects->save_preset(a, again, diagnostic) == ResultCode::Ok && projects->list_presets().size() == 2);
    again.overwrite_id = "preset-1";
    again.name = "warm 2";
    assert(projects->save_preset(a, again, diagnostic) == ResultCode::Ok && projects->list_presets().size() == 2 &&
           projects->list_presets()[0].name == "warm 2");
    assert(projects->delete_preset("preset-2") == ResultCode::Ok && projects->list_presets().size() == 1);
    assert(projects->rename_preset("preset-1", "warm") == ResultCode::Ok);
    assert(projects->rename_preset("preset-1", std::string(21, 'x')) == ResultCode::StateConflict);
    assert(projects->preset_overwrites_color(b, "preset-1") && !projects->preset_overwrites_color(b, "nope"));
    assert(projects->apply_preset("preset-1", {copy, b}, outcomes) == ResultCode::Ok && outcomes.size() == 2);
    assert(projects->apply_preset("nope", {b}, outcomes) == ResultCode::StateConflict);
    session.in = false;
    assert(projects->save_preset(a, draft, diagnostic) == ResultCode::StateConflict);
    assert(projects->delete_preset("preset-1") == ResultCode::StateConflict);
    assert(projects->apply_preset("preset-1", {b}, outcomes) == ResultCode::StateConflict);
    session.in = true;

    // 筛选与排序：原图被筛掉，它的副本也不出现；排序只排原图，副本跟在后面。
    assert(projects->set_rating({a}, 2) == ResultCode::Ok);
    PhotoQuery query;
    query.rating_min = 2;
    assert(projects->list_photos(imported.project_id, photos, &query) == ResultCode::Ok && photos.size() == 3);
    assert(photos[0].id == a && photos[1].id == copy && photos[2].id == second);
    query = {};
    query.edit_statuses = {""};  // 未编辑：b 已经被粘贴效果编辑过，没有了
    assert(projects->list_photos(imported.project_id, photos, &query) == ResultCode::Ok && photos.empty());
    query.edit_statuses = {kEditStatusEdited};
    assert(projects->list_photos(imported.project_id, photos, &query) == ResultCode::Ok && photos.size() == 4);
    query = {};
    query.sort = PhotoSortKey::FileName;
    query.descending = true;
    assert(projects->list_photos(imported.project_id, photos, &query) == ResultCode::Ok && photos.size() == 4);
    assert(photos[0].id == imported.photo_ids[1] && photos[1].id == a && photos[2].id == copy);
    query = {};
    query.rating_min = 4;
    query.rating_max = 2;  // 倒置
    assert(projects->list_photos(imported.project_id, photos, &query) == ResultCode::StateConflict);
    assert(projects->set_rating({a}, 0) == ResultCode::Ok);
    session.in = false;
    assert(projects->set_rating({a}, 2) == ResultCode::StateConflict);
    session.in = true;

    // 删除与回收站：删一个副本只少它自己；删原图连副本一起走；恢复副本时原图一并回来。
    assert(projects->trash_items({second}, diagnostic) == ResultCode::Ok);
    assert(projects->list_photos(imported.project_id, photos) == ResultCode::Ok && photos.size() == 3);
    std::vector<ProjectSession::TrashEntry> trash;
    assert(projects->list_trash(trash) == ResultCode::Ok && trash.size() == 1);
    assert(trash[0].subject == kTrashVirtualCopy && trash[0].copy == 2 && trash[0].name == "a.BMP" &&
           trash[0].project_name == "婚礼 2026" && trash[0].days_left == 30);
    assert(projects->restore(trash[0], diagnostic) == ResultCode::Ok);
    assert(projects->list_photos(imported.project_id, photos) == ResultCode::Ok && photos.size() == 4);
    assert(projects->trash_items({a}, diagnostic) == ResultCode::Ok);
    assert(projects->trash_items({a}, diagnostic) == ResultCode::Ok);  // already there: skipped
    assert(projects->list_photos(imported.project_id, photos) == ResultCode::Ok && photos.size() == 1);
    assert(photos[0].id == imported.photo_ids[1]);
    assert(projects->list_trash(trash) == ResultCode::Ok && trash.size() >= 1);
    ProjectSession::TrashEntry gone;
    for (const auto& entry : trash) {
      if (entry.subject == kTrashPhoto && entry.id == a) gone = entry;
    }
    assert(gone.id == a && gone.name == "a.BMP");
    assert(projects->restore(gone, diagnostic) == ResultCode::Ok);
    assert(projects->list_photos(imported.project_id, photos) == ResultCode::Ok && photos.size() >= 2);
    // 删项目：从项目列表消失，回收站里按项目列出；恢复回来；彻底删除只清索引。
    assert(projects->trash_project(mixed.project_id, diagnostic) == ResultCode::Ok);
    assert(projects->list_projects(list) == ResultCode::Ok && list.size() == 1);
    assert(projects->list_trash(trash) == ResultCode::Ok);
    ProjectSession::TrashEntry gone_project;
    for (const auto& entry : trash) {
      if (entry.subject == kTrashProject) gone_project = entry;
    }
    assert(gone_project.id == mixed.project_id && gone_project.name == "single" && gone_project.days_left == 30);
    assert(projects->restore(gone_project, diagnostic) == ResultCode::Ok);
    assert(projects->list_projects(list) == ResultCode::Ok && list.size() == 2);
    assert(projects->trash_project(mixed.project_id, diagnostic) == ResultCode::Ok);
    assert(projects->erase_trashed_project(mixed.project_id, diagnostic) == ResultCode::Ok);
    assert(projects->list_trash(trash) == ResultCode::Ok);
    for (const auto& entry : trash) assert(entry.id != mixed.project_id || entry.subject != kTrashProject);
    assert(std::ifstream(shoot + "/a.BMP").good());  // 原图还在磁盘上
    assert(projects->list_projects(list) == ResultCode::Ok && list.size() == 1);
    // 后面的测试要两个项目：补回一个（同一个文件再导入一次）
    mixed = projects->import_new_project({root + "/single.bmp"});
    assert(mixed.accepted == 1);
    // Signed out: nothing is created.
    session.in = false;
    std::int64_t refused_copy = 0;
    assert(projects->create_virtual_copy(a, refused_copy, diagnostic) == ResultCode::StateConflict);
    session.in = true;
  }

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
  // 预设跟着库一起存盘。
  assert(projects->list_presets().size() == 1 && projects->list_presets()[0].name == "warm");
  const ProjectPlace place = projects->last_place(first_project);
  assert(place.workspace == Workspace::Library && place.photo_id == first_photo);
  std::printf("project session ok\n");
  return 0;
}
