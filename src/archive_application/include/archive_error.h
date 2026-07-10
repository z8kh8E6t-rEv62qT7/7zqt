#pragma once

#include <string>

namespace z7::app {

    enum class ArchiveErrorDomain {
        kNone,
        kCanceled,
        kPassword,
        kUnsupportedFormat,
        kIo,
        kPartialSuccess,
        kInvalidArguments,
        kBackendUnavailable,
        kBudgetExceeded,
        kUnknown
    };

    enum class NativeTerminationReason {
        kCompleted,
        kCanceled,
        kAborted
    };

    struct ArchiveError {
        ArchiveErrorDomain domain = ArchiveErrorDomain::kNone;
        int native_exit_code = 0;
        std::string message;
    };

    ArchiveError make_archive_error(ArchiveErrorDomain domain, std::string message = {}, int native_exit_code = 0);

    ArchiveError map_native_exit_code(int native_exit_code,
                                      NativeTerminationReason termination_reason,
                                      std::string const& diagnostic_text = {});

    std::string describe_archive_error(ArchiveError const& error);

    bool is_operation_canceled(ArchiveError const& error);

} // namespace z7::app
