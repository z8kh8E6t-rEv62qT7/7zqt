// src/archive_application/src/native_7z/operations/operations_test_hash_delete.cpp
// Role: Native backend test/hash/delete operations.

#include <algorithm>
#include <optional>

#include "core/internal.h"
#include "session/session_registry_internal.h"
#include "third_party_adapter/callbacks_extract.h"
#include "third_party_adapter/callbacks_update.h"
#include "third_party_adapter/third_party_adapter.h"

namespace z7::app {

    namespace {

        void merge_test_hash_summary(HashSummary* merged, HashSummary const& step) {
            if (merged == nullptr) {
                return;
            }

            merged->num_archives += std::max<uint64_t>(1, step.num_archives);
            merged->num_dirs += step.num_dirs;
            merged->num_files += step.num_files;
            merged->files_size += step.files_size;
            merged->num_errors += step.num_errors;

            if (step.physical_size_defined) {
                merged->physical_size_defined = true;
                merged->physical_size += step.physical_size;
            }

            if (merged->main_name.empty() && !step.main_name.empty()) {
                merged->main_name = step.main_name;
            }
            if (merged->first_file_name.empty() && !step.first_file_name.empty()) {
                merged->first_file_name = step.first_file_name;
            }
        }

        uint64_t filesystem_input_size(std::string const& path) {
            std::error_code ec;
            fs::path const fs_path(path);
            if (!fs::is_regular_file(fs_path, ec)) {
                return 0;
            }
            uintmax_t const size = fs::file_size(fs_path, ec);
            if (ec) {
                return 0;
            }
            return static_cast<uint64_t>(size);
        }

        void append_directory_test_inputs(fs::path const& root, std::vector<std::string>* out) {
            if (out == nullptr) {
                return;
            }

            std::vector<std::string> entries;
            std::error_code ec;
            fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec);
            fs::recursive_directory_iterator const end;
            while (!ec && it != end) {
                fs::directory_entry const entry = *it;
                std::error_code status_ec;
                if (entry.is_regular_file(status_ec)) {
                    entries.push_back(entry.path().string());
                }
                it.increment(ec);
            }

            std::sort(entries.begin(), entries.end());
            out->insert(out->end(), entries.begin(), entries.end());
        }

        std::vector<std::string> expand_filesystem_test_inputs(std::vector<std::string> const& inputs) {
            std::vector<std::string> expanded;
            for (std::string const& input : inputs) {
                if (input.empty()) {
                    continue;
                }

                std::error_code ec;
                fs::path const path(input);
                if (fs::is_directory(path, ec)) {
                    append_directory_test_inputs(path, &expanded);
                    continue;
                }
                expanded.push_back(input);
            }
            return expanded;
        }

        HRESULT collect_archive_hash_entries(CArc const* arc,
                                             UInt32 num_items,
                                             std::vector<std::string> const& requested_entries,
                                             std::string* single_selected_entry,
                                             std::vector<HashInputEntry>& entries) {
            std::unordered_set<std::string> selected_entries;
            selected_entries.reserve(requested_entries.size());
            std::vector<std::string> normalized_request_entries;
            normalized_request_entries.reserve(requested_entries.size());
            for (std::string const& entry : requested_entries) {
                std::string const normalized = normalize_archive_item_path(entry);
                if (!normalized.empty() && selected_entries.insert(normalized).second) {
                    normalized_request_entries.push_back(normalized);
                }
            }
            if (single_selected_entry != nullptr) {
                *single_selected_entry =
                    normalized_request_entries.size() == 1 ? normalized_request_entries.front() : std::string();
            }

            entries.clear();
            entries.reserve(static_cast<size_t>(num_items));
            for (UInt32 i = 0; i < num_items; ++i) {
                ArchiveItemPath item_path;
                HRESULT const path_result = resolve_archive_item_path(arc, i, item_path);
                if (path_result != S_OK) {
                    return path_result;
                }
                bool const matches = !item_path.normalized.empty()
                                  && archive_path_matches_selection(item_path.normalized, selected_entries);
                if (!matches) {
                    continue;
                }

                bool is_dir = false;
                (void)archive_get_prop_bool(arc->Archive, i, kpidIsDir, is_dir);
                uint64_t size = 0;
                if (!is_dir) {
                    (void)archive_get_prop_uint64(arc->Archive, i, kpidSize, size);
                }

                HashInputEntry entry;
                entry.relative_path = item_path.normalized;
                entry.is_dir = is_dir;
                entry.file_size = size;
                entry.archive_index = i;
                entries.push_back(std::move(entry));
            }
            return S_OK;
        }

        TestResult run_test_on_arc(CArc const* arc,
                                   TestRequest const& request,
                                   ArchiveBackendHooks const& hooks,
                                   std::atomic<bool>& cancel_requested,
                                   std::function<bool()> const& wait_while_paused,
                                   std::string const& archive_display_path,
                                   UInt32 num_items,
                                   OpenArchiveDiagnostics const* open_diagnostics,
                                   bool open_diagnostic_already_published) {
            ReadOperationOpenDiagnosticState const open_diagnostic_state =
                publish_read_operation_open_diagnostics(
                    hooks,
                    open_diagnostics,
                    open_diagnostic_already_published);
            std::unordered_set<std::string> selected_entries;
            selected_entries.reserve(request.entries.size());
            std::vector<std::string> normalized_request_entries;
            normalized_request_entries.reserve(request.entries.size());
            for (std::string const& entry : request.entries) {
                std::string const normalized = normalize_archive_item_path(entry);
                if (!normalized.empty() && selected_entries.insert(normalized).second) {
                    normalized_request_entries.push_back(normalized);
                }
            }

            TestArchiveItemStats item_stats;
            std::vector<UInt32> selected_indices;
            selected_indices.reserve(static_cast<size_t>(num_items));
            std::string first_matched_item_path;
            if (selected_entries.empty()) {
                item_stats = collect_test_archive_item_stats(arc->Archive, num_items);
            } else {
                for (UInt32 i = 0; i < num_items; ++i) {
                    ArchiveItemPath item_path;
                    HRESULT const path_result = resolve_archive_item_path(arc, i, item_path);
                    if (path_result != S_OK) {
                        return from_base_result<TestResult>(
                            make_operation_failure_from_hresult<OperationResult>(path_result));
                    }
                    if (!archive_path_matches_selection(item_path.normalized, selected_entries)) {
                        continue;
                    }
                    if (first_matched_item_path.empty()) {
                        first_matched_item_path = item_path.normalized;
                    }
                    selected_indices.push_back(i);
                    accumulate_test_item_stats(arc->Archive, i, item_stats);
                }
            }

            uint64_t const selected_total_files = selected_entries.empty()
                                                    ? static_cast<uint64_t>(num_items)
                                                    : static_cast<uint64_t>(selected_indices.size());

            HashSummary test_summary;
            test_summary.num_archives = 1;
            test_summary.num_dirs = item_stats.num_dirs;
            test_summary.num_files = item_stats.num_files;
            test_summary.files_size = item_stats.total_unpacked_size;
            test_summary.num_errors = open_diagnostics != nullptr ? open_diagnostics->error_count : 0;
            test_summary.physical_size_defined = arc->PhySize_Defined;
            test_summary.physical_size = arc->PhySize_Defined ? arc->PhySize : 0;
            if (!normalized_request_entries.empty()) {
                if (normalized_request_entries.size() == 1) {
                    test_summary.main_name = normalized_request_entries.front();
                }
                if (item_stats.num_files == 1 && item_stats.num_dirs == 0) {
                    if (!first_matched_item_path.empty()) {
                        test_summary.first_file_name = first_matched_item_path;
                    } else if (normalized_request_entries.size() == 1) {
                        test_summary.first_file_name = normalized_request_entries.front();
                    }
                }
            }

            emit_log_event(hooks, OperationStage::kRunning, OutputChannel::kNone, "Archives: 1");
            if (arc->PhySize_Defined) {
                emit_log_event(hooks,
                               OperationStage::kRunning,
                               OutputChannel::kNone,
                               "Physical Size = " + std::to_string(arc->PhySize));
            }
            emit_log_event(hooks,
                           OperationStage::kRunning,
                           OutputChannel::kNone,
                           "Folders: " + std::to_string(item_stats.num_dirs));
            emit_log_event(hooks,
                           OperationStage::kRunning,
                           OutputChannel::kNone,
                           "Files: " + std::to_string(item_stats.num_files));
            emit_log_event(hooks,
                           OperationStage::kRunning,
                           OutputChannel::kNone,
                           "Size = " + std::to_string(item_stats.total_unpacked_size));

            emit_progress_event(hooks,
                                OperationStage::kRunning,
                                -1,
                                arc->PhySize_Defined,
                                arc->PhySize_Defined ? arc->PhySize : 0,
                                0,
                                selected_total_files,
                                0,
                                open_diagnostic_state.progress_error_count,
                                {},
                                {});

            if (!selected_entries.empty() && selected_indices.empty()) {
                TestResult out = make_operation_success<TestResult>("There are no errors");
                out.hash_summary = test_summary;
                return out;
            }

            auto* callback = new NativeTestExtractCallback(arc,
                                                           hooks,
                                                           &cancel_requested,
                                                           wait_while_paused,
                                                           archive_display_path,
                                                           selected_total_files,
                                                           request.configured_memory_limit_bytes,
                                                           request.configured_memory_limit_defined,
                                                           {},
                                                           open_diagnostic_state.progress_error_count,
                                                           open_diagnostic_state.archive_context_reported);
            UInt32 const* indices = nullptr;
            UInt32 num_indices = static_cast<UInt32>(-1);
            if (!selected_entries.empty()) {
                indices = selected_indices.data();
                num_indices = static_cast<UInt32>(selected_indices.size());
            }
            ExtractInvocationStatus const status =
                invoke_archive_extract_with_callback(arc->Archive, indices, num_indices, true, callback);

            return finalize_extract_operation_result<TestResult>(
                hooks,
                cancel_requested,
                selected_total_files,
                status,
                [&](ExtractInvocationStatus const& done) {
                    TestResult out = done.diagnostic.empty()
                                       ? make_operation_partial_success<TestResult>()
                                       : make_operation_partial_success<TestResult>(done.diagnostic);
                    test_summary.num_errors = done.error_count
                                            + (open_diagnostics != nullptr ? open_diagnostics->error_count : 0);
                    out.hash_summary = test_summary;
                    return out;
                },
                [&](ExtractInvocationStatus const&) {
                    TestResult out = make_operation_success<TestResult>("There are no errors");
                    out.hash_summary = test_summary;
                    return out;
                },
                "Password required or incorrect",
                open_diagnostics);
        }

    } // namespace

    TestResult NativeArchiveBackend::test(TestRequest const& request, ArchiveBackendHooks const& hooks) {
        if (!request.archive_paths.empty()) {
            std::vector<std::string> const archive_inputs = expand_filesystem_test_inputs(request.archive_paths);
            TestResult merged;
            std::optional<TestResult> first_failure;
            HashSummary merged_summary;
            bool has_summary = false;
            bool has_any = false;
            uint64_t cumulative_error_count = 0;
            uint64_t display_completed_files = 0;
            uint64_t display_completed_bytes = 0;
            uint64_t display_total_bytes = 0;
            for (std::string const& archive : archive_inputs) {
                display_total_bytes += filesystem_input_size(archive);
            }
            auto emit_batch_snapshot = [&](std::string const& current_path) {
                emit_progress_event(hooks,
                                    OperationStage::kRunning,
                                    archive_inputs.empty()
                                        ? -1
                                        : static_cast<int>((display_completed_files * 100)
                                                           / static_cast<uint64_t>(archive_inputs.size())),
                                    display_total_bytes != 0,
                                    display_total_bytes,
                                    display_completed_bytes,
                                    static_cast<uint64_t>(archive_inputs.size()),
                                    display_completed_files,
                                    cumulative_error_count,
                                    current_path,
                                    {});
            };
            for (std::string const& archive : archive_inputs) {
                if (archive.empty()) {
                    continue;
                }
                has_any = true;
                uint64_t const input_size = filesystem_input_size(archive);
                TestRequest single = request;
                single.archive_path = archive;
                single.archive_paths.clear();
                TestResult const step = test(single, hooks);
                ++display_completed_files;
                display_completed_bytes += input_size;
                if (!step.ok) {
                    if (is_operation_canceled(step.error)) {
                        return step;
                    }
                    uint64_t const step_error_count =
                        step.hash_summary.has_value() && step.hash_summary->num_errors != 0
                            ? step.hash_summary->num_errors
                            : 1;
                    cumulative_error_count += step_error_count;
                    if (step.hash_summary.has_value()) {
                        merge_test_hash_summary(&merged_summary, *step.hash_summary);
                        has_summary = true;
                    }
                    emit_batch_snapshot(archive);
                    if (!first_failure.has_value()) {
                        first_failure = step;
                    }
                    continue;
                }
                merged = step;
                if (step.hash_summary.has_value()) {
                    merge_test_hash_summary(&merged_summary, *step.hash_summary);
                    has_summary = true;
                }
                emit_batch_snapshot(archive);
            }
            if (!has_any) {
                return merged;
            }
            if (first_failure.has_value()) {
                if (has_summary || cumulative_error_count != 0) {
                    merged_summary.num_errors = std::max(merged_summary.num_errors, cumulative_error_count);
                    first_failure->hash_summary = merged_summary;
                }
                return *first_failure;
            }
            if (has_summary) {
                merged.hash_summary = merged_summary;
            }
            return merged;
        }

        if (request.session_token.has_value() && request.session_token->is_valid()) {
            auto session = ArchiveSessionRegistry::instance().find(*request.session_token);
            if (!session) {
                return make_operation_failure<TestResult>(
                    ArchiveErrorDomain::kInvalidArguments, "Unknown archive session token", 7);
            }
            std::unique_lock<std::recursive_mutex> session_lock(
                ArchiveOpenSessionNativeAccess::operation_mutex(*session));
            ScopedFilenameCodePage filename_scope(session->filename_code_page());
            if (ArchiveOpenSessionNativeAccess::closed(*session)) {
                return make_operation_failure<TestResult>(
                    ArchiveErrorDomain::kInvalidArguments, "Archive session is already closed", 7);
            }
            CArc const* arc = archive_session_link(*session).GetArc();
            UInt32 num_items = 0;
            const HRESULT num_items_hr = arc->Archive->GetNumberOfItems(&num_items);
            if (num_items_hr != S_OK) {
                return from_base_result<TestResult>(make_operation_failure_from_hresult<OperationResult>(num_items_hr));
            }
            std::string const archive_selection_path =
                ArchiveOpenSessionNativeAccess::source_archive_path(*session).empty()
                    ? session->display_path()
                    : ArchiveOpenSessionNativeAccess::source_archive_path(*session);
            TestResult result = run_test_on_arc(
                arc,
                request,
                hooks,
                cancel_requested_,
                [this]() { return this->wait_while_paused(); },
                archive_selection_path,
                num_items,
                &archive_session_state(*session).open_diagnostics,
                false);
            apply_open_archive_diagnostics(result, archive_session_state(*session).open_diagnostics);
            return result;
        }

        return run_open_archive_read_pipeline<TestResult>(
            request.archive_path,
            {},
            hooks,
            OpenResultMessagePolicy::kReadOperationMessages,
            true,
            {},
            [&](OpenArchiveReadState const& open_state, UInt32 num_items) -> TestResult {
                return run_test_on_arc(
                    open_state.arc,
                    request,
                    hooks,
                    cancel_requested_,
                    [this]() { return this->wait_while_paused(); },
                    request.archive_path,
                    num_items,
                    &open_state.open_diagnostics,
                    true);
            });
    }

    // Delete/hash operations share the same unit as test flow.

    HashResult NativeArchiveBackend::hash(HashRequest const& request, ArchiveBackendHooks const& hooks) {
        if (request.session_token.has_value() && request.session_token->is_valid()) {
            emit_hash_progress(hooks, "Scanning", false, 0, 0, 0, 0, 0, {});

            auto session = ArchiveSessionRegistry::instance().find(*request.session_token);
            if (!session) {
                return make_operation_failure<HashResult>(
                    ArchiveErrorDomain::kInvalidArguments, "Unknown archive session token", 7);
            }
            std::unique_lock<std::recursive_mutex> session_lock(
                ArchiveOpenSessionNativeAccess::operation_mutex(*session));
            ScopedFilenameCodePage filename_scope(session->filename_code_page());
            if (ArchiveOpenSessionNativeAccess::closed(*session)) {
                return make_operation_failure<HashResult>(
                    ArchiveErrorDomain::kInvalidArguments, "Archive session is already closed", 7);
            }
            CArc const* arc = archive_session_link(*session).GetArc();
            if (arc == nullptr || arc->Archive == nullptr) {
                return make_operation_failure<HashResult>(
                    ArchiveErrorDomain::kInvalidArguments, "Session archive unavailable", 7);
            }
            ReadOperationOpenDiagnosticState const open_diagnostic_state =
                publish_read_operation_open_diagnostics(
                    hooks,
                    &archive_session_state(*session).open_diagnostics,
                    false);
            UInt32 num_items = 0;
            const HRESULT num_items_hr = arc->Archive->GetNumberOfItems(&num_items);
            if (num_items_hr != S_OK) {
                return from_base_result<HashResult>(make_operation_failure_from_hresult<OperationResult>(num_items_hr));
            }

            std::string single_selected_entry;
            std::vector<HashInputEntry> hash_entries;
            HRESULT const collect_result =
                collect_archive_hash_entries(arc, num_items, request.entries, &single_selected_entry, hash_entries);
            if (collect_result != S_OK) {
                return from_base_result<HashResult>(
                    make_operation_failure_from_hresult<OperationResult>(collect_result));
            }
            if (hash_entries.empty() && !request.entries.empty()) {
                return make_operation_failure<HashResult>(
                    ArchiveErrorDomain::kInvalidArguments, "Hash request entries do not exist in archive", 7);
            }

            bool const has_files = std::any_of(
                hash_entries.begin(), hash_entries.end(), [](HashInputEntry const& entry) { return !entry.is_dir; });
            if (has_files) {
                ArchiveBackendHooks const session_hooks = make_session_password_hooks(*session, hooks);
                std::string const password = session->password_defined() ? session->password() : std::string();
                HashResult result = run_hash_archive_entries(request,
                                                             session_hooks,
                                                             arc,
                                                             hash_entries,
                                                             single_selected_entry,
                                                             session->display_path(),
                                                             password,
                                                             &archive_session_state(*session).open_diagnostics,
                                                             open_diagnostic_state);
                if (!result.ok && result.error.domain == ArchiveErrorDomain::kPassword) {
                    session->clear_password();
                }
                apply_open_archive_diagnostics(result, archive_session_state(*session).open_diagnostics);
                return result;
            }

            HashResult result = run_hash_entries(request,
                                                 hooks,
                                                 hash_entries,
                                                 single_selected_entry,
                                                 open_diagnostic_state.progress_error_count);
            apply_open_archive_diagnostics(result, archive_session_state(*session).open_diagnostics);
            return result;
        }
        return run_hash_internal(request, hooks);
    }

    DeleteResult NativeArchiveBackend::remove(DeleteRequest const& request, ArchiveBackendHooks const& hooks) {
        if (!request.filesystem_paths.empty()) {
            std::error_code ec;
            return run_filesystem_path_batch<DeleteResult>(
                request.filesystem_paths,
                hooks,
                cancel_requested_,
                [&](std::string const& path) -> std::optional<ArchiveError> {
                    bool const deleted = request.use_recycle_bin ? move_path_to_recycle_bin(fs::path(path), ec)
                                                                 : remove_path_any(fs::path(path), ec);
                    if (deleted) {
                        return std::nullopt;
                    }
                    return make_archive_error(ArchiveErrorDomain::kIo,
                                              ec ? ec.message()
                                                 : (request.use_recycle_bin
                                                        ? std::string("Failed to move path to recycle bin")
                                                        : std::string("Failed to delete filesystem path")),
                                              2);
                },
                "Delete completed",
                [](DeleteResult&, uint64_t) {});
        }

        if (request.session_token.has_value() && request.session_token->is_valid()) {
            auto session = ArchiveSessionRegistry::instance().find(*request.session_token);
            if (!session) {
                return make_operation_failure<DeleteResult>(
                    ArchiveErrorDomain::kInvalidArguments, "Unknown archive session token", 7);
            }
            std::unique_lock<std::recursive_mutex> session_lock(
                ArchiveOpenSessionNativeAccess::operation_mutex(*session));
            ScopedFilenameCodePage filename_scope(session->filename_code_page());
            if (ArchiveOpenSessionNativeAccess::closed(*session)) {
                return make_operation_failure<DeleteResult>(
                    ArchiveErrorDomain::kInvalidArguments, "Archive session is already closed", 7);
            }
            if (archive_session_state(*session).open_diagnostics.has_errors()) {
                return make_operation_failure_from_open_diagnostics<DeleteResult>(
                    archive_session_state(*session).open_diagnostics);
            }
            if (!request.password.empty()) {
                session->set_password(request.password);
            }
            if (std::optional<OperationResult> materialize_error = ensure_archive_session_writable(
                    *session, hooks, &cancel_requested_, [this]() { return this->wait_while_paused(); });
                materialize_error.has_value()) {
                return from_base_result<DeleteResult>(std::move(*materialize_error));
            }

            ArchiveOpenSessionState const& state = archive_session_state(*session);
            if (state.temp_file == nullptr || state.temp_file->empty()) {
                return make_operation_failure<DeleteResult>(
                    ArchiveErrorDomain::kIo, "Writable archive session does not have a backing file", 2);
            }

            SessionMutationBackup mutation_backup;
            if (std::optional<OperationResult> backup_error =
                    create_archive_session_mutation_backup(*session, &mutation_backup);
                backup_error.has_value()) {
                return from_base_result<DeleteResult>(std::move(*backup_error));
            }

            DeleteRequest writable_request = request;
            writable_request.session_token.reset();
            writable_request.archive_path = state.temp_file->string();
            if (session->password_defined()) {
                writable_request.password = session->password();
            }

            DeleteResult delete_result = remove(writable_request, hooks);
            if (!delete_result.ok) {
                if (std::optional<OperationResult> restore_error = restore_archive_session_mutation_backup(
                        *session, mutation_backup, hooks, nullptr, []() { return true; });
                    restore_error.has_value()) {
                    return from_base_result<DeleteResult>(std::move(*restore_error));
                }
                return delete_result;
            }
            if (std::optional<OperationResult> refresh_error = refresh_archive_session_from_backing_file(
                    *session, hooks, &cancel_requested_, [this]() { return this->wait_while_paused(); });
                refresh_error.has_value()) {
                if (std::optional<OperationResult> restore_error = restore_archive_session_mutation_backup(
                        *session, mutation_backup, hooks, nullptr, []() { return true; });
                    restore_error.has_value()) {
                    return from_base_result<DeleteResult>(std::move(*restore_error));
                }
                return from_base_result<DeleteResult>(std::move(*refresh_error));
            }
            ArchiveOpenSessionNativeAccess::set_dirty(*session, true);
            ArchiveOpenSessionNativeAccess::increment_generation(*session);
            if (std::optional<OperationResult> cleanup_error = discard_archive_session_mutation_backup(mutation_backup);
                cleanup_error.has_value()) {
                emit_log_event(hooks, OperationStage::kRunning, OutputChannel::kStdErr, cleanup_error->error.message);
            }
            return delete_result;
        }

        DeleteResult preflight = run_open_archive_read_pipeline<DeleteResult>(
            request.archive_path,
            {},
            hooks,
            OpenResultMessagePolicy::kOperationMessages,
            true,
            request.password,
            [](OpenArchiveReadState const&, UInt32) {
                return make_operation_success<DeleteResult>("Archive is safe to update");
            });
        if (!preflight.ok) {
            return preflight;
        }

        return run_update_operation_with_mode<DeleteResult>(
            request.archive_path,
            hooks,
            static_cast<uint64_t>(request.entries.size()),
            [&]() {
                return NativeUpdateOperationCallback(
                    hooks,
                    &cancel_requested_,
                    [this]() { return this->wait_while_paused(); },
                    request.archive_path,
                    NativeUpdateOperationCallback::Mode::kDelete,
                    OpenResultMessagePolicy::kOperationMessages,
                    request.password,
                    /*reject_open_errors=*/true);
            },
            [&](CCodecs&, CObjectVector<COpenType>&, NWildcard::CCensor& censor, CUpdateOptions& options)
                -> std::optional<OperationResult> {
                for (std::string const& entry : request.entries) {
                    std::string const normalized = normalize_archive_item_path(entry);
                    if (normalized.empty()) {
                        continue;
                    }
                    censor.AddPreItem_NoWildcard(utf8_to_ustring(normalized));
                }

                options.Commands.Clear();
                CUpdateArchiveCommand command;
                command.ActionSet = NUpdateArchive::k_ActionSet_Delete;
                options.Commands.Add(command);
                options.ArcNameMode = k_ArcNameMode_Exact;
                return std::nullopt;
            });
    }

} // namespace z7::app
