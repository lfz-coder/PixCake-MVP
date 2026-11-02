#pragma once

#include "pixcake/persist/file_system.hpp"

#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace pixcake::test {

class SimulatedCrash : public std::runtime_error {
 public:
  SimulatedCrash() : std::runtime_error("simulated crash") {}
};

class FakeFileSystem : public FileSystem {
 public:
  std::map<std::string, std::vector<std::uint8_t>> files;
  std::vector<std::string> writes;
  std::vector<std::string> events;
  ResultCode write_error = ResultCode::Ok;
  ResultCode rename_error = ResultCode::Ok;
  ResultCode sync_file_error = ResultCode::Ok;
  ResultCode sync_dir_error = ResultCode::Ok;
  int crash_after = 0;

  void arm_crash_after(int count) {
    mutations_ = 0;
    crash_after = count;
  }

  bool exists(const std::string& path) const override { return files.find(path) != files.end(); }

  IoResult read_file(const std::string& path, std::vector<std::uint8_t>& out) override {
    const auto found = files.find(path);
    if (found == files.end()) {
      return io_not_found();
    }
    out = found->second;
    return io_ok();
  }

  IoResult write_new(const std::string& path, const std::vector<std::uint8_t>& data) override {
    writes.push_back(path);
    if (write_error != ResultCode::Ok) {
      return io_error(write_error);
    }
    if (files.find(path) != files.end()) {
      return io_error(ResultCode::IoFailed);
    }
    files[path] = data;
    note("write:" + path);
    return io_ok();
  }

  IoResult remove_file(const std::string& path) override {
    const auto found = files.find(path);
    if (found == files.end()) {
      return io_not_found();
    }
    files.erase(found);
    note("remove:" + path);
    return io_ok();
  }

  IoResult rename_file(const std::string& from, const std::string& to) override {
    if (rename_error != ResultCode::Ok) {
      return io_error(rename_error);
    }
    const auto found = files.find(from);
    if (found == files.end()) {
      return io_not_found();
    }
    auto bytes = std::move(found->second);
    files.erase(found);
    files[to] = std::move(bytes);
    note("rename:" + from + "->" + to);
    return io_ok();
  }

  IoResult sync_file(const std::string& path) override {
    if (sync_file_error != ResultCode::Ok) {
      return io_error(sync_file_error);
    }
    if (files.find(path) == files.end()) {
      return io_not_found();
    }
    note("sync_file:" + path);
    return io_ok();
  }

  IoResult sync_directory(const std::string& path) override {
    if (sync_dir_error != ResultCode::Ok) {
      return io_error(sync_dir_error);
    }
    note("sync_dir:" + path);
    return io_ok();
  }

  IoResult create_directory(const std::string&) override { return io_ok(); }

 private:
  int mutations_ = 0;

  void note(const std::string& event) {
    events.push_back(event);
    if (crash_after > 0 && ++mutations_ == crash_after) {
      throw SimulatedCrash();
    }
  }
};

}  // namespace pixcake::test
