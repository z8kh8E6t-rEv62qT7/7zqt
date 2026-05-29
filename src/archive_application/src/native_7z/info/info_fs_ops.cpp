// src/archive_application/src/native_7z/info/info_fs_ops.cpp
// Role: Open/list/navigate and filesystem-level backend operations.

#include "core/internal.h"
#include "core/filesystem_replace.h"
#include "third_party_adapter/third_party_adapter.h"
#include "third_party_adapter/callbacks_update.h"
#include "third_party_adapter/info_properties_detail.h"
#include "session/session_registry_internal.h"

namespace z7::app {
namespace {

bool rename_request_targets_archive(const RenameRequest& request) {
  return (request.session_token.has_value() && request.session_token->is_valid()) ||
         !request.archive_path.empty() || !request.entry_path.empty();
}

std::string rename_entry_parent(const std::string& entry_path) {
  const size_t slash = entry_path.rfind('/');
  if (slash == std::string::npos) {
    return {};
  }
  return entry_path.substr(0, slash);
}

std::string rename_entry_target_path(const std::string& entry_path,
                                     const std::string& new_name) {
  const std::string parent = rename_entry_parent(entry_path);
  if (parent.empty()) {
    return new_name;
  }
  return parent + "/" + new_name;
}

bool archive_path_is_child_of(const std::string& path,
                              const std::string& parent) {
  return path.size() > parent.size() &&
         path.compare(0, parent.size(), parent) == 0 &&
         path[parent.size()] == '/';
}

bool archive_rename_item_matches(const std::string& item_path,
                                 const std::string& old_path,
                                 bool old_path_is_dir) {
  return item_path == old_path ||
         (old_path_is_dir && archive_path_is_child_of(item_path, old_path));
}

std::string renamed_archive_item_path(const std::string& item_path,
                                      const std::string& old_path,
                                      const std::string& new_path) {
  if (item_path == old_path) {
    return new_path;
  }
  return new_path + item_path.substr(old_path.size());
}

bool archive_rename_conflicts_with_unselected_item(const std::string& item_path,
                                                   const std::string& new_path,
                                                   bool new_path_is_dir) {
  return item_path == new_path ||
         (new_path_is_dir && archive_path_is_child_of(item_path, new_path));
}

fs::path make_rename_temp_path(const fs::path& archive_path,
                               std::error_code& ec) {
  ec.clear();
  const std::string stem = archive_path.filename().string();
  const uint64_t base =
      static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
  for (uint64_t i = 0; i < 64; ++i) {
    fs::path candidate = archive_path.parent_path() /
                         fs::path(stem + ".z7_rename_tmp_" + std::to_string(base + i));
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

ArchiveError map_archive_rename_hresult(HRESULT hr) {
  if (hr == E_NOTIMPL || hr == E_NOINTERFACE) {
    return make_archive_error(ArchiveErrorDomain::kUnsupportedFormat,
                              "Rename operation is unsupported for this archive format",
                              2);
  }
  return map_hresult_to_archive_error(hr);
}

std::optional<int64_t> filesystem_time_msecs_utc(const fs::path& path) {
  std::error_code ec;
  const fs::file_time_type file_time = fs::last_write_time(path, ec);
  if (ec) {
    return std::nullopt;
  }
  const auto system_time =
      std::chrono::time_point_cast<std::chrono::system_clock::duration>(
          file_time - fs::file_time_type::clock::now() +
          std::chrono::system_clock::now());
  return static_cast<int64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          system_time.time_since_epoch())
          .count());
}

void fill_overwrite_prompt_file_info(const fs::path& path,
                                     bool existing,
                                     OverwritePrompt& prompt) {
  std::error_code ec;
  const uint64_t size = fs::file_size(path, ec);
  if (!ec) {
    if (existing) {
      prompt.existing_size_defined = true;
      prompt.existing_size = size;
    } else {
      prompt.incoming_size_defined = true;
      prompt.incoming_size = size;
    }
  }

  if (existing) {
    prompt.existing_mtime_msecs_utc = filesystem_time_msecs_utc(path);
  } else {
    prompt.incoming_mtime_msecs_utc = filesystem_time_msecs_utc(path);
  }
}

OverwritePrompt filesystem_overwrite_prompt(const fs::path& source,
                                            const fs::path& destination) {
  OverwritePrompt prompt;
  prompt.existing_path = destination.generic_string();
  prompt.incoming_path = source.generic_string();
  fill_overwrite_prompt_file_info(destination, true, prompt);
  fill_overwrite_prompt_file_info(source, false, prompt);
  return prompt;
}

ArchiveError filesystem_io_error(const std::error_code& ec,
                                 const std::string& fallback) {
  return make_archive_error(ArchiveErrorDomain::kIo,
                            ec ? ec.message() : fallback,
                            2);
}

enum class FilesystemTransferStatus {
  kTransferred,
  kSkipped,
  kCanceled,
  kFailed
};

struct FilesystemTransferResult {
  FilesystemTransferStatus status = FilesystemTransferStatus::kFailed;
  ArchiveError error;
};

struct FilesystemOverwriteState {
  bool yes_to_all = false;
  bool no_to_all = false;
};

FilesystemTransferResult make_transfer_status(
    FilesystemTransferStatus status) {
  FilesystemTransferResult result;
  result.status = status;
  return result;
}

FilesystemTransferResult make_transfer_error(const ArchiveError& error) {
  FilesystemTransferResult result;
  result.status = FilesystemTransferStatus::kFailed;
  result.error = error;
  return result;
}

FilesystemTransferResult copy_path_with_overwrite_mode(
    const fs::path& source,
    const fs::path& destination,
    OverwriteMode mode,
    const ArchiveBackendHooks& hooks,
    FilesystemOverwriteState& overwrite_state) {
  std::error_code ec;
  if (!fs::exists(source, ec)) {
    return make_transfer_error(
        filesystem_io_error(ec, "Source path does not exist"));
  }

  fs::path final_destination = destination;
  fs::path renamed_existing_backup;
  const bool destination_exists = fs::exists(destination, ec);
  if (ec) {
    return make_transfer_error(
        filesystem_io_error(ec, "Cannot query destination path"));
  }

  if (destination_exists) {
    bool overwrite_destination = false;
    switch (mode) {
      case OverwriteMode::kSkip:
        return make_transfer_status(FilesystemTransferStatus::kSkipped);
      case OverwriteMode::kOverwrite:
        overwrite_destination = true;
        break;
      case OverwriteMode::kRenameExtracted: {
        std::error_code unique_ec;
        final_destination = make_unique_destination_path(destination, unique_ec);
        if (unique_ec) {
          return make_transfer_error(
              filesystem_io_error(unique_ec, "Cannot allocate renamed destination path"));
        }
        break;
      }
      case OverwriteMode::kRenameExisting: {
        std::error_code unique_ec;
        const fs::path renamed_existing =
            make_unique_destination_path(destination, unique_ec);
        if (unique_ec) {
          return make_transfer_error(
              filesystem_io_error(unique_ec, "Cannot allocate renamed destination path"));
        }
        if (renamed_existing.empty()) {
          return make_transfer_error(make_archive_error(
              ArchiveErrorDomain::kIo,
              "Cannot allocate renamed destination path",
              2));
        }
        fs::rename(destination, renamed_existing, ec);
        if (ec) {
          return make_transfer_error(
              filesystem_io_error(ec, "Cannot rename existing destination"));
        }
        renamed_existing_backup = renamed_existing;
        break;
      }
      case OverwriteMode::kAsk: {
        if (overwrite_state.yes_to_all) {
          overwrite_destination = true;
          break;
        }
        if (overwrite_state.no_to_all) {
          return make_transfer_status(FilesystemTransferStatus::kSkipped);
        }
        if (!hooks.ask_overwrite) {
          return make_transfer_error(make_archive_error(
              ArchiveErrorDomain::kInvalidArguments,
              "request_overwrite interaction is required for overwrite Ask mode",
              7));
        }

        const OverwriteDecision decision =
            hooks.ask_overwrite(
                filesystem_overwrite_prompt(source, destination));
        switch (decision) {
          case OverwriteDecision::kYes:
            overwrite_destination = true;
            break;
          case OverwriteDecision::kYesToAll:
            overwrite_state.yes_to_all = true;
            overwrite_destination = true;
            break;
          case OverwriteDecision::kNo:
            return make_transfer_status(FilesystemTransferStatus::kSkipped);
          case OverwriteDecision::kNoToAll:
            overwrite_state.no_to_all = true;
            return make_transfer_status(FilesystemTransferStatus::kSkipped);
          case OverwriteDecision::kAutoRename: {
            std::error_code unique_ec;
            final_destination = make_unique_destination_path(destination, unique_ec);
            if (unique_ec) {
              return make_transfer_error(
                  filesystem_io_error(unique_ec, "Cannot allocate renamed destination path"));
            }
            break;
          }
          case OverwriteDecision::kCancel:
            return make_transfer_status(FilesystemTransferStatus::kCanceled);
        }
        break;
      }
    }

    if (final_destination.empty()) {
      return make_transfer_error(make_archive_error(
          ArchiveErrorDomain::kIo,
          "Cannot allocate destination path",
          2));
    }

    if (overwrite_destination &&
        !remove_path_any(final_destination, ec)) {
      return make_transfer_error(
          filesystem_io_error(ec, "Cannot remove existing destination"));
    }
  }

  if (!copy_path_any(source, final_destination, false, ec)) {
    if (!renamed_existing_backup.empty()) {
      std::error_code restore_ec;
      if (!fs::exists(final_destination, restore_ec) || restore_ec) {
        restore_ec.clear();
        fs::rename(renamed_existing_backup, destination, restore_ec);
      }
    }
    return make_transfer_error(
        filesystem_io_error(ec, "Failed to copy path"));
  }
  return make_transfer_status(FilesystemTransferStatus::kTransferred);
}

template <typename TResult, typename OnTransferred>
TResult run_filesystem_transfer_batch(
    const std::vector<std::string>& source_paths,
    const ArchiveBackendHooks& hooks,
    std::atomic<bool>& cancel_requested,
    const std::string& destination_dir,
    const std::string& destination_path,
    OverwriteMode overwrite_mode,
    bool move_mode,
    std::string success_summary,
    OnTransferred&& on_transferred) {
  const uint64_t total = static_cast<uint64_t>(source_paths.size());
  uint64_t processed = 0;
  uint64_t transferred = 0;
  FilesystemOverwriteState overwrite_state;

  for (const std::string& source : source_paths) {
    if (cancel_requested.load()) {
      return make_operation_canceled<TResult>();
    }

    const fs::path src(source);
    const fs::path dst = destination_path.empty()
                             ? fs::path(destination_dir) / src.filename()
                             : fs::path(destination_path);
    const FilesystemTransferResult transfer =
        copy_path_with_overwrite_mode(src,
                                      dst,
                                      overwrite_mode,
                                      hooks,
                                      overwrite_state);
    switch (transfer.status) {
      case FilesystemTransferStatus::kTransferred: {
        if (move_mode) {
          std::error_code remove_ec;
          if (!remove_path_any(src, remove_ec)) {
            return make_operation_failure<TResult>(
                filesystem_io_error(remove_ec, "Failed to remove moved source"));
          }
        }
        ++transferred;
        break;
      }
      case FilesystemTransferStatus::kSkipped:
        break;
      case FilesystemTransferStatus::kCanceled:
        return make_operation_canceled<TResult>();
      case FilesystemTransferStatus::kFailed:
        return make_operation_failure<TResult>(transfer.error);
    }

    ++processed;
    emit_progress_event(hooks,
                        OperationStage::kRunning,
                        total == 0 ? -1 : static_cast<int>((processed * 100) / total),
                        true,
                        0,
                        0,
                        total,
                        processed,
                        0,
                        source,
                        {});
  }

  TResult result = make_operation_success<TResult>(std::move(success_summary));
  on_transferred(result, transferred);
  return result;
}

}  // namespace

OpenArchiveSessionResult NativeArchiveBackend::open_archive_from_path(
    const OpenArchiveFromPathRequest& request,
    const ArchiveBackendHooks& hooks) {
  return open_native_archive_session_from_path(
      ArchiveSessionRegistry::instance(),
      request,
      hooks,
      &cancel_requested_,
      [this]() { return wait_while_paused(); });
}

OpenArchiveSessionResult NativeArchiveBackend::open_archive_from_parent(
    const OpenArchiveFromParentRequest& request,
    const ArchiveBackendHooks& hooks) {
  return open_native_archive_session_from_parent(
      ArchiveSessionRegistry::instance(),
      request,
      hooks,
      &cancel_requested_,
      [this]() { return wait_while_paused(); });
}

OperationResult NativeArchiveBackend::close_archive_session(
    const CloseArchiveSessionRequest& request,
    const ArchiveBackendHooks& hooks) {
  return close_native_archive_session(
      ArchiveSessionRegistry::instance(),
      request.token,
      hooks,
      &cancel_requested_,
      [this]() { return wait_while_paused(); });
}

OpenArchiveResult NativeArchiveBackend::open_archive(const OpenArchiveRequest& request,
                                                     const ArchiveBackendHooks& hooks) {
  return run_open_archive_read_pipeline<OpenArchiveResult>(
      request.archive_path,
      request.archive_type_hint,
      hooks,
      true,
      [&](const OpenArchiveReadState&, UInt32) -> OpenArchiveResult {
        OpenArchiveResult out = make_operation_success<OpenArchiveResult>("Success");
        out.archive_path = request.archive_path;
        return out;
      });
}

ListResult NativeArchiveBackend::list(const ListRequest& request,
                                      const ArchiveBackendHooks& hooks) {
  const size_t batch_size =
      request.streaming_mode
          ? (request.batch_size_hint.value_or(256) > 0
                 ? request.batch_size_hint.value_or(256)
                 : 256)
          : 0;
  const auto& batch_cb = hooks.on_list_batch;

  // Token path: reuse an already-opened archive from the session registry.
  if (request.session_token.has_value() && request.session_token->is_valid()) {
    auto session = ArchiveSessionRegistry::instance().find(*request.session_token);
    if (!session) {
      return make_operation_failure<ListResult>(
          ArchiveErrorDomain::kInvalidArguments,
          "Unknown archive session token",
          7);
    }
    ListResult out;
    const CArc* arc = archive_session_link(*session).GetArc();
    const HRESULT hr = list_archive_entries_from_arc(arc,
                                                      request.directory,
                                                      request.recursive_dirs,
                                                      request.include_detailed_props,
                                                      &cancel_requested_,
                                                      out.entries,
                                                      batch_size,
                                                      batch_cb);
    if (hr != S_OK) {
      return from_base_result<ListResult>(
          make_operation_failure_from_hresult<OperationResult>(hr));
    }
    static_cast<OperationResult&>(out) =
        make_operation_success<OperationResult>(
            request.streaming_mode ? "batch-mode" : "Success");
    return out;
  }

  return run_with_operation_codecs_hresult<ListResult>(
      [&](CCodecs& codecs, ListResult& out) -> int {
        return list_archive_entries_via_original_api(
            request.archive_path,
            request.directory,
            request.archive_type_hint,
            request.recursive_dirs,
            request.include_detailed_props,
            hooks,
            &cancel_requested_,
            [this]() { return wait_while_paused(); },
            &codecs,
            out.entries,
            batch_size,
            batch_cb);
      },
      request.streaming_mode ? "batch-mode" : "Success");
}

ArchivePropertiesResult NativeArchiveBackend::properties(
    const ArchivePropertiesRequest& request,
    const ArchiveBackendHooks& hooks) {
  if (request.session_token.has_value() && request.session_token->is_valid()) {
    auto session = ArchiveSessionRegistry::instance().find(*request.session_token);
    if (!session) {
      return make_operation_failure<ArchivePropertiesResult>(
          ArchiveErrorDomain::kInvalidArguments,
          "Unknown archive session token",
          7);
    }
    ArchivePropertiesResult out;
    const CArc* arc = archive_session_link(*session).GetArc();
    const HRESULT hr = collect_archive_properties_from_open_state(request,
                                                                 *arc,
                                                                 archive_session_codecs(*session),
                                                                 archive_session_link(*session),
                                                                 &cancel_requested_,
                                                                 out.lines);
    if (hr != S_OK) {
      return from_base_result<ArchivePropertiesResult>(
          make_operation_failure_from_hresult<OperationResult>(hr));
    }

    uint32_t level_offset =
        static_cast<uint32_t>(archive_session_link(*session).Arcs.Size());
    std::shared_ptr<ArchiveOpenSession> current = ArchiveOpenSessionNativeAccess::parent(*session);
    std::string child_entry_path =
        ArchiveOpenSessionNativeAccess::entry_path_from_parent(*session);
    while (current) {
      const CArc* parent_arc = archive_session_link(*current).GetArc();
      if (parent_arc == nullptr) {
        break;
      }

      info_properties_detail::append_archive_props2_for_parent_entry(
          *parent_arc,
          child_entry_path,
          level_offset == 0 ? std::optional<uint32_t>{}
                            : std::optional<uint32_t>{level_offset},
          out.lines,
          &cancel_requested_);
      info_properties_detail::append_archive_link_properties_with_offset(
          archive_session_codecs(*current),
          archive_session_link(*current),
          level_offset,
          false,
          out.lines,
          &cancel_requested_);

      child_entry_path = ArchiveOpenSessionNativeAccess::entry_path_from_parent(*current);
      level_offset += static_cast<uint32_t>(archive_session_link(*current).Arcs.Size());
      current = ArchiveOpenSessionNativeAccess::parent(*current);
    }

    static_cast<OperationResult&>(out) =
        make_operation_success<OperationResult>("Success");
    return out;
  }

  return run_with_operation_codecs_hresult<ArchivePropertiesResult>(
      [&](CCodecs& codecs, ArchivePropertiesResult& out) -> int {
        return collect_archive_properties_via_original_api(
            request,
            hooks,
            &cancel_requested_,
            [this]() { return wait_while_paused(); },
            &codecs,
            out.lines);
      });
}

NavigateResult NativeArchiveBackend::navigate(const NavigateRequest& request,
                                              const ArchiveBackendHooks&) {
  NavigateResult result = make_operation_success<NavigateResult>("Navigation resolved");
  result.resolved_path = request.to_path;
  return result;
}

CopyResult NativeArchiveBackend::copy(const CopyRequest& request,
                                      const ArchiveBackendHooks& hooks) {
  return run_filesystem_transfer_batch<CopyResult>(
      request.source_paths,
      hooks,
      cancel_requested_,
      request.destination_dir,
      request.destination_path,
      request.overwrite_mode,
      false,
      "Copy completed",
      [](CopyResult& result, uint64_t completed) {
        result.copied_count = static_cast<size_t>(completed);
      });
}

MoveResult NativeArchiveBackend::move(const MoveRequest& request,
                                      const ArchiveBackendHooks& hooks) {
  return run_filesystem_transfer_batch<MoveResult>(
      request.source_paths,
      hooks,
      cancel_requested_,
      request.destination_dir,
      request.destination_path,
      request.overwrite_mode,
      true,
      "Move completed",
      [](MoveResult& result, uint64_t completed) {
        result.moved_count = static_cast<size_t>(completed);
      });
}

RenameResult NativeArchiveBackend::rename(const RenameRequest& request,
                                          const ArchiveBackendHooks& hooks) {
  if (rename_request_targets_archive(request)) {
    if (request.session_token.has_value() && request.session_token->is_valid()) {
      auto session = ArchiveSessionRegistry::instance().find(*request.session_token);
      if (!session) {
        return make_operation_failure<RenameResult>(
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
        return from_base_result<RenameResult>(std::move(*materialize_error));
      }

      const ArchiveOpenSessionState& state = archive_session_state(*session);
      if (state.temp_file == nullptr || state.temp_file->empty()) {
        return make_operation_failure<RenameResult>(
            ArchiveErrorDomain::kIo,
            "Writable archive session does not have a backing file",
            2);
      }

      RenameRequest writable_request = request;
      writable_request.session_token.reset();
      writable_request.archive_path = state.temp_file->string();

      RenameResult result = rename(writable_request, hooks);
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
        return from_base_result<RenameResult>(std::move(*refresh_error));
      }
      return result;
    }

    const std::string old_path =
        normalize_archive_virtual_directory(request.entry_path);
    const std::string new_path = rename_entry_target_path(old_path, request.new_name);
    const fs::path archive_path(request.archive_path);

    return run_open_archive_read_pipeline<RenameResult>(
        request.archive_path,
        {},
        hooks,
        true,
        [&](OpenArchiveReadState& open_state, UInt32 num_items) -> RenameResult {
          const CArc* arc = open_state.arc;
          CMyComPtr<IOutArchive> out_archive;
          const HRESULT query_out_res =
              arc->Archive->QueryInterface(IID_IOutArchive, (void**)&out_archive);
          if (query_out_res != S_OK || !out_archive) {
            return from_base_result<RenameResult>(
                make_operation_failure<OperationResult>(
                    map_archive_rename_hresult(query_out_res)));
          }

          std::vector<std::string> item_paths(static_cast<size_t>(num_items));
          std::vector<std::string> new_item_paths(static_cast<size_t>(num_items));
          std::vector<bool> renamed_items(static_cast<size_t>(num_items), false);
          std::unordered_set<std::string> requested_new_paths;
          bool matched_any = false;

          for (UInt32 i = 0; i < num_items; ++i) {
            std::string item_path = normalize_archive_item_path(
                archive_get_prop_text(arc->Archive, i, kpidPath));
            item_paths[static_cast<size_t>(i)] = item_path;
            if (item_path.empty() ||
                !archive_rename_item_matches(item_path,
                                             old_path,
                                             request.entry_is_dir)) {
              continue;
            }

            matched_any = true;
            renamed_items[static_cast<size_t>(i)] = true;
            std::string item_new_path =
                renamed_archive_item_path(item_path, old_path, new_path);
            if (!requested_new_paths.insert(item_new_path).second) {
              return make_operation_failure<RenameResult>(
                  ArchiveErrorDomain::kInvalidArguments,
                  "Rename request would create duplicate archive entries",
                  7);
            }
            new_item_paths[static_cast<size_t>(i)] = std::move(item_new_path);
          }

          if (!matched_any) {
            return make_operation_failure<RenameResult>(
                ArchiveErrorDomain::kInvalidArguments,
                "Rename request entry was not found in archive",
                7);
          }

          for (UInt32 i = 0; i < num_items; ++i) {
            const size_t index = static_cast<size_t>(i);
            if (renamed_items[index] || item_paths[index].empty()) {
              continue;
            }
            if (archive_rename_conflicts_with_unselected_item(
                    item_paths[index], new_path, request.entry_is_dir)) {
              return make_operation_failure<RenameResult>(
                  ArchiveErrorDomain::kInvalidArguments,
                  "Rename request destination already exists in archive",
                  7);
            }
          }

          CRecordVector<CUpdatePair2> update_pairs;
          update_pairs.Reserve(num_items);
          UStringVector new_names;
          for (UInt32 i = 0; i < num_items; ++i) {
            CUpdatePair2 pair;
            pair.SetAs_NoChangeArcItem(i);
            const size_t index = static_cast<size_t>(i);
            if (renamed_items[index]) {
              pair.NewProps = true;
              const HRESULT anti_res = arc->IsItem_Anti(i, pair.IsAnti);
              if (anti_res != S_OK) {
                return from_base_result<RenameResult>(
                    make_operation_failure<OperationResult>(
                        map_archive_rename_hresult(anti_res)));
              }
              pair.NewNameIndex = static_cast<int>(
                  new_names.Add(utf8_to_ustring(new_item_paths[index])));
              pair.IsMainRenameItem = item_paths[index] == old_path;
            }
            update_pairs.Add(pair);
          }

          CMyComPtr2_Create<IArchiveUpdateCallback, CArchiveUpdateCallback> update_callback;
          NativeUpdateOperationCallback update_operation_callback(
              hooks,
              &cancel_requested_,
              [this]() { return this->wait_while_paused(); },
              request.archive_path,
              NativeUpdateOperationCallback::Mode::kAdd);
          update_callback->Callback = &update_operation_callback;
          update_callback->UpdatePairs = &update_pairs;
          update_callback->NewNames = &new_names;
          update_callback->Arc = arc;
          update_callback->Archive = arc->Archive;
          update_callback->ArcFileName =
              utf8_to_ustring(archive_path.filename().string());

          std::error_code temp_path_ec;
          const fs::path temp_path = make_rename_temp_path(archive_path, temp_path_ec);
          if (temp_path.empty()) {
            std::string message = "Failed to allocate temporary archive output path";
            if (temp_path_ec) {
              message += ": " + temp_path_ec.message();
            }
            return make_operation_failure<RenameResult>(
                ArchiveErrorDomain::kIo,
                std::move(message),
                2);
          }
          COutFileStream* out_stream_spec = new COutFileStream();
          CMyComPtr<IOutStream> out_stream(out_stream_spec);
          if (!out_stream_spec->Create_ALWAYS(us2fs(utf8_to_ustring(temp_path.string())))) {
            return make_operation_failure<RenameResult>(
                ArchiveErrorDomain::kIo,
                "Failed to create temporary archive output",
                2);
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
              return from_base_result<RenameResult>(
                  make_operation_failure<OperationResult>(
                      map_archive_rename_hresult(seek_res)));
            }
            const HRESULT copy_res = NCompress::CopyStream_ExactSize(
                arc->InStream, out_stream, arc->ArcStreamOffset, nullptr);
            if (copy_res != S_OK) {
              out_stream_spec->Close();
              std::error_code cleanup_ec;
              fs::remove(temp_path, cleanup_ec);
              return from_base_result<RenameResult>(
                  make_operation_failure<OperationResult>(
                      map_archive_rename_hresult(copy_res)));
            }

            CTailOutStream* tail_stream_spec = new CTailOutStream;
            CMyComPtr<IOutStream> tail_stream(tail_stream_spec);
            tail_stream_spec->Stream = out_stream;
            tail_stream_spec->Offset = arc->ArcStreamOffset;
            tail_stream_spec->Init();
            update_stream = tail_stream;
          }

          HRESULT update_res = out_archive->UpdateItems(
              update_stream,
              static_cast<UInt32>(update_pairs.Size()),
              update_callback);
          const HRESULT close_res = out_stream_spec->Close();
          if (update_res == S_OK && close_res != S_OK) {
            update_res = close_res;
          }

          if (cancel_requested_.load()) {
            std::error_code cleanup_ec;
            fs::remove(temp_path, cleanup_ec);
            return make_operation_failure<RenameResult>(
                ArchiveErrorDomain::kCanceled,
                "Operation canceled",
                255);
          }

          if (update_res != S_OK) {
            std::error_code cleanup_ec;
            fs::remove(temp_path, cleanup_ec);
            return from_base_result<RenameResult>(
                make_operation_failure<OperationResult>(
                    map_archive_rename_hresult(update_res)));
          }

          out_archive.Release();
          open_state.archive_link.Release();

          const AtomicReplaceResult replace_result = replace_file_atomically(
              temp_path,
              archive_path,
              ".z7-rename-backup-");
          if (!replace_result.success) {
            return from_base_result<RenameResult>(
                make_operation_failure<OperationResult>(
                    replace_result.error.value_or(
                        make_operation_failure<OperationResult>(
                            ArchiveErrorDomain::kIo,
                            "Failed to replace archive file",
                            2))
                        .error));
          }

          RenameResult result = make_operation_success<RenameResult>("Rename completed");
          result.renamed_path = new_path;
          return result;
        });
  }

  const fs::path src(request.source_path);
  const fs::path dst = src.parent_path() / request.new_name;
  std::error_code ec;
  return run_filesystem_single_step<RenameResult>(
      hooks,
      cancel_requested_,
      request.source_path,
      [&]() -> std::optional<ArchiveError> {
        fs::rename(src, dst, ec);
        if (!ec) {
          return std::nullopt;
        }
        return make_archive_error(ArchiveErrorDomain::kIo, ec.message(), 2);
      },
      "Rename completed",
      [&](RenameResult& result) { result.renamed_path = dst.string(); });
}

CreateResult NativeArchiveBackend::create(const CreateRequest& request,
                                          const ArchiveBackendHooks& hooks) {
  const std::string created_path = (fs::path(request.parent_dir) / request.name).string();
  std::error_code ec;
  return run_filesystem_single_step<CreateResult>(
      hooks,
      cancel_requested_,
      created_path,
      [&]() -> std::optional<ArchiveError> {
        if (request.kind == CreateNodeKind::kDirectory) {
          ec.clear();
          if (fs::create_directories(created_path, ec)) {
            return std::nullopt;
          }
          if (!ec) {
            return make_archive_error(
                ArchiveErrorDomain::kIo,
                "Target already exists: " + created_path,
                2);
          }
          return make_archive_error(ArchiveErrorDomain::kIo, ec.message(), 2);
        }

        if (!ensure_parent_dir(created_path, ec)) {
          return make_archive_error(ArchiveErrorDomain::kIo, ec.message(), 2);
        }

        ec.clear();
        if (fs::exists(fs::path(created_path), ec)) {
          if (ec) {
            return make_archive_error(ArchiveErrorDomain::kIo, ec.message(), 2);
          }
          return make_archive_error(
              ArchiveErrorDomain::kIo,
              "Target already exists: " + created_path,
              2);
        }

        std::ofstream file(created_path, std::ios::binary | std::ios::out);
        if (!file) {
          return make_archive_error(ArchiveErrorDomain::kIo, "Failed to create file", 2);
        }
        file.close();
        if (!file) {
          return make_archive_error(ArchiveErrorDomain::kIo, "Failed to create file", 2);
        }
        return std::nullopt;
      },
      "Create completed",
      [&](CreateResult& result) { result.created_path = created_path; });
}

}  // namespace z7::app
