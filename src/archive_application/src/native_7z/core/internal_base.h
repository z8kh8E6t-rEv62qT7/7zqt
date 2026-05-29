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

#include "common/ascii_text.h"
#include "third_party_adapter/third_party_adapter.h"
#include "ports/archive_backend_port.h"
#include "backend/benchmark_typed_parser.h"
#include "archive_session.h"
#include "archive_error.h"

#ifdef Z7_EXTERNAL_CODECS
int LoadGlobalCodecs();
#endif

namespace z7::app {

namespace fs = std::filesystem;
inline constexpr size_t kHashReadChunkSize = 1 << 20;
inline constexpr uint64_t kHashProgressStepBytes = 1 << 21;
inline constexpr uint32_t kDefaultBenchIterations = 10;

class NativeUpdateOperationCallback;
template <typename TRequest, typename TResult>
class OperationRunner;

template <typename TResult>
TResult from_base_result(OperationResult base) {
  TResult out;
  static_cast<OperationResult&>(out) = std::move(base);
  return out;
}

bool is_auto_value(const std::string& value);
bool parse_uint64_decimal(const std::string& value, uint64_t& out);
uint64_t parse_size_to_bytes_or_default(const std::string& raw,
                                        uint64_t default_bytes);
uint32_t parse_thread_count_or_default(const std::string& raw,
                                       uint32_t default_threads);
bool parse_volume_sizes_spec(const std::string& input,
                             std::vector<uint64_t>& values);
uint64_t get_number_of_volumes(uint64_t size,
                               const std::vector<uint64_t>& vol_sizes);
size_t volume_number_digits(uint64_t num_volumes);
std::string volume_sequence_name(uint64_t index, size_t digits);

struct VolumeSequenceState {
  std::string unchanged_part;
  std::string changed_part{"000"};

  std::string next_name();
  bool parse_name(const std::string& name);
};

std::string astring_to_std(const AString& value);
std::string ustring_to_utf8(const UString& value);
UString utf8_to_ustring(const std::string& value);

OperationResult invalid_request(const std::string& summary);
OperationResult unsupported_request(const std::string& summary);

void emit_log_event(const ArchiveBackendHooks& hooks,
                    OperationStage stage,
                    OutputChannel channel,
                    const std::string& message,
                    const std::optional<BenchmarkTypedSnapshot>& benchmark_snapshot =
                        std::nullopt,
                    const std::optional<BenchmarkTypedSummary>& benchmark_summary =
                        std::nullopt);
void emit_progress_event(const ArchiveBackendHooks& hooks,
                         OperationStage stage,
                         int percent,
                         bool totals_known,
                         uint64_t total_bytes,
                         uint64_t completed_bytes,
                         uint64_t total_files,
                         uint64_t completed_files,
                         uint64_t error_count,
                         const std::string& current_path,
                         const std::string& message = {},
                         const std::optional<ProgressRatioInfo>& ratio_info =
                             std::nullopt,
                         const std::optional<BenchmarkTypedSnapshot>& benchmark_snapshot =
                             std::nullopt,
                         const std::optional<BenchmarkTypedSummary>& benchmark_summary =
                             std::nullopt);

bool ensure_parent_dir(const fs::path& path, std::error_code& ec);
bool remove_path_any(const fs::path& path, std::error_code& ec);
bool copy_path_any(const fs::path& src,
                   const fs::path& dst,
                   bool overwrite,
                   std::error_code& ec);
bool move_path_to_recycle_bin(const fs::path& path, std::error_code& ec);
ArchiveError map_hresult_to_archive_error(int hr);
int load_codecs_shared(CCodecs& codecs);
int prepare_open_types_for_archive(const std::string& archive_type_hint,
                                   CCodecs& codecs,
                                   CObjectVector<COpenType>& types);
int open_archive_shared(const std::string& archive_path,
                        const std::string& archive_type_hint,
                        const ArchiveBackendHooks& hooks,
                        std::atomic<bool>* cancel_requested,
                        std::function<bool()> wait_while_paused,
                        bool enable_open_callback,
                        bool codecs_already_loaded,
                        CCodecs& codecs,
                        CObjectVector<COpenType>& types,
                        CIntVector& excluded_formats,
                        CArchiveLink& archive_link,
                        const CArc*& arc,
                        bool* out_password_requested = nullptr,
                        bool* out_wrong_password = nullptr,
                        std::string* out_password = nullptr);

// Open an archive whose bytes are already available as an IInStream (seekable)
// rather than a filesystem path. `display_path` is used purely for extension /
// format-hint resolution, not for I/O.
int open_archive_shared_from_stream(IInStream* in_stream,
                                    const std::string& display_path,
                                    const std::string& archive_type_hint,
                                    const ArchiveBackendHooks& hooks,
                                    std::atomic<bool>* cancel_requested,
                                    std::function<bool()> wait_while_paused,
                                    bool enable_open_callback,
                                    bool codecs_already_loaded,
                                    CCodecs& codecs,
                                    CObjectVector<COpenType>& types,
                                    CIntVector& excluded_formats,
                                    CArchiveLink& archive_link,
                                    const CArc*& arc,
                                    bool* out_password_requested = nullptr,
                                    bool* out_wrong_password = nullptr,
                                    std::string* out_password = nullptr);

struct OpenArchiveReadState {
  CCodecs codecs;
  CObjectVector<COpenType> types;
  CIntVector excluded_formats;
  CArchiveLink archive_link;
  const CArc* arc = nullptr;
};

template <typename TResult>
TResult make_operation_failure_from_hresult(int hr);

template <typename TResult, typename Handler>
TResult run_with_open_archive_read(const std::string& archive_path,
                                   const std::string& archive_type_hint,
                                   const ArchiveBackendHooks& hooks,
                                   std::atomic<bool>* cancel_requested,
                                   std::function<bool()> wait_while_paused,
                                   bool enable_open_callback,
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
                                               enable_open_callback,
                                               preloaded_codecs != nullptr,
                                               codecs,
                                               open_state.types,
                                               open_state.excluded_formats,
                                               open_state.archive_link,
                                               open_state.arc,
                                               &password_requested,
                                               &wrong_password,
                                               nullptr);
  if (open_res != S_OK) {
    if (password_requested || wrong_password) {
      return make_operation_failure<TResult>(ArchiveErrorDomain::kPassword,
                                             "Password required or incorrect",
                                             2);
    }
    return make_operation_failure_from_hresult<TResult>(open_res);
  }

  UInt32 num_items = 0;
  const HRESULT num_res = open_state.arc->Archive->GetNumberOfItems(&num_items);
  if (num_res != S_OK) {
    return make_operation_failure_from_hresult<TResult>(num_res);
  }

  return handler(open_state, num_items);
}

template <typename TResult, typename Handler>
TResult run_with_open_archive_read(const std::string& archive_path,
                                   const std::string& archive_type_hint,
                                   const ArchiveBackendHooks& hooks,
                                   std::atomic<bool>* cancel_requested,
                                   std::function<bool()> wait_while_paused,
                                   bool enable_open_callback,
                                   Handler&& handler) {
  return run_with_open_archive_read<TResult>(archive_path,
                                             archive_type_hint,
                                             hooks,
                                             cancel_requested,
                                             std::move(wait_while_paused),
                                             enable_open_callback,
                                             nullptr,
                                             std::forward<Handler>(handler));
}

template <typename Handler>
int run_with_open_archive_read_hresult(const std::string& archive_path,
                                       const std::string& archive_type_hint,
                                       const ArchiveBackendHooks& hooks,
                                       std::atomic<bool>* cancel_requested,
                                       std::function<bool()> wait_while_paused,
                                       bool enable_open_callback,
                                       CCodecs* preloaded_codecs,
                                       Handler&& handler) {
  OpenArchiveReadState open_state;
  CCodecs& codecs = preloaded_codecs != nullptr ? *preloaded_codecs : open_state.codecs;
  const HRESULT open_res = open_archive_shared(archive_path,
                                               archive_type_hint,
                                               hooks,
                                               cancel_requested,
                                               std::move(wait_while_paused),
                                               enable_open_callback,
                                               preloaded_codecs != nullptr,
                                               codecs,
                                               open_state.types,
                                               open_state.excluded_formats,
                                               open_state.archive_link,
                                               open_state.arc);
  if (open_res != S_OK) {
    return open_res;
  }

  UInt32 num_items = 0;
  const HRESULT num_res = open_state.arc->Archive->GetNumberOfItems(&num_items);
  if (num_res != S_OK) {
    return num_res;
  }

  return handler(open_state, num_items);
}

template <typename Handler>
int run_with_open_archive_read_hresult(const std::string& archive_path,
                                       const std::string& archive_type_hint,
                                       const ArchiveBackendHooks& hooks,
                                       std::atomic<bool>* cancel_requested,
                                       std::function<bool()> wait_while_paused,
                                       bool enable_open_callback,
                                       Handler&& handler) {
  return run_with_open_archive_read_hresult(archive_path,
                                            archive_type_hint,
                                            hooks,
                                            cancel_requested,
                                            std::move(wait_while_paused),
                                            enable_open_callback,
                                            nullptr,
                                            std::forward<Handler>(handler));
}

}  // namespace z7::app
