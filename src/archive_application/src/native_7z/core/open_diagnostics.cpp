// src/archive_application/src/native_7z/core/open_diagnostics.cpp
// Role: Canonical collection, formatting, and result mapping for 7-Zip open diagnostics.

#include <array>
#include <iomanip>
#include <sstream>
#include <string_view>

#include "core/internal.h"
#include "Windows/ErrorMsg.h"

namespace z7::app {
    namespace {

        constexpr std::array<char const*, 11> kOpenErrorFlagMessages = {
            "Is not archive",
            "Headers Error",
            "Headers Error : Wrong password?",
            "Unavailable start of archive",
            "Unconfirmed start of archive",
            "Unexpected end of data",
            "There are some data after the end of the payload data",
            "Unsupported compression method",
            "Unsupported feature",
            "Data error",
            "CRC failed",
        };

        void append_line(std::string& destination, std::string_view line) {
            if (line.empty()) {
                return;
            }
            if (!destination.empty() && destination.back() != '\n') {
                destination.push_back('\n');
            }
            destination.append(line);
            destination.push_back('\n');
        }

        bool contains_line(std::string const& destination, std::string_view line) {
            size_t offset = 0;
            while (offset < destination.size()) {
                size_t const end = destination.find('\n', offset);
                size_t const length = (end == std::string::npos ? destination.size() : end) - offset;
                if (line == std::string_view(destination).substr(offset, length)) {
                    return true;
                }
                if (end == std::string::npos) {
                    break;
                }
                offset = end + 1;
            }
            return false;
        }

        void append_unique_lines(std::string& destination, std::string_view source) {
            size_t offset = 0;
            while (offset < source.size()) {
                size_t const end = source.find('\n', offset);
                std::string_view const line = source.substr(
                    offset, (end == std::string_view::npos ? source.size() : end) - offset);
                if (!line.empty() && !contains_line(destination, line)) {
                    append_line(destination, line);
                }
                if (end == std::string_view::npos) {
                    break;
                }
                offset = end + 1;
            }
        }

        void trim_trailing_newline(std::string& value) {
            while (!value.empty() && value.back() == '\n') {
                value.pop_back();
            }
        }

        std::string format_open_error_flags(UInt32 flags) {
            std::string message;
            for (std::size_t bit = 0; bit < kOpenErrorFlagMessages.size(); ++bit) {
                UInt32 const flag = static_cast<UInt32>(1U << bit);
                if ((flags & flag) == 0) {
                    continue;
                }
                append_line(message, kOpenErrorFlagMessages[bit]);
                flags &= ~flag;
            }
            if (flags != 0) {
                std::ostringstream hex;
                hex << "0x" << std::uppercase << std::hex << flags;
                append_line(message, hex.str());
            }
            trim_trailing_newline(message);
            return message;
        }

        std::string braced_format_name(CCodecs const* codecs, int format_index) {
            if (codecs == nullptr || format_index < 0
                || static_cast<unsigned>(format_index) >= codecs->Formats.Size()) {
                return "[]";
            }
            wchar_t const* name = codecs->GetFormatNamePtr(static_cast<unsigned>(format_index));
            return "[" + update_wide_name_to_utf8(name) + "]";
        }

        void append_context(std::string& destination, std::string const& context) {
            if (!context.empty()) {
                append_unique_lines(destination, context);
            }
        }

        void collect_error_info(OpenArchiveDiagnostics& diagnostics,
                                CArcErrorInfo const& error_info,
                                std::string const& context) {
            UInt32 const error_flags = error_info.GetErrorFlags();
            bool const has_error_message = !error_info.ErrorMessage.IsEmpty();
            if (error_flags != 0 || has_error_message) {
                ++diagnostics.error_count;
                append_context(diagnostics.error_message, context);
                if (error_flags != 0) {
                    append_unique_lines(diagnostics.error_message, format_open_error_flags(error_flags));
                }
                if (has_error_message) {
                    append_unique_lines(diagnostics.error_message, ustring_to_utf8(error_info.ErrorMessage));
                }
            }

            UInt32 const warning_flags = error_info.GetWarningFlags();
            if (warning_flags != 0) {
                ++diagnostics.warning_count;
                append_context(diagnostics.warning_message, context);
                append_unique_lines(diagnostics.warning_message, format_open_error_flags(warning_flags));
            }
            if (!error_info.WarningMessage.IsEmpty()) {
                ++diagnostics.warning_count;
                append_context(diagnostics.warning_message, context);
                append_unique_lines(diagnostics.warning_message, ustring_to_utf8(error_info.WarningMessage));
            }
        }

        void append_operation_error_info(std::string& destination, CArcErrorInfo const& error_info) {
            UInt32 const error_flags = error_info.GetErrorFlags();
            UInt32 const warning_flags = error_info.GetWarningFlags();
            if (error_flags != 0) {
                append_line(destination, format_open_error_flags(error_flags));
            }
            if (!error_info.ErrorMessage.IsEmpty()) {
                append_line(destination, ustring_to_utf8(error_info.ErrorMessage));
            }
            if (warning_flags != 0) {
                append_line(destination, "Warnings:");
                append_line(destination, format_open_error_flags(warning_flags));
            }
            if (!error_info.WarningMessage.IsEmpty()) {
                append_line(destination, "Warning: " + ustring_to_utf8(error_info.WarningMessage));
            }
        }

        std::string prefixed_error_message(OpenArchiveDiagnostics const& diagnostics) {
            std::string message = "Archive open error";
            if (!diagnostics.error_message.empty()) {
                message += ":\n";
                message += diagnostics.error_message;
            }
            return message;
        }

        void append_existing_failure(std::string& message, OperationResult const& result) {
            if (result.ok || result.error.domain == ArchiveErrorDomain::kNone || result.error.message.empty()) {
                return;
            }
            append_unique_lines(message, result.error.message);
            trim_trailing_newline(message);
        }

    } // namespace

    OpenArchiveDiagnostics collect_open_archive_diagnostics(CCodecs const* codecs,
                                                             CArchiveLink const& archive_link,
                                                             wchar_t const* name,
                                                             HRESULT result) {
        OpenArchiveDiagnostics diagnostics;
        std::string const archive_name = update_wide_name_to_utf8(name);

        for (unsigned level = 0; level < archive_link.Arcs.Size(); ++level) {
            CArc const& arc = archive_link.Arcs[level];
            CArcErrorInfo const& error_info = arc.ErrorInfo;
            std::string const context = level == 0 ? std::string() : ustring_to_utf8(arc.Path);
            collect_error_info(diagnostics, error_info, context);

            if (!error_info.IsThereErrorOrWarning() && error_info.ErrorFormatIndex < 0) {
                continue;
            }
            if (diagnostics.operation_message.empty()) {
                append_line(diagnostics.operation_message, archive_name);
            }
            if (level != 0) {
                append_line(diagnostics.operation_message, ustring_to_utf8(arc.Path));
            }
            append_operation_error_info(diagnostics.operation_message, error_info);

            if (error_info.ErrorFormatIndex >= 0) {
                ++diagnostics.warning_count;
                append_line(diagnostics.operation_message, "Warning");
                std::string warning;
                if (arc.FormatIndex == error_info.ErrorFormatIndex) {
                    warning = "The archive is open with offset";
                } else {
                    warning = "Cannot open the file as "
                            + braced_format_name(codecs, error_info.ErrorFormatIndex) + " archive\nThe file is open as "
                            + braced_format_name(codecs, arc.FormatIndex) + " archive";
                }
                append_line(diagnostics.operation_message, warning);
                append_context(diagnostics.warning_message, context);
                append_unique_lines(diagnostics.warning_message, warning);
            }
        }

        CArcErrorInfo const& non_open_info = archive_link.NonOpen_ErrorInfo;
        std::string const non_open_context = archive_link.Arcs.IsEmpty()
                                                   ? std::string()
                                                   : ustring_to_utf8(archive_link.NonOpen_ArcPath);
        collect_error_info(diagnostics, non_open_info, non_open_context);

        if (non_open_info.ErrorFormatIndex >= 0) {
            ++diagnostics.warning_count;
            append_context(diagnostics.warning_message, non_open_context);
            append_unique_lines(
                diagnostics.warning_message,
                "Cannot open the file as " + braced_format_name(codecs, non_open_info.ErrorFormatIndex) + " archive");
        }

        if (non_open_info.ErrorFormatIndex >= 0 || result != S_OK) {
            append_line(diagnostics.operation_message, archive_name);
            if (!archive_link.Arcs.IsEmpty()) {
                append_line(diagnostics.operation_message, archive_link.NonOpen_ArcPath.IsEmpty()
                                                               ? std::string()
                                                               : ustring_to_utf8(archive_link.NonOpen_ArcPath));
            }

            if (non_open_info.ErrorFormatIndex >= 0 || result == S_FALSE) {
                if (archive_link.PasswordWasAsked) {
                    append_line(diagnostics.operation_message, "Cannot open encrypted archive. Wrong password?");
                } else if (non_open_info.ErrorFormatIndex >= 0) {
                    append_line(diagnostics.operation_message,
                                "Cannot open the file as "
                                    + braced_format_name(codecs, non_open_info.ErrorFormatIndex) + " archive");
                } else {
                    append_line(diagnostics.operation_message, "Cannot open file as archive");
                }
            } else {
                append_line(diagnostics.operation_message,
                            ustring_to_utf8(NWindows::NError::MyFormatMessage(result)));
            }
            append_operation_error_info(diagnostics.operation_message, non_open_info);
        }

        trim_trailing_newline(diagnostics.error_message);
        trim_trailing_newline(diagnostics.warning_message);
        trim_trailing_newline(diagnostics.operation_message);
        return diagnostics;
    }

    void append_open_archive_diagnostics(OpenArchiveDiagnostics& destination,
                                         OpenArchiveDiagnostics const& source) {
        destination.error_count += source.error_count;
        destination.warning_count += source.warning_count;
        append_unique_lines(destination.error_message, source.error_message);
        append_unique_lines(destination.warning_message, source.warning_message);
        append_unique_lines(destination.operation_message, source.operation_message);
        trim_trailing_newline(destination.error_message);
        trim_trailing_newline(destination.warning_message);
        trim_trailing_newline(destination.operation_message);
    }

    void apply_open_archive_diagnostics(OperationResult& result,
                                        OpenArchiveDiagnostics const& diagnostics) {
        if (!diagnostics.has_errors()) {
            return;
        }

        std::string message = prefixed_error_message(diagnostics);
        append_existing_failure(message, result);
        ArchiveErrorDomain const existing_domain = result.error.domain;
        if (existing_domain == ArchiveErrorDomain::kCanceled
            || existing_domain == ArchiveErrorDomain::kPassword
            || existing_domain == ArchiveErrorDomain::kBudgetExceeded) {
            result.error.message = std::move(message);
            result.summary = describe_archive_error(result.error);
            return;
        }

        result.ok = false;
        result.native_exit_code = 2;
        result.native_execution.native_exit_code = 2;
        result.native_execution.termination_reason = NativeTerminationReason::kCompleted;
        result.error = make_archive_error(ArchiveErrorDomain::kIo, std::move(message), 2);
        result.summary = describe_archive_error(result.error);
    }

    ReadOperationOpenDiagnosticState publish_read_operation_open_diagnostics(
        ArchiveBackendHooks const& hooks,
        OpenArchiveDiagnostics const* diagnostics,
        bool already_published) {
        ReadOperationOpenDiagnosticState state;
        if (diagnostics == nullptr || diagnostics->operation_message.empty()) {
            return state;
        }

        state.progress_error_count = diagnostics->error_count;
        state.archive_context_reported = true;
        if (!already_published) {
            emit_log_event(hooks,
                           OperationStage::kRunning,
                           OutputChannel::kStdErr,
                           diagnostics->operation_message,
                           OperationMessageKind::kArchiveOpenDiagnostic);
        }
        return state;
    }

} // namespace z7::app
