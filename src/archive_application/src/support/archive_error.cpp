#include "archive_error.h"

namespace z7::app {

ArchiveError make_archive_error(ArchiveErrorDomain domain,
                                std::string message,
                                int native_exit_code) {
  ArchiveError error;
  error.domain = domain;
  error.native_exit_code = native_exit_code;
  error.message = std::move(message);
  return error;
}

ArchiveError map_native_exit_code(int native_exit_code,
                                  NativeTerminationReason termination_reason,
                                  const std::string&) {
  if (native_exit_code == 0) {
    return make_archive_error(ArchiveErrorDomain::kNone, {}, native_exit_code);
  }

  if (termination_reason == NativeTerminationReason::kCanceled ||
      native_exit_code == 255) {
    return make_archive_error(ArchiveErrorDomain::kCanceled,
                              "Operation canceled",
                              native_exit_code);
  }

  if (native_exit_code == 1) {
    return make_archive_error(ArchiveErrorDomain::kPartialSuccess,
                              "Operation completed with warnings",
                              native_exit_code);
  }

  if (native_exit_code == 7) {
    return make_archive_error(ArchiveErrorDomain::kInvalidArguments,
                              "Invalid request arguments",
                              native_exit_code);
  }

  if (native_exit_code == 8 ||
      termination_reason == NativeTerminationReason::kAborted) {
    return make_archive_error(ArchiveErrorDomain::kBackendUnavailable,
                              "Requested backend is unavailable",
                              native_exit_code);
  }

  return make_archive_error(ArchiveErrorDomain::kUnknown,
                            "Unknown archive backend error",
                            native_exit_code);
}

std::string describe_archive_error(const ArchiveError& error) {
  if (!error.message.empty()) {
    return error.message;
  }

  switch (error.domain) {
    case ArchiveErrorDomain::kNone:
      return "Success";
    case ArchiveErrorDomain::kCanceled:
      return "Operation canceled";
    case ArchiveErrorDomain::kPassword:
      return "Password required or incorrect";
    case ArchiveErrorDomain::kUnsupportedFormat:
      return "Archive format is unsupported";
    case ArchiveErrorDomain::kIo:
      return "I/O error";
    case ArchiveErrorDomain::kPartialSuccess:
      return "Operation completed with warnings";
    case ArchiveErrorDomain::kInvalidArguments:
      return "Invalid request arguments";
    case ArchiveErrorDomain::kBackendUnavailable:
      return "Requested backend is unavailable";
    case ArchiveErrorDomain::kBudgetExceeded:
      return "Extract budget exceeded";
    case ArchiveErrorDomain::kUnknown:
      return "Unknown archive backend error";
  }

  return "Unknown archive backend error";
}

bool is_operation_canceled(const ArchiveError& error) {
  return error.domain == ArchiveErrorDomain::kCanceled;
}

}  // namespace z7::app
