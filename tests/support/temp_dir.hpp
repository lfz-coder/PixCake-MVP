#pragma once

#include <filesystem>
#include <stdexcept>
#include <string>
#include <system_error>
#include <unistd.h>

namespace pixcake::test {

class TempDir {
 public:
  TempDir() {
    std::string pattern = "/tmp/pixcake-project-XXXXXX";
    if (::mkdtemp(pattern.data()) == nullptr) {
      throw std::runtime_error("mkdtemp");
    }
    path_ = std::move(pattern);
  }

  TempDir(const TempDir&) = delete;
  TempDir& operator=(const TempDir&) = delete;

  ~TempDir() {
    std::error_code error;
    std::filesystem::remove_all(path_, error);
  }

  const std::string& path() const { return path_; }

 private:
  std::string path_;
};

}  // namespace pixcake::test
