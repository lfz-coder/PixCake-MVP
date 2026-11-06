#pragma once

#include "pixcake/persist/file_system.hpp"
#include "pixcake/support/result_code.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace pixcake::image_import {

enum class SourceFormat {
  Jpeg,
  Png,
  Tiff,
  Bmp,
};

struct SourceProbe {
  std::string absolute_path;
  SourceFormat format = SourceFormat::Jpeg;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  FileIdentity identity;
};

const char* source_format_name(SourceFormat format);

// Reads only format headers from an already loaded byte sequence.  It does not
// decode pixels or retain the source bytes in the result.
ResultCode probe_bytes(const std::vector<std::uint8_t>& bytes, SourceFormat& format,
                       std::uint32_t& width, std::uint32_t& height);

// Admits one absolute source path and records its persistent file identity.
// The caller may continue a batch after a non-success result.
ResultCode probe_source(FileSystem& files, const std::string& absolute_path, SourceProbe& out);

}  // namespace pixcake::image_import
