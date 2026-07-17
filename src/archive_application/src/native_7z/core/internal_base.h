// src/archive_application/src/native_7z/core/internal_base.h
// Role: Core declarations and open-archive helpers for native backend internals.

#pragma once

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <vector>

#include "archive_error.h"
#include "archive_session.h"
#include "filename_code_page.h"
#include "backend/benchmark_typed_parser.h"
#include "common/ascii_text.h"
#include "ports/archive_backend_port.h"
#include "third_party_adapter/third_party_adapter.h"

#ifdef Z7_EXTERNAL_CODECS
int LoadGlobalCodecs();
#endif

namespace z7::app {

    namespace fs = std::filesystem;
    inline constexpr size_t kHashReadChunkSize = 1 << 20;
    inline constexpr uint64_t kHashProgressStepBytes = 1 << 21;
    inline constexpr uint32_t kDefaultBenchIterations = 10;

    struct FilesystemObjectIdentity {
        uint64_t volume = 0;
        uint64_t object = 0;
        bool defined = false;
    };

    enum class OpenResultMessagePolicy {
        kSilentBrowse,
        kOperationMessages,
        kReadOperationMessages
    };

    class NativeUpdateOperationCallback;
    class FilesystemTransaction;
    template <typename TRequest, typename TResult>
    class OperationRunner;

    template <typename TResult>
    TResult from_base_result(OperationResult base) {
        TResult out;
        static_cast<OperationResult&>(out) = std::move(base);
        return out;
    }

    bool is_auto_value(std::string const& value);
    bool parse_uint64_decimal(std::string const& value, uint64_t& out);
    uint64_t parse_size_to_bytes_or_default(std::string const& raw, uint64_t default_bytes);
    uint32_t parse_thread_count_or_default(std::string const& raw, uint32_t default_threads);
    bool parse_volume_sizes_spec(std::string const& input, std::vector<uint64_t>& values);
    uint64_t get_number_of_volumes(uint64_t size, std::vector<uint64_t> const& vol_sizes);
    size_t volume_number_digits(uint64_t num_volumes);
    std::string volume_sequence_name(uint64_t index, size_t digits);

    struct VolumeSequenceState {
        std::string unchanged_part;
        std::string changed_part{"000"};

        std::string next_name();
        bool parse_name(std::string const& name);
    };

    std::string astring_to_std(AString const& value);
    std::string ustring_to_utf8(UString const& value);
    UString utf8_to_ustring(std::string const& value);

    OperationResult invalid_request(std::string const& summary);
    OperationResult unsupported_request(std::string const& summary);

    void emit_log_event(ArchiveBackendHooks const& hooks,
                        OperationStage stage,
                        OutputChannel channel,
                        std::string const& message,
                        OperationMessageKind message_kind = OperationMessageKind::kGeneral,
                        std::optional<BenchmarkTypedSnapshot> const& benchmark_snapshot = std::nullopt,
                        std::optional<BenchmarkTypedSummary> const& benchmark_summary = std::nullopt);
    void emit_progress_event(ArchiveBackendHooks const& hooks,
                             OperationStage stage,
                             int percent,
                             bool totals_known,
                             uint64_t total_bytes,
                             uint64_t completed_bytes,
                             uint64_t total_files,
                             uint64_t completed_files,
                             uint64_t error_count,
                             std::string const& current_path,
                             std::string const& message = {},
                             std::optional<ProgressRatioInfo> const& ratio_info = std::nullopt,
                             std::optional<BenchmarkTypedSnapshot> const& benchmark_snapshot = std::nullopt,
                             std::optional<BenchmarkTypedSummary> const& benchmark_summary = std::nullopt);

    bool ensure_parent_dir(fs::path const& path, std::error_code& ec);
    bool create_private_directory(fs::path const& path, std::error_code& ec);
    bool remove_path_any(fs::path const& path, std::error_code& ec);
    FilesystemObjectIdentity capture_filesystem_object_identity_no_follow(fs::path const& path, std::error_code& ec);
    bool filesystem_object_matches_identity_no_follow(fs::path const& path,
                                                      FilesystemObjectIdentity const& identity,
                                                      std::error_code& ec);
    bool copy_path_any(fs::path const& src, fs::path const& dst, bool overwrite, std::error_code& ec);
    bool copy_regular_file_with_metadata(fs::path const& src, fs::path const& dst, std::error_code& ec);
    bool copy_file_metadata(fs::path const& src, fs::path const& dst, std::error_code& ec);
    bool move_path_to_recycle_bin(fs::path const& path, std::error_code& ec);
    ArchiveError map_hresult_to_archive_error(int hr);
    int load_codecs_shared(CCodecs& codecs);
    int prepare_open_types_for_archive(std::string const& archive_type_hint,
                                       CCodecs& codecs,
                                       CObjectVector<COpenType>& types);

    struct OpenArchiveDiagnostics {
        uint64_t error_count = 0;
        uint64_t warning_count = 0;
        std::string error_message;
        std::string warning_message;
        std::string operation_message;

        bool has_errors() const { return error_count != 0; }
        bool has_warnings() const { return warning_count != 0; }
    };

    struct ReadOperationOpenDiagnosticState {
        uint64_t progress_error_count = 0;
        bool archive_context_reported = false;
    };

    OpenArchiveDiagnostics collect_open_archive_diagnostics(CCodecs const* codecs,
                                                             CArchiveLink const& archive_link,
                                                             wchar_t const* name,
                                                             HRESULT result);
    void append_open_archive_diagnostics(OpenArchiveDiagnostics& destination,
                                         OpenArchiveDiagnostics const& source);
    void apply_open_archive_diagnostics(OperationResult& result,
                                        OpenArchiveDiagnostics const& diagnostics);
    ReadOperationOpenDiagnosticState publish_read_operation_open_diagnostics(
        ArchiveBackendHooks const& hooks,
        OpenArchiveDiagnostics const* diagnostics,
        bool already_published);

    template <typename TResult>
    TResult make_operation_failure_from_open_diagnostics(OpenArchiveDiagnostics const& diagnostics) {
        TResult result;
        apply_open_archive_diagnostics(result, diagnostics);
        return result;
    }

    int open_archive_shared(std::string const& archive_path,
                            std::string const& archive_type_hint,
                            ArchiveBackendHooks const& hooks,
                            std::atomic<bool>* cancel_requested,
                            std::function<bool()> wait_while_paused,
                            OpenResultMessagePolicy message_policy,
                            bool allow_password_prompt,
                            std::string const& initial_password,
                            bool codecs_already_loaded,
                            CCodecs& codecs,
                            CObjectVector<COpenType>& types,
                            CIntVector& excluded_formats,
                            CArchiveLink& archive_link,
                            CArc const*& arc,
                            bool* out_password_requested = nullptr,
                            bool* out_wrong_password = nullptr,
                            std::string* out_password = nullptr,
                            OpenArchiveDiagnostics* out_diagnostics = nullptr,
                            FilenameCodePage filename_code_page = std::nullopt);

    // Open an archive whose bytes are already available as an IInStream (seekable)
    // rather than a filesystem path. `display_path` is used purely for extension /
    // format-hint resolution, not for I/O.
    int open_archive_shared_from_stream(IInStream* in_stream,
                                        std::string const& display_path,
                                        std::string const& archive_type_hint,
                                        ArchiveBackendHooks const& hooks,
                                        std::atomic<bool>* cancel_requested,
                                        std::function<bool()> wait_while_paused,
                                        OpenResultMessagePolicy message_policy,
                                        bool allow_password_prompt,
                                        std::string const& initial_password,
                                        bool codecs_already_loaded,
                                        CCodecs& codecs,
                                        CObjectVector<COpenType>& types,
                                        CIntVector& excluded_formats,
                                        CArchiveLink& archive_link,
                                        CArc const*& arc,
                                        bool* out_password_requested = nullptr,
                                        bool* out_wrong_password = nullptr,
                                        std::string* out_password = nullptr,
                                        OpenArchiveDiagnostics* out_diagnostics = nullptr,
                                        FilenameCodePage filename_code_page = std::nullopt);

    struct OpenArchiveReadState {
        CCodecs codecs;
        CObjectVector<COpenType> types;
        CIntVector excluded_formats;
        CArchiveLink archive_link;
        CArc const* arc = nullptr;
        OpenArchiveDiagnostics open_diagnostics;
    };

    template <typename TResult>
    TResult make_operation_failure_from_hresult(int hr);

    template <typename TResult, typename Handler>
    TResult run_with_open_archive_read(std::string const& archive_path,
                                       std::string const& archive_type_hint,
                                       ArchiveBackendHooks const& hooks,
                                       std::atomic<bool>* cancel_requested,
                                       std::function<bool()> wait_while_paused,
                                       OpenResultMessagePolicy message_policy,
                                       bool allow_password_prompt,
                                       std::string const& initial_password,
                                       CCodecs* preloaded_codecs,
                                       Handler&& handler) {
        OpenArchiveReadState open_state;
        CCodecs& codecs = preloaded_codecs != nullptr ? *preloaded_codecs : open_state.codecs;
        bool password_requested = false;
        bool wrong_password = false;
        const HRESULT open_res = open_archive_shared(archive_path,
                                                     archive_type_hint,
                                                     hooks,
                                                     cancel_requested,
                                                     std::move(wait_while_paused),
                                                     message_policy,
                                                     allow_password_prompt,
                                                     initial_password,
                                                     preloaded_codecs != nullptr,
                                                     codecs,
                                                     open_state.types,
                                                     open_state.excluded_formats,
                                                     open_state.archive_link,
                                                     open_state.arc,
                                                     &password_requested,
                                                     &wrong_password,
                                                     nullptr,
                                                     &open_state.open_diagnostics);
        if (open_res != S_OK) {
            TResult result;
            if (password_requested || wrong_password) {
                result = make_operation_failure<TResult>(
                    ArchiveErrorDomain::kPassword, "Password required or incorrect", 2);
            } else {
                result = make_operation_failure_from_hresult<TResult>(open_res);
            }
            apply_open_archive_diagnostics(result, open_state.open_diagnostics);
            return result;
        }

        UInt32 num_items = 0;
        const HRESULT num_res = open_state.arc->Archive->GetNumberOfItems(&num_items);
        if (num_res != S_OK) {
            TResult result = make_operation_failure_from_hresult<TResult>(num_res);
            apply_open_archive_diagnostics(result, open_state.open_diagnostics);
            return result;
        }

        TResult result = handler(open_state, num_items);
        apply_open_archive_diagnostics(result, open_state.open_diagnostics);
        return result;
    }

    template <typename TResult, typename Handler>
    TResult run_with_open_archive_read(std::string const& archive_path,
                                       std::string const& archive_type_hint,
                                       ArchiveBackendHooks const& hooks,
                                       std::atomic<bool>* cancel_requested,
                                       std::function<bool()> wait_while_paused,
                                       OpenResultMessagePolicy message_policy,
                                       bool allow_password_prompt,
                                       std::string const& initial_password,
                                       Handler&& handler) {
        return run_with_open_archive_read<TResult>(archive_path,
                                                   archive_type_hint,
                                                   hooks,
                                                   cancel_requested,
                                                   std::move(wait_while_paused),
                                                   message_policy,
                                                   allow_password_prompt,
                                                   initial_password,
                                                   nullptr,
                                                   std::forward<Handler>(handler));
    }

    template <typename Handler>
    int run_with_open_archive_read_hresult(std::string const& archive_path,
                                           std::string const& archive_type_hint,
                                           ArchiveBackendHooks const& hooks,
                                           std::atomic<bool>* cancel_requested,
                                           std::function<bool()> wait_while_paused,
                                           OpenResultMessagePolicy message_policy,
                                           bool allow_password_prompt,
                                           std::string const& initial_password,
                                           CCodecs* preloaded_codecs,
                                           OpenArchiveDiagnostics* out_diagnostics,
                                           Handler&& handler) {
        OpenArchiveReadState open_state;
        CCodecs& codecs = preloaded_codecs != nullptr ? *preloaded_codecs : open_state.codecs;
        const HRESULT open_res = open_archive_shared(archive_path,
                                                     archive_type_hint,
                                                     hooks,
                                                     cancel_requested,
                                                     std::move(wait_while_paused),
                                                     message_policy,
                                                     allow_password_prompt,
                                                     initial_password,
                                                     preloaded_codecs != nullptr,
                                                     codecs,
                                                     open_state.types,
                                                     open_state.excluded_formats,
                                                     open_state.archive_link,
                                                     open_state.arc,
                                                     nullptr,
                                                     nullptr,
                                                     nullptr,
                                                     &open_state.open_diagnostics);
        if (open_res != S_OK) {
            if (out_diagnostics != nullptr) {
                *out_diagnostics = std::move(open_state.open_diagnostics);
            }
            return open_res;
        }

        UInt32 num_items = 0;
        const HRESULT num_res = open_state.arc->Archive->GetNumberOfItems(&num_items);
        if (num_res != S_OK) {
            if (out_diagnostics != nullptr) {
                *out_diagnostics = std::move(open_state.open_diagnostics);
            }
            return num_res;
        }

        const HRESULT result = handler(open_state, num_items);
        if (out_diagnostics != nullptr) {
            *out_diagnostics = std::move(open_state.open_diagnostics);
        }
        return result;
    }

    template <typename Handler>
    int run_with_open_archive_read_hresult(std::string const& archive_path,
                                           std::string const& archive_type_hint,
                                           ArchiveBackendHooks const& hooks,
                                           std::atomic<bool>* cancel_requested,
                                           std::function<bool()> wait_while_paused,
                                           OpenResultMessagePolicy message_policy,
                                           bool allow_password_prompt,
                                           std::string const& initial_password,
                                           Handler&& handler) {
        return run_with_open_archive_read_hresult(archive_path,
                                                  archive_type_hint,
                                                  hooks,
                                                  cancel_requested,
                                                  std::move(wait_while_paused),
                                                  message_policy,
                                                  allow_password_prompt,
                                                  initial_password,
                                                  nullptr,
                                                  nullptr,
                                                  std::forward<Handler>(handler));
    }

} // namespace z7::app
