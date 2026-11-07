#pragma once

#include "pixcake/persist/file_system.hpp"

#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include <functional>

namespace pixcake::test {

class SimulatedCrash : public std::runtime_error {
 public:
  SimulatedCrash() : std::runtime_error("simulated crash") {}
};

class FakeFileSystem : public FileSystem {
 public:
  std::map<std::string, std::vector<std::uint8_t>> files;
  std::map<std::string, ResultCode> probe_errors;
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

  IoResult probe_file(const std::string& path) const override {
    const auto special = probe_errors.find(path);
    if (special != probe_errors.end()) {
      return io_error(special->second);
    }
    if (files.find(path) == files.end()) {
      return io_not_found();
    }
    return io_ok();
  }

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

  IoResult publish_new(const std::string& from, const std::string& to) override {
    if (files.find(to) != files.end()) {
      return io_error(ResultCode::StateConflict);
    }
    // A link cannot cross devices (EXDEV).
    if (device_for(from) != device_for(to)) return io_error(ResultCode::IoFailed);
    return rename_file(from, to);
  }

  // Directories on another device: path prefix -> device.  Everything else is device 1.
  std::map<std::string, std::uint64_t> devices;

  IoResult device_of(const std::string& directory, std::uint64_t& out) const override {
    out = device_for(directory + "/");
    return io_ok();
  }

  std::uint64_t device_for(const std::string& path) const {
    std::uint64_t device = 1;
    std::size_t longest = 0;
    for (const auto& [prefix, id] : devices) {
      if (path.rfind(prefix + "/", 0) == 0 && prefix.size() >= longest) {
        device = id;
        longest = prefix.size();
      }
    }
    return device;
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

  IoResult identify(const std::string& path, FileIdentity& out) const override {
    if (files.find(path) == files.end()) return io_not_found();
    out.valid = true;
    out.device = 1;
    const auto& bytes = files.at(path);
    std::string key(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    out.inode = static_cast<std::uint64_t>(std::hash<std::string>{}(key));
    out.size = files.at(path).size();
    return io_ok();
  }

  IoResult remove_if_identity(const std::string& path, const FileIdentity& expected) override {
    FileIdentity actual;
    const IoResult identified = identify(path, actual);
    if (!identified.ok) return identified;
    if (!expected.valid || actual.device != expected.device || actual.inode != expected.inode) {
      return io_error(ResultCode::StateConflict);
    }
    return remove_file(path);
  }

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
