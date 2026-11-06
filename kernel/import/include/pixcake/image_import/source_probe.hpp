#pragma once

#include "pixcake/persist/file_system.hpp"
#include "pixcake/project/admitted_source.hpp"
#include "pixcake/support/result_code.hpp"

#include <cstdint>
#include <optional>
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

// 内容准入：probe_source 成功后签发项目库唯一接受的原图凭证。失败时 out 为空，结果码与 probe_source 相同。
ResultCode admit_source(FileSystem& files, const std::string& absolute_path,
                        std::optional<AdmittedSource>& out, SourceProbe* details = nullptr);

}  // namespace pixcake::image_import
