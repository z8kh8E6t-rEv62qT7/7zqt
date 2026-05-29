// src/archive_application/src/native_7z/session/session_registry_open_parent.cpp
// Role: Parent-session nested archive open flow and fallback strategies.

#include "session/session_registry_internal.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <utility>

#include "core/internal.h"
#include "third_party_adapter/callbacks_extract_run.h"
#include "third_party_adapter/callbacks_extract_stream.h"

namespace z7::app {

namespace {

constexpr size_t kMinStrategy2Budget = 4u * 1024u * 1024u;

size_t compute_nested_open_budget(size_t depth) {
  size_t ram_size = static_cast<size_t>(sizeof(size_t)) << 29;
  if (!NWindows::NSystem::GetRamSize(ram_size) || ram_size == 0) {
    return kMinStrategy2Budget;
  }

  const size_t shift = std::max<size_t>(depth + 1, 8u);
  const size_t total_bits = sizeof(size_t) * 8u;
  size_t budget = 0;
  if (shift < total_bits) {
    budget = ram_size >> shift;
  }
  return std::max<size_t>(budget, kMinStrategy2Budget);
}

// Holds a COM CBufInStream alive for the session. We instantiate via
// CMyComPtr2_Create so the handle owns the underlying object.
struct StreamRefHolder {
  CMyComPtr<IInStream> stream;
};

// Holds an IInStream derived from the parent's IInArchiveGetStream for
// strategy 1. We keep both the original sequential stream and its IInStream
// QI result so reference counts stay balanced for the archive lifetime.
struct ParentStreamRefHolder {
  CMyComPtr<ISequentialInStream> seq;
  CMyComPtr<IInStream> seekable;
};

// Best-effort unique temp directory under the system temp root.
std::filesystem::path make_temp_session_dir() {
  namespace fs = std::filesystem;
  std::error_code ec;
  fs::path base = fs::temp_directory_path(ec);
  if (ec || base.empty()) {
    base = fs::path(".");
  }
  const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
  fs::path candidate =
      base / (std::string("z7-session-") + std::to_string(static_cast<long long>(ticks)));
  for (int i = 0; i < 32; ++i) {
    std::error_code dir_ec;
    if (fs::create_directories(candidate, dir_ec)) {
      return candidate;
    }
    candidate = base /
                (std::string("z7-session-") +
                 std::to_string(static_cast<long long>(ticks + i + 1)));
  }
  return {};
}

void remove_path_tree(const std::filesystem::path& path) {
  if (path.empty()) {
    return;
  }
  std::error_code ec;
  std::filesystem::remove_all(path, ec);
}

// Extract a single entry of the parent archive into `sink` using the
// standard extract callback wired with a buffer sink.
HRESULT extract_entry_to_buffer(ArchiveOpenSession& parent,
                                UInt32 entry_index,
                                const std::string& password,
                                size_t size_budget,
                                std::vector<uint8_t>& sink,
                                std::atomic<bool>* cancel_requested,
                                std::function<bool()> wait_while_paused) {
  CArchiveLink& link = archive_session_link(parent);
  const CArc* arc = link.GetArc();
  if (arc == nullptr || arc->Archive == nullptr) {
    return E_FAIL;
  }

  sink.clear();
  sink.reserve(std::min<size_t>(size_budget, 1u << 16));

  ArchiveBackendHooks no_hooks;
  auto* callback = new NativeExtractCallback(arc->Archive,
                                              std::filesystem::path{},
                                              no_hooks,
                                              cancel_requested,
                                              std::move(wait_while_paused),
                                              parent.display_path(),
                                              {},
                                              OverwriteMode::kOverwrite,
                                              ExtractPathMode::kFullPaths,
                                              std::string{},
                                              {},
                                              password,
                                              ExtractZoneIdMode::kNone,
                                              false,
                                              1);
  callback->set_buffer_sink(&sink, size_budget);

  const UInt32 indices[1] = {entry_index};
  const HRESULT hr = arc->Archive->Extract(indices, 1, /*testMode=*/0, callback);
  callback->Release();
  return hr;
}

// Try to obtain a seekable IInStream for `entry_index` from the parent
// archive via IInArchiveGetStream. Returns S_OK with populated holder on
// success, S_FALSE if the parent format does not expose the interface /
// stream, or an HRESULT on hard failure.
HRESULT acquire_parent_sub_stream(ArchiveOpenSession& parent,
                                  UInt32 entry_index,
                                  ParentStreamRefHolder& out) {
  CArchiveLink& link = archive_session_link(parent);
  const CArc* arc = link.GetArc();
  if (arc == nullptr || arc->Archive == nullptr) {
    return E_FAIL;
  }

  CMyComPtr<IInArchiveGetStream> get_stream;
  if (arc->Archive->QueryInterface(IID_IInArchiveGetStream,
                                    reinterpret_cast<void**>(&get_stream)) != S_OK ||
      !get_stream) {
    return S_FALSE;
  }

  CMyComPtr<ISequentialInStream> seq;
  const HRESULT get_res = get_stream->GetStream(entry_index, &seq);
  if (get_res != S_OK || !seq) {
    return S_FALSE;
  }

  CMyComPtr<IInStream> seekable;
  if (seq.QueryInterface(IID_IInStream, &seekable) != S_OK || !seekable) {
    return S_FALSE;
  }

  out.seq = std::move(seq);
  out.seekable = std::move(seekable);
  return S_OK;
}

}  // namespace

OpenArchiveSessionResult open_native_archive_session_from_parent(
    ArchiveSessionRegistry& registry,
    const OpenArchiveFromParentRequest& request,
    const ArchiveBackendHooks& hooks,
    std::atomic<bool>* cancel_requested,
    std::function<bool()> wait_while_paused) {
  OpenArchiveSessionResult result;

  auto parent = registry.find(request.parent);
  if (!parent) {
    static_cast<OperationResult&>(result) =
        make_operation_failure<OperationResult>(
            ArchiveErrorDomain::kInvalidArguments,
            "Parent session not found",
            7);
    return result;
  }

  // Resolve the explicit child selector before attempting any nested-open
  // strategy so underspecified requests fail fast and audibly.
  const bool has_entry_path = !request.entry_path.empty();
  const bool has_entry_index = request.entry_index.has_value();
  if (has_entry_path == has_entry_index) {
    static_cast<OperationResult&>(result) =
        make_operation_failure<OperationResult>(
            ArchiveErrorDomain::kInvalidArguments,
            "OpenArchiveFromParent requires exactly one selector",
            7);
    return result;
  }

  CArchiveLink& parent_link = archive_session_link(*parent);
  const CArc* parent_arc = parent_link.GetArc();
  if (parent_arc == nullptr || parent_arc->Archive == nullptr) {
    static_cast<OperationResult&>(result) =
        make_operation_failure<OperationResult>(
            ArchiveErrorDomain::kInvalidArguments,
            "Parent archive unavailable",
            7);
    return result;
  }

  UInt32 num_items = 0;
  if (parent_arc->Archive->GetNumberOfItems(&num_items) != S_OK) {
    static_cast<OperationResult&>(result) =
        make_operation_failure<OperationResult>(
            ArchiveErrorDomain::kUnknown,
            "GetNumberOfItems failed on parent",
            2);
    return result;
  }

  UInt32 resolved_index = 0;
  std::string resolved_entry_path;
  if (has_entry_path) {
    const std::string needle = normalize_archive_item_path(request.entry_path);
    bool found = false;
    for (UInt32 i = 0; i < num_items; ++i) {
      const std::string candidate = normalize_archive_item_path(
          archive_get_prop_text(parent_arc->Archive, i, kpidPath));
      if (candidate == needle) {
        resolved_index = i;
        resolved_entry_path = candidate;
        found = true;
        break;
      }
    }
    if (!found) {
      static_cast<OperationResult&>(result) =
          make_operation_failure<OperationResult>(
              ArchiveErrorDomain::kInvalidArguments,
              "Entry path not found in parent archive: " + request.entry_path,
              7);
      return result;
    }
  } else {
    resolved_index = static_cast<UInt32>(*request.entry_index);
    if (resolved_index >= num_items) {
      static_cast<OperationResult&>(result) =
          make_operation_failure<OperationResult>(
              ArchiveErrorDomain::kInvalidArguments,
              "Entry index out of range in parent archive",
              7);
      return result;
    }
    resolved_entry_path = normalize_archive_item_path(
        archive_get_prop_text(parent_arc->Archive, resolved_index, kpidPath));
  }

  const std::string display_path = request.display_path_hint.empty()
                                       ? parent->display_path()
                                       : request.display_path_hint;

  auto child = std::make_shared<ArchiveOpenSession>();
  ArchiveOpenSessionNativeAccess::set_display_path(*child, display_path);
  ArchiveOpenSessionNativeAccess::set_parent(*child, parent);
  ArchiveOpenSessionNativeAccess::set_entry_path_from_parent(
      *child,
      resolved_entry_path);
  child->set_password(parent->password());
  reset_archive_session_open_state(*child);
  ArchiveOpenSessionState& child_state = archive_session_state(*child);

  auto finalize_success = [&](OpenArchiveSessionResult::Strategy used) {
    ArchiveOpenSessionNativeAccess::set_strategy(*child, used);
    ArchiveOpenSessionNativeAccess::set_token(
        *child,
        ArchiveSessionRegistryNativeAccess::allocate_token(registry));
    ArchiveSessionRegistryNativeAccess::register_session(registry, child);
    static_cast<OperationResult&>(result) =
        make_operation_success<OperationResult>("Nested archive opened");
    result.token = child->token();
    result.used_strategy = used;
    result.archive_path = child->display_path();
  };

  // Strategy 1: parent IInArchiveGetStream -> seekable IInStream.
  {
    ParentStreamRefHolder holder;
    const HRESULT acquire = acquire_parent_sub_stream(*parent,
                                                       resolved_index,
                                                       holder);
    if (acquire == E_ABORT) {
      static_cast<OperationResult&>(result) =
          make_operation_canceled<OperationResult>();
      return result;
    }
    if (acquire == S_OK) {
      IInStream* stream_raw = holder.seekable;
      const CArc* arc = nullptr;
      bool password_requested = false;
      bool wrong_password = false;
      std::string password;
      const HRESULT open_hr = open_archive_shared_from_stream(
          stream_raw,
          display_path,
          request.archive_type_hint,
          hooks,
          cancel_requested,
          wait_while_paused,
          /*enable_open_callback=*/true,
          /*codecs_already_loaded=*/false,
          *child_state.codecs,
          *child_state.types,
          *child_state.excluded_formats,
          *child_state.archive_link,
          arc,
          &password_requested,
          &wrong_password,
          &password);
      if (open_hr == S_OK) {
        child->set_password(std::move(password));
        auto holder_heap = std::make_shared<ParentStreamRefHolder>(std::move(holder));
        child_state.stream_ref_holder = std::static_pointer_cast<void>(holder_heap);
        finalize_success(OpenArchiveSessionResult::Strategy::kStream);
        return result;
      }
      if (password_requested || wrong_password || !password.empty()) {
        static_cast<OperationResult&>(result) =
            make_operation_failure<OperationResult>(
                ArchiveErrorDomain::kPassword,
                "Password required or incorrect",
                2);
        return result;
      }
      if (open_hr == E_ABORT) {
        static_cast<OperationResult&>(result) =
            make_operation_canceled<OperationResult>();
        return result;
      }
      // Rebuild a fresh CArchiveLink / types / codecs; the previous attempt
      // may have partially consumed internal state.
      reset_archive_session_open_state(*child);
      // fall through to strategy 2
    }
  }

  // Strategy 2: extract to in-memory buffer.
  const size_t effective_budget = request.size_budget != 0
                                    ? std::max<size_t>(request.size_budget,
                                                       kMinStrategy2Budget)
                                    : compute_nested_open_budget(child->depth());
  {
    std::vector<uint8_t> buffer;
    const HRESULT extract_hr = extract_entry_to_buffer(*parent,
                                                        resolved_index,
                                                        child->password(),
                                                        effective_budget,
                                                        buffer,
                                                        cancel_requested,
                                                        wait_while_paused);
    if (extract_hr == E_ABORT) {
      static_cast<OperationResult&>(result) =
          make_operation_canceled<OperationResult>();
      return result;
    }
    if (extract_hr == S_OK && !buffer.empty()) {
      child_state.memory_buffer = std::move(buffer);

      auto holder = std::make_shared<StreamRefHolder>();
      CMyComPtr2_Create<IInStream, CBufInStream> buf_stream;
      buf_stream->Init(child_state.memory_buffer.data(),
                       child_state.memory_buffer.size(),
                       /*ref=*/nullptr);
      holder->stream = buf_stream;

      const CArc* arc = nullptr;
      bool password_requested = false;
      bool wrong_password = false;
      std::string password;
      const HRESULT open_hr = open_archive_shared_from_stream(
          holder->stream,
          display_path,
          request.archive_type_hint,
          hooks,
          cancel_requested,
          wait_while_paused,
          /*enable_open_callback=*/true,
          /*codecs_already_loaded=*/false,
          *child_state.codecs,
          *child_state.types,
          *child_state.excluded_formats,
          *child_state.archive_link,
          arc,
          &password_requested,
          &wrong_password,
          &password);
      if (open_hr == S_OK) {
        child->set_password(std::move(password));
        child_state.stream_ref_holder = std::static_pointer_cast<void>(holder);
        finalize_success(OpenArchiveSessionResult::Strategy::kMemory);
        return result;
      }
      if (password_requested || wrong_password || !password.empty()) {
        static_cast<OperationResult&>(result) =
            make_operation_failure<OperationResult>(
                ArchiveErrorDomain::kPassword,
                "Password required or incorrect",
                2);
        return result;
      }
      if (open_hr == E_ABORT) {
        static_cast<OperationResult&>(result) =
            make_operation_canceled<OperationResult>();
        return result;
      }
      // Reset per-attempt state and fall through.
      child_state.memory_buffer.clear();
      child_state.memory_buffer.shrink_to_fit();
      reset_archive_session_open_state(*child);
    }
  }

  // Strategy 3: extract to temp file, open as path.
  {
    namespace fs = std::filesystem;
    fs::path dir = make_temp_session_dir();
    if (dir.empty()) {
      static_cast<OperationResult&>(result) =
          make_operation_failure<OperationResult>(
              ArchiveErrorDomain::kIo,
              "Failed to create temp dir for nested archive",
              2);
      return result;
    }

    CArchiveLink& parent_link = archive_session_link(*parent);
    const CArc* parent_arc = parent_link.GetArc();
    if (parent_arc == nullptr || parent_arc->Archive == nullptr) {
      remove_path_tree(dir);
      static_cast<OperationResult&>(result) =
          make_operation_failure<OperationResult>(
              ArchiveErrorDomain::kIo,
              "Parent archive not available",
              2);
      return result;
    }

    ArchiveBackendHooks no_hooks;
    auto* callback = new NativeExtractCallback(parent_arc->Archive,
                                                dir,
                                                no_hooks,
                                                cancel_requested,
                                                wait_while_paused,
                                                parent->display_path(),
                                                {},
                                                OverwriteMode::kOverwrite,
                                                ExtractPathMode::kNoPaths,
                                                std::string{},
                                                {},
                                                child->password(),
                                                ExtractZoneIdMode::kNone,
                                                false,
                                                1);
    const UInt32 indices[1] = {resolved_index};
    const HRESULT ex_hr =
        parent_arc->Archive->Extract(indices, 1, /*testMode=*/0, callback);
    callback->Release();
    if (ex_hr != S_OK) {
      remove_path_tree(dir);
      static_cast<OperationResult&>(result) =
          (ex_hr == E_ABORT)
              ? make_operation_canceled<OperationResult>()
              : make_operation_failure_from_hresult<OperationResult>(ex_hr);
      return result;
    }

    // Find the single extracted file under `dir`. NoPaths mode flattens so
    // the output sits directly under the directory.
    fs::path extracted;
    std::error_code it_ec;
    for (const auto& entry : fs::directory_iterator(dir, it_ec)) {
      if (entry.is_regular_file()) {
        extracted = entry.path();
        break;
      }
    }
    if (extracted.empty()) {
      remove_path_tree(dir);
      static_cast<OperationResult&>(result) =
          make_operation_failure<OperationResult>(
              ArchiveErrorDomain::kIo,
              "Extracted temp file not found",
              2);
      return result;
    }

    child_state.temp_dir = dir;
    child_state.temp_file = std::make_unique<fs::path>(extracted);

    const CArc* arc = nullptr;
    bool password_requested = false;
    bool wrong_password = false;
    std::string password;
    const HRESULT open_hr = open_archive_shared(
        extracted.generic_string(),
        request.archive_type_hint,
        hooks,
        cancel_requested,
        std::move(wait_while_paused),
        /*enable_open_callback=*/true,
        /*codecs_already_loaded=*/false,
        *child_state.codecs,
        *child_state.types,
        *child_state.excluded_formats,
        *child_state.archive_link,
        arc,
        &password_requested,
        &wrong_password,
        &password);
    if (open_hr != S_OK) {
      if (password_requested || wrong_password || !password.empty()) {
        static_cast<OperationResult&>(result) =
            make_operation_failure<OperationResult>(
                ArchiveErrorDomain::kPassword,
                "Password required or incorrect",
                2);
      } else {
        static_cast<OperationResult&>(result) =
            (open_hr == E_ABORT)
                ? make_operation_canceled<OperationResult>()
                : make_operation_failure_from_hresult<OperationResult>(open_hr);
      }
      return result;
    }

    child->set_password(std::move(password));
    finalize_success(OpenArchiveSessionResult::Strategy::kTempFile);
    return result;
  }
}

}  // namespace z7::app
