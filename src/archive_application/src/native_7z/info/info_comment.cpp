// src/archive_application/src/native_7z/info/info_comment.cpp
// Role: Archive comment target resolution and update operation.

#include "core/internal.h"
#include "core/filesystem_replace.h"
#include "descript_ion_store.h"
#include "third_party_adapter/third_party_adapter.h"
#include "third_party_adapter/callbacks_update.h"
#include "session/session_registry_internal.h"

namespace z7::app {
namespace {

struct ArchiveCommentTarget final {
  std::string archive_path;
  std::string item_path;
};

template <typename TResult>
void assign_comment_error(TResult& result, const ArchiveError& error) {
  result.ok = false;
  result.error = error;
  result.native_exit_code = result.error.native_exit_code;
  result.native_execution.native_exit_code = result.native_exit_code;
  result.native_execution.termination_reason =
      result.error.domain == ArchiveErrorDomain::kCanceled
          ? NativeTerminationReason::kCanceled
          : NativeTerminationReason::kCompleted;
  result.summary = describe_archive_error(result.error);
}

std::string normalize_comment_item_path(const std::string& raw_item_path) {
  return normalize_archive_virtual_directory(
      z7::common::trim_ascii_space_copy(raw_item_path));
}

bool resolve_comment_target(const ArchiveCommentRequest& request,
                            ArchiveCommentTarget& out_target,
                            std::string& error_summary) {
  std::string archive_path =
      z7::common::trim_ascii_space_copy(request.archive_path);
  std::string item_path =
      z7::common::trim_ascii_space_copy(request.entry_path);
  if (archive_path.empty() || item_path.empty()) {
    error_summary = "Archive comment request requires archive_path and entry_path.";
    return false;
  }

  item_path = normalize_comment_item_path(item_path);
  if (item_path.empty()) {
    error_summary = "Archive comment request entry_path resolves to empty virtual path.";
    return false;
  }

  out_target.archive_path = std::move(archive_path);
  out_target.item_path = std::move(item_path);
  return true;
}

HRESULT resolve_comment_arc_index_via_proxy2(const CArc& arc,
                                             const std::string& item_path,
                                             UInt32& out_arc_index) {
  CProxyArc2 proxy;
  RINOK(proxy.Load(arc, nullptr))
  if (proxy.Dirs.Size() <= k_Proxy2_RootDirIndex) {
    return E_FAIL;
  }

  const std::vector<std::string> parts =
      split_archive_virtual_directory(normalize_comment_item_path(item_path));
  if (parts.empty()) {
    return E_INVALIDARG;
  }

  unsigned target_dir_index = k_Proxy2_RootDirIndex;
  for (size_t i = 0; i + 1 < parts.size(); ++i) {
    const UString part_u = utf8_to_ustring(parts[i]);
    const int pos = proxy.FindItem(target_dir_index, part_u.Ptr(), true);
    if (pos < 0) {
      return E_INVALIDARG;
    }

    const CProxyDir2& current_dir = proxy.Dirs[target_dir_index];
    if (static_cast<unsigned>(pos) >= current_dir.Items.Size()) {
      return E_INVALIDARG;
    }

    const UInt32 arc_index = current_dir.Items[static_cast<unsigned>(pos)];
    const CProxyFile2& file = proxy.Files[arc_index];
    if (!file.IsDir() || file.DirIndex < 0 ||
        static_cast<unsigned>(file.DirIndex) >= proxy.Dirs.Size()) {
      return E_INVALIDARG;
    }
    target_dir_index = static_cast<unsigned>(file.DirIndex);
  }

  const UString leaf_name = utf8_to_ustring(parts.back());
  const int leaf_pos = proxy.FindItem(target_dir_index, leaf_name.Ptr(), false);
  if (leaf_pos < 0) {
    return E_INVALIDARG;
  }
  const CProxyDir2& target_dir = proxy.Dirs[target_dir_index];
  if (static_cast<unsigned>(leaf_pos) >= target_dir.Items.Size()) {
    return E_INVALIDARG;
  }
  out_arc_index = target_dir.Items[static_cast<unsigned>(leaf_pos)];
  return S_OK;
}

HRESULT resolve_comment_arc_index_via_proxy(const CArc& arc,
                                            const std::string& item_path,
                                            UInt32& out_arc_index) {
  CProxyArc proxy;
  RINOK(proxy.Load(arc, nullptr))
  if (proxy.Dirs.Size() <= k_Proxy_RootDirIndex) {
    return E_FAIL;
  }

  const std::vector<std::string> parts =
      split_archive_virtual_directory(normalize_comment_item_path(item_path));
  if (parts.empty()) {
    return E_INVALIDARG;
  }

  unsigned target_dir_index = k_Proxy_RootDirIndex;
  for (size_t i = 0; i + 1 < parts.size(); ++i) {
    const UString part_u = utf8_to_ustring(parts[i]);
    const int next = proxy.FindSubDir(target_dir_index, part_u.Ptr());
    if (next < 0) {
      return E_INVALIDARG;
    }
    target_dir_index = static_cast<unsigned>(next);
  }

  const UString leaf_name = utf8_to_ustring(parts.back());
  const CProxyDir& dir = proxy.Dirs[target_dir_index];
  for (unsigned i = 0; i < dir.SubDirs.Size(); ++i) {
    const unsigned sub_dir_index = dir.SubDirs[i];
    const CProxyDir& sub_dir = proxy.Dirs[sub_dir_index];
    if (UString(sub_dir.Name).IsEqualTo_NoCase(leaf_name)) {
      if (sub_dir.ArcIndex < 0) {
        return E_INVALIDARG;
      }
      out_arc_index = static_cast<UInt32>(sub_dir.ArcIndex);
      return S_OK;
    }
  }

  for (unsigned i = 0; i < dir.SubFiles.Size(); ++i) {
    const UInt32 arc_index = dir.SubFiles[i];
    const CProxyFile& file = proxy.Files[arc_index];
    if (UString(file.Name).IsEqualTo_NoCase(leaf_name)) {
      out_arc_index = arc_index;
      return S_OK;
    }
  }

  return E_INVALIDARG;
}

HRESULT resolve_comment_arc_index(const CArc& arc,
                                  const std::string& item_path,
                                  UInt32& out_arc_index) {
  if (arc.GetRawProps && arc.IsTree) {
    return resolve_comment_arc_index_via_proxy2(arc, item_path, out_arc_index);
  }
  return resolve_comment_arc_index_via_proxy(arc, item_path, out_arc_index);
}

fs::path make_comment_temp_path(const fs::path& archive_path,
                                std::error_code& ec) {
  ec.clear();
  const std::string stem = archive_path.filename().string();
  const uint64_t base =
      static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
  for (uint64_t i = 0; i < 64; ++i) {
    fs::path candidate = archive_path.parent_path() /
                         fs::path(stem + ".z7_comment_tmp_" + std::to_string(base + i));
    std::error_code exists_ec;
    if (!fs::exists(candidate, exists_ec)) {
      if (exists_ec) {
        ec = exists_ec;
        return {};
      }
      return candidate;
    }
    if (exists_ec) {
      ec = exists_ec;
      return {};
    }
  }
  ec = std::make_error_code(std::errc::file_exists);
  return {};
}

ArchiveError map_comment_hresult(HRESULT hr) {
  if (hr == E_NOTIMPL || hr == E_NOINTERFACE) {
    return make_archive_error(ArchiveErrorDomain::kUnsupportedFormat,
                              "Comment operation is unsupported for this archive format",
                              2);
  }
  return map_hresult_to_archive_error(hr);
}

ArchiveError map_comment_set_property_hresult(HRESULT hr) {
  if (hr == E_NOTIMPL || hr == E_NOINTERFACE || hr == E_INVALIDARG) {
    return make_archive_error(ArchiveErrorDomain::kUnsupportedFormat,
                              "Comment operation is unsupported for this archive format",
                              2);
  }
  return map_comment_hresult(hr);
}

}  // namespace

ArchiveCommentResult NativeArchiveBackend::comment_archive(
    const ArchiveCommentRequest& request,
    const ArchiveBackendHooks& hooks) {
  ArchiveCommentTarget target;
  std::string resolve_error;
  if (!resolve_comment_target(request, target, resolve_error)) {
    return from_base_result<ArchiveCommentResult>(invalid_request(resolve_error));
  }

  if (request.session_token.has_value() && request.session_token->is_valid()) {
    auto session = ArchiveSessionRegistry::instance().find(*request.session_token);
    if (!session) {
      return make_operation_failure<ArchiveCommentResult>(
          ArchiveErrorDomain::kInvalidArguments,
          "Unknown archive session token",
          7);
    }
    if (std::optional<OperationResult> materialize_error =
            ensure_archive_session_writable(
                *session,
                hooks,
                &cancel_requested_,
                [this]() { return this->wait_while_paused(); });
        materialize_error.has_value()) {
      return from_base_result<ArchiveCommentResult>(std::move(*materialize_error));
    }

    const ArchiveOpenSessionState& state = archive_session_state(*session);
    if (state.temp_file == nullptr || state.temp_file->empty()) {
      return make_operation_failure<ArchiveCommentResult>(
          ArchiveErrorDomain::kIo,
          "Writable archive session does not have a backing file",
          2);
    }

    ArchiveCommentRequest writable_request = request;
    writable_request.session_token.reset();
    writable_request.archive_path = state.temp_file->string();

    ArchiveCommentResult result = comment_archive(writable_request, hooks);
    if (!result.ok) {
      return result;
    }
    ArchiveOpenSessionNativeAccess::set_dirty(*session, true);
    if (std::optional<OperationResult> refresh_error =
            refresh_archive_session_from_backing_file(
                *session,
                hooks,
                &cancel_requested_,
                [this]() { return this->wait_while_paused(); });
        refresh_error.has_value()) {
      return from_base_result<ArchiveCommentResult>(std::move(*refresh_error));
    }
    return result;
  }

  const fs::path archive_path(target.archive_path);
  std::error_code path_ec;
  if (!fs::exists(archive_path, path_ec) || path_ec || fs::is_directory(archive_path, path_ec)) {
    return from_base_result<ArchiveCommentResult>(
        invalid_request("Archive comment request archive_path must point to an existing archive file"));
  }

  return run_open_archive_read_pipeline<ArchiveCommentResult>(
      target.archive_path,
      {},
      hooks,
      true,
      [&](OpenArchiveReadState& open_state, UInt32 num_items) -> ArchiveCommentResult {
        ArchiveCommentResult result;
        const CArc* arc = open_state.arc;

        UInt32 target_arc_index = 0;
        const HRESULT resolve_index_res =
            resolve_comment_arc_index(*arc, target.item_path, target_arc_index);
        if (resolve_index_res != S_OK) {
          assign_comment_error(result, map_comment_hresult(resolve_index_res));
          return result;
        }

        if (target_arc_index >= num_items) {
          assign_comment_error(result, map_comment_hresult(E_INVALIDARG));
          return result;
        }

        CMyComPtr<IOutArchive> out_archive;
        const HRESULT query_out_res =
            arc->Archive->QueryInterface(IID_IOutArchive, (void**)&out_archive);
        if (query_out_res != S_OK || !out_archive) {
          assign_comment_error(result, map_comment_hresult(query_out_res));
          return result;
        }

        CRecordVector<CUpdatePair2> update_pairs;
        update_pairs.Reserve(num_items);
        for (UInt32 i = 0; i < num_items; ++i) {
          CUpdatePair2 pair;
          pair.SetAs_NoChangeArcItem(i);
          if (i == target_arc_index) {
            pair.NewProps = true;
          }
          update_pairs.Add(pair);
        }

        const UString comment_text = utf8_to_ustring(request.comment);
        CMyComPtr2_Create<IArchiveUpdateCallback, CArchiveUpdateCallback> update_callback;
        NativeUpdateOperationCallback update_operation_callback(
            hooks,
            &cancel_requested_,
            [this]() { return this->wait_while_paused(); },
            target.archive_path,
            NativeUpdateOperationCallback::Mode::kAdd);
        update_callback->Callback = &update_operation_callback;
        update_callback->UpdatePairs = &update_pairs;
        update_callback->CommentIndex = static_cast<int>(target_arc_index);
        update_callback->Comment = &comment_text;
        update_callback->Arc = arc;
        update_callback->Archive = arc->Archive;
        update_callback->ArcFileName =
            utf8_to_ustring(archive_path.filename().string());

        std::error_code temp_path_ec;
        const fs::path temp_path = make_comment_temp_path(archive_path, temp_path_ec);
        if (temp_path.empty()) {
          std::string message = "Failed to allocate temporary archive output path";
          if (temp_path_ec) {
            message += ": " + temp_path_ec.message();
          }
          assign_comment_error(
              result,
              make_archive_error(ArchiveErrorDomain::kIo,
                                 std::move(message),
                                 2));
          return result;
        }
        COutFileStream* out_stream_spec = new COutFileStream();
        CMyComPtr<IOutStream> out_stream(out_stream_spec);
        if (!out_stream_spec->Create_ALWAYS(us2fs(utf8_to_ustring(temp_path.string())))) {
          assign_comment_error(
              result,
              make_archive_error(ArchiveErrorDomain::kIo,
                                 "Failed to create temporary archive output",
                                 2));
          return result;
        }

        CMyComPtr<ISequentialOutStream> update_stream;
        if (arc->ArcStreamOffset == 0) {
          update_stream = out_stream;
        } else {
          const HRESULT seek_res = arc->InStream->Seek(0, STREAM_SEEK_SET, nullptr);
          if (seek_res != S_OK) {
            out_stream_spec->Close();
            std::error_code cleanup_ec;
            fs::remove(temp_path, cleanup_ec);
            assign_comment_error(result, map_comment_hresult(seek_res));
            return result;
          }
          const HRESULT copy_res = NCompress::CopyStream_ExactSize(
              arc->InStream, out_stream, arc->ArcStreamOffset, nullptr);
          if (copy_res != S_OK) {
            out_stream_spec->Close();
            std::error_code cleanup_ec;
            fs::remove(temp_path, cleanup_ec);
            assign_comment_error(result, map_comment_hresult(copy_res));
            return result;
          }

          CTailOutStream* tail_stream_spec = new CTailOutStream;
          CMyComPtr<IOutStream> tail_stream(tail_stream_spec);
          tail_stream_spec->Stream = out_stream;
          tail_stream_spec->Offset = arc->ArcStreamOffset;
          tail_stream_spec->Init();
          update_stream = tail_stream;
        }

        HRESULT update_res = out_archive->UpdateItems(update_stream,
                                                      static_cast<UInt32>(update_pairs.Size()),
                                                      update_callback);

        const HRESULT close_res = out_stream_spec->Close();
        if (update_res == S_OK && close_res != S_OK) {
          update_res = close_res;
        }

        if (cancel_requested_.load()) {
          std::error_code cleanup_ec;
          fs::remove(temp_path, cleanup_ec);
          assign_comment_error(result,
                               make_archive_error(ArchiveErrorDomain::kCanceled,
                                                  "Operation canceled",
                                                  255));
          return result;
        }

        if (update_res != S_OK) {
          std::error_code cleanup_ec;
          fs::remove(temp_path, cleanup_ec);
          assign_comment_error(result, map_comment_set_property_hresult(update_res));
          return result;
        }

        // Release archive handles before replacing the original archive file.
        out_archive.Release();
        open_state.archive_link.Release();

        const AtomicReplaceResult replace_result = replace_file_atomically(
            temp_path,
            archive_path,
            ".z7-comment-backup-");
        if (!replace_result.success) {
          assign_comment_error(result,
                               replace_result.error.value_or(
                                   make_operation_failure<OperationResult>(
                                       ArchiveErrorDomain::kIo,
                                       "Failed to replace archive file",
                                       2))
                                   .error);
          return result;
        }

        result = make_operation_success<ArchiveCommentResult>("Comment updated");
        return result;
      });
}

FilesystemCommentResult NativeArchiveBackend::comment_filesystem(
    const FilesystemCommentRequest& request,
    const ArchiveBackendHooks&) {
  DescriptIonDocument document;
  std::string error_message;
  if (!load_descript_ion_document(request.directory_path, &document, &error_message)) {
    return make_operation_failure<FilesystemCommentResult>(
        ArchiveErrorDomain::kIo,
        error_message.empty() ? "Failed to read descript.ion" : error_message,
        2);
  }
  if (!update_descript_ion_entry(&document, request.entry_name, request.comment)) {
    return make_operation_failure<FilesystemCommentResult>(
        ArchiveErrorDomain::kInvalidArguments,
        "Filesystem comment request entry_name is invalid",
        7);
  }
  if (!save_descript_ion_document(request.directory_path, document, &error_message)) {
    return make_operation_failure<FilesystemCommentResult>(
        ArchiveErrorDomain::kIo,
        error_message.empty() ? "Failed to write descript.ion" : error_message,
        2);
  }
  return make_operation_success<FilesystemCommentResult>("Comment updated");
}

}  // namespace z7::app
