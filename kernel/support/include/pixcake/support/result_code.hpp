#pragma once

namespace pixcake {

enum class ResultCode {
  Ok,
  UnsupportedFormat,
  ImageCorrupt,
  PermissionDenied,
  DiskFull,
  SourceMissing,
  DocumentCorrupt,
  CapabilityTooNew,
  Cancelled,
  NetworkFailed,
  InsufficientBalance,
  FastExportBusy,
  StateConflict,
  IoFailed,
};

const char* result_code_label(ResultCode code);

}  // namespace pixcake
