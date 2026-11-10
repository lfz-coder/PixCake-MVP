#pragma once

#include <string>
#include <cstdint>
#include <vector>

namespace pixcake::project_session {
// Local, portable project content. File references are supplied by the owning
// adapter: absolute paths for local work, validated asset keys on the wire.
// There are no database IDs or account credentials. source_access is an
// ephemeral local read capability and is deliberately excluded by the codec.
struct SnapshotItem { std::string edit_file; std::string layer_file; };
struct SnapshotPhoto {
  std::string source_file;
  std::vector<std::uint8_t> source_access;
  std::string name;
  int rating = 0;
  std::string flag;
  std::string color;
  std::vector<SnapshotItem> items; // Original first, then its virtual copies.
};
struct ProjectSnapshot {
  std::string name;
  std::vector<SnapshotPhoto> photos;
};
}  // namespace pixcake::project_session
