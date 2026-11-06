#pragma once

#include "pixcake/persist/file_system.hpp"

#include <string>

namespace pixcake {

namespace image_import {
class SourceAdmission;
}

// 已通过内容准入（格式、尺寸、文件身份）的原图。构造函数私有，只有 kernel/import 的
// SourceAdmission 能签发；项目库只接受这种凭证，不接受裸路径。
class AdmittedSource {
 public:
  const std::string& absolute_path() const { return absolute_path_; }
  const FileIdentity& identity() const { return identity_; }
  // 准入时按文件内容识别的格式名（JPEG、PNG、TIFF、BMP），不看扩展名。
  const std::string& format() const { return format_; }

 private:
  friend class image_import::SourceAdmission;
  AdmittedSource(std::string absolute_path, FileIdentity identity, std::string format)
      : absolute_path_(std::move(absolute_path)), identity_(identity), format_(std::move(format)) {}

  std::string absolute_path_;
  FileIdentity identity_;
  std::string format_;
};

}  // namespace pixcake
