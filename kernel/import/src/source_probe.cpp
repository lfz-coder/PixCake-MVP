#include "pixcake/image_import/source_probe.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>

namespace pixcake::image_import {
namespace {

constexpr std::uint8_t kPngSignature[] = {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};

bool has_prefix(const std::vector<std::uint8_t>& bytes, const std::uint8_t* prefix, std::size_t size) {
  if (bytes.size() < size) {
    return false;
  }
  for (std::size_t index = 0; index < size; ++index) {
    if (bytes[index] != prefix[index]) {
      return false;
    }
  }
  return true;
}

bool has_partial_prefix(const std::vector<std::uint8_t>& bytes, const std::uint8_t* prefix, std::size_t size) {
  if (bytes.empty() || bytes.size() >= size) {
    return false;
  }
  for (std::size_t index = 0; index < bytes.size(); ++index) {
    if (bytes[index] != prefix[index]) {
      return false;
    }
  }
  return true;
}

std::uint16_t read_be16(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
  return static_cast<std::uint16_t>((static_cast<std::uint16_t>(bytes[offset]) << 8) | bytes[offset + 1]);
}

std::uint32_t read_be32(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
  return (static_cast<std::uint32_t>(bytes[offset]) << 24) |
         (static_cast<std::uint32_t>(bytes[offset + 1]) << 16) |
         (static_cast<std::uint32_t>(bytes[offset + 2]) << 8) | static_cast<std::uint32_t>(bytes[offset + 3]);
}

std::uint16_t read_le16(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
  return static_cast<std::uint16_t>(bytes[offset] | (static_cast<std::uint16_t>(bytes[offset + 1]) << 8));
}

std::uint32_t read_le32(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
  return static_cast<std::uint32_t>(bytes[offset]) | (static_cast<std::uint32_t>(bytes[offset + 1]) << 8) |
         (static_cast<std::uint32_t>(bytes[offset + 2]) << 16) |
         (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
}

bool dimensions_ok(std::uint32_t width, std::uint32_t height) {
  return width != 0 && height != 0 &&
         (static_cast<std::uint64_t>(width) <= std::numeric_limits<std::uint64_t>::max() /
                                                    static_cast<std::uint64_t>(height));
}

ResultCode probe_png(const std::vector<std::uint8_t>& bytes, std::uint32_t& width, std::uint32_t& height) {
  if (!has_prefix(bytes, kPngSignature, sizeof(kPngSignature))) {
    return ResultCode::UnsupportedFormat;
  }
  if (bytes.size() < 33 || read_be32(bytes, 8) != 13 || bytes[12] != 'I' || bytes[13] != 'H' ||
      bytes[14] != 'D' || bytes[15] != 'R') {
    return ResultCode::ImageCorrupt;
  }
  width = read_be32(bytes, 16);
  height = read_be32(bytes, 20);
  return dimensions_ok(width, height) ? ResultCode::Ok : ResultCode::ImageCorrupt;
}

bool jpeg_sof_marker(std::uint8_t marker) {
  return (marker >= 0xc0 && marker <= 0xc3) || (marker >= 0xc5 && marker <= 0xc7) ||
         (marker >= 0xc9 && marker <= 0xcb) || (marker >= 0xcd && marker <= 0xcf);
}

ResultCode probe_jpeg(const std::vector<std::uint8_t>& bytes, std::uint32_t& width, std::uint32_t& height) {
  if (bytes.empty() || bytes[0] != 0xff) {
    return ResultCode::UnsupportedFormat;
  }
  if (bytes.size() < 2) {
    return ResultCode::ImageCorrupt;
  }
  if (bytes[1] != 0xd8) {
    return ResultCode::UnsupportedFormat;
  }
  std::size_t offset = 2;
  while (offset < bytes.size()) {
    if (bytes[offset] != 0xff) {
      return ResultCode::ImageCorrupt;
    }
    while (offset < bytes.size() && bytes[offset] == 0xff) {
      ++offset;
    }
    if (offset >= bytes.size()) {
      return ResultCode::ImageCorrupt;
    }
    const std::uint8_t marker = bytes[offset++];
    if (marker == 0xd9 || marker == 0xda) {
      return ResultCode::ImageCorrupt;
    }
    if (marker >= 0xd0 && marker <= 0xd7) {
      continue;
    }
    if (offset + 2 > bytes.size()) {
      return ResultCode::ImageCorrupt;
    }
    const std::uint16_t segment_length = read_be16(bytes, offset);
    if (segment_length < 2 || static_cast<std::size_t>(segment_length) > bytes.size() - offset) {
      return ResultCode::ImageCorrupt;
    }
    if (jpeg_sof_marker(marker)) {
      if (segment_length < 7) {
        return ResultCode::ImageCorrupt;
      }
      height = read_be16(bytes, offset + 3);
      width = read_be16(bytes, offset + 5);
      return dimensions_ok(width, height) ? ResultCode::Ok : ResultCode::ImageCorrupt;
    }
    offset += segment_length;
  }
  return ResultCode::ImageCorrupt;
}

ResultCode probe_bmp(const std::vector<std::uint8_t>& bytes, std::uint32_t& width, std::uint32_t& height) {
  if (bytes.size() < 2 || bytes[0] != 'B' || bytes[1] != 'M') {
    return ResultCode::UnsupportedFormat;
  }
  if (bytes.size() < 18) {
    return ResultCode::ImageCorrupt;
  }
  const std::uint32_t dib_size = read_le32(bytes, 14);
  if (dib_size < 40 || bytes.size() < 14u + dib_size) {
    return ResultCode::UnsupportedFormat;
  }
  const auto read_signed = [&](std::size_t offset) {
    return static_cast<std::int64_t>(static_cast<std::int32_t>(read_le32(bytes, offset)));
  };
  const std::int64_t signed_width = read_signed(18);
  const std::int64_t signed_height = read_signed(22);
  if (signed_width <= 0 || signed_height == 0 || signed_height == std::numeric_limits<std::int32_t>::min()) {
    return ResultCode::ImageCorrupt;
  }
  const std::int64_t absolute_height = signed_height < 0 ? -signed_height : signed_height;
  if (signed_width > std::numeric_limits<std::uint32_t>::max() ||
      absolute_height > std::numeric_limits<std::uint32_t>::max()) {
    return ResultCode::ImageCorrupt;
  }
  width = static_cast<std::uint32_t>(signed_width);
  height = static_cast<std::uint32_t>(absolute_height);
  return dimensions_ok(width, height) ? ResultCode::Ok : ResultCode::ImageCorrupt;
}

struct TiffReader {
  const std::vector<std::uint8_t>& bytes;
  bool little = true;

  std::uint16_t u16(std::size_t offset) const {
    return little ? read_le16(bytes, offset) : read_be16(bytes, offset);
  }

  std::uint32_t u32(std::size_t offset) const {
    return little ? read_le32(bytes, offset) : read_be32(bytes, offset);
  }
};

ResultCode probe_tiff(const std::vector<std::uint8_t>& bytes, std::uint32_t& width, std::uint32_t& height) {
  if (bytes.size() < 2 || ((bytes[0] != 'I' || bytes[1] != 'I') && (bytes[0] != 'M' || bytes[1] != 'M'))) {
    return ResultCode::UnsupportedFormat;
  }
  if (bytes.size() < 8) {
    return ResultCode::ImageCorrupt;
  }
  TiffReader reader{bytes, bytes[0] == 'I'};
  if (reader.u16(2) != 42) {
    return ResultCode::ImageCorrupt;
  }
  const std::uint32_t ifd_offset = reader.u32(4);
  if (ifd_offset > bytes.size() - 2) {
    return ResultCode::ImageCorrupt;
  }
  const std::uint16_t entry_count = reader.u16(ifd_offset);
  const std::size_t entries_offset = static_cast<std::size_t>(ifd_offset) + 2;
  if (entry_count > (bytes.size() - entries_offset) / 12) {
    return ResultCode::ImageCorrupt;
  }
  bool have_width = false;
  bool have_height = false;
  const auto read_tag_dimension = [&](std::size_t entry_offset, std::uint32_t& value) {
    const std::uint16_t type = reader.u16(entry_offset + 2);
    const std::uint32_t count = reader.u32(entry_offset + 4);
    if (count == 0 || (type != 3 && type != 4)) {
      return false;
    }
    const std::size_t element_size = type == 3 ? 2 : 4;
    if (count > std::numeric_limits<std::size_t>::max() / element_size) {
      return false;
    }
    const std::size_t total = static_cast<std::size_t>(count) * element_size;
    if (total <= 4) {
      value = type == 3 ? reader.u16(entry_offset + 8) : reader.u32(entry_offset + 8);
      return true;
    }
    const std::uint32_t value_offset = reader.u32(entry_offset + 8);
    if (value_offset > bytes.size() - total) {
      return false;
    }
    value = type == 3 ? reader.u16(value_offset) : reader.u32(value_offset);
    return true;
  };
  for (std::uint16_t index = 0; index < entry_count; ++index) {
    const std::size_t entry_offset = entries_offset + static_cast<std::size_t>(index) * 12;
    const std::uint16_t tag = reader.u16(entry_offset);
    if (tag == 256) {
      if (!read_tag_dimension(entry_offset, width)) {
        return ResultCode::ImageCorrupt;
      }
      have_width = true;
    } else if (tag == 257) {
      if (!read_tag_dimension(entry_offset, height)) {
        return ResultCode::ImageCorrupt;
      }
      have_height = true;
    }
  }
  if (!have_width || !have_height || !dimensions_ok(width, height)) {
    return ResultCode::ImageCorrupt;
  }
  return ResultCode::Ok;
}

ResultCode map_io(const IoResult& result) {
  if (result.not_found) {
    return ResultCode::SourceMissing;
  }
  return result.ok ? ResultCode::Ok : result.code;
}

}  // namespace

const char* source_format_name(SourceFormat format) {
  switch (format) {
    case SourceFormat::Jpeg:
      return "JPEG";
    case SourceFormat::Png:
      return "PNG";
    case SourceFormat::Tiff:
      return "TIFF";
    case SourceFormat::Bmp:
      return "BMP";
  }
  return "";
}

ResultCode probe_bytes(const std::vector<std::uint8_t>& bytes, SourceFormat& format, std::uint32_t& width,
                       std::uint32_t& height) {
  if (bytes.size() >= 2 && bytes[0] == 0xff && bytes[1] == 0xd8) {
    format = SourceFormat::Jpeg;
    return probe_jpeg(bytes, width, height);
  }
  if (bytes.size() >= sizeof(kPngSignature) && has_prefix(bytes, kPngSignature, sizeof(kPngSignature))) {
    format = SourceFormat::Png;
    return probe_png(bytes, width, height);
  }
  if (has_partial_prefix(bytes, kPngSignature, sizeof(kPngSignature))) {
    format = SourceFormat::Png;
    return ResultCode::ImageCorrupt;
  }
  if (bytes.size() >= 2 && ((bytes[0] == 'I' && bytes[1] == 'I') || (bytes[0] == 'M' && bytes[1] == 'M'))) {
    format = SourceFormat::Tiff;
    return probe_tiff(bytes, width, height);
  }
  if (bytes.size() >= 2 && bytes[0] == 'B' && bytes[1] == 'M') {
    format = SourceFormat::Bmp;
    return probe_bmp(bytes, width, height);
  }
  return ResultCode::UnsupportedFormat;
}

ResultCode probe_source(FileSystem& files, const std::string& absolute_path, SourceProbe& out) {
  if (absolute_path.empty() || absolute_path.front() != '/') {
    return ResultCode::StateConflict;
  }
  const IoResult probed = files.probe_file(absolute_path);
  const ResultCode probe_code = map_io(probed);
  if (probe_code != ResultCode::Ok) {
    return probe_code;
  }
  std::vector<std::uint8_t> bytes;
  const IoResult read = files.read_file(absolute_path, bytes);
  const ResultCode read_code = map_io(read);
  if (read_code != ResultCode::Ok) {
    return read_code;
  }
  SourceFormat format = SourceFormat::Jpeg;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  const ResultCode header_code = probe_bytes(bytes, format, width, height);
  if (header_code != ResultCode::Ok) {
    return header_code;
  }
  FileIdentity identity;
  const IoResult identified = files.identify(absolute_path, identity);
  const ResultCode identity_code = map_io(identified);
  if (identity_code != ResultCode::Ok) {
    return identity_code;
  }
  if (!identity.valid) {
    return ResultCode::StateConflict;
  }
  out.absolute_path = absolute_path;
  out.format = format;
  out.width = width;
  out.height = height;
  out.identity = identity;
  return ResultCode::Ok;
}

// 唯一能构造 AdmittedSource 的类型（见 pixcake/project/admitted_source.hpp）。
class SourceAdmission {
 public:
  static AdmittedSource issue(const SourceProbe& probe) { return AdmittedSource(probe.absolute_path, probe.identity); }
};

ResultCode admit_source(FileSystem& files, const std::string& absolute_path, std::optional<AdmittedSource>& out,
                        SourceProbe* details) {
  out.reset();
  SourceProbe probe;
  const ResultCode probed = probe_source(files, absolute_path, probe);
  if (probed != ResultCode::Ok) {
    return probed;
  }
  out = SourceAdmission::issue(probe);
  if (details != nullptr) {
    *details = probe;
  }
  return ResultCode::Ok;
}

}  // namespace pixcake::image_import
