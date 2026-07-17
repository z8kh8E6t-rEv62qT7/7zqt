// src/archive_application/src/native_7z/session/session_registry_open_parent.cpp
// Role: Parent-session nested archive open flow and fallback strategies.

#include <algorithm>
#include <filesystem>
#include <limits>
#include <utility>

#include "core/internal.h"
#include "session/session_registry_internal.h"
#include "third_party_adapter/callbacks_extract_run.h"
#include "third_party_adapter/callbacks_extract_stream.h"

namespace z7::app {

    namespace {

        constexpr size_t kUnknownRamNestedBudget = 4u * 1024u * 1024u;
        constexpr size_t kNestedMemoryAllowance = 1u << 16;

        size_t compute_nested_open_budget(size_t depth) {
            size_t ram_size = static_cast<size_t>(sizeof(size_t)) << 29;
            if (!NWindows::NSystem::GetRamSize(ram_size) || ram_size == 0) {
                return kUnknownRamNestedBudget;
            }

            size_t const shift = std::max<size_t>(depth + 1, 8u);
            size_t const total_bits = sizeof(size_t) * 8u;
            return shift < total_bits ? ram_size >> shift : 0;
        }

        std::optional<uint64_t> declared_entry_size(CArc const& arc, UInt32 index) {
            NWindows::NCOM::CPropVariant value;
            if (arc.Archive->GetProperty(index, kpidSize, &value) != S_OK) {
                return std::nullopt;
            }
            UInt64 converted = 0;
            if (!ConvertPropVariantToUInt64(value, converted)) {
                return std::nullopt;
            }
            return static_cast<uint64_t>(converted);
        }

        size_t nested_memory_cap(std::optional<uint64_t> declared_size, size_t file_limit) {
            uint64_t const base = declared_size.value_or(static_cast<uint64_t>(file_limit));
            uint64_t const size_max = static_cast<uint64_t>(std::numeric_limits<size_t>::max());
            if (base >= size_max || base > size_max - kNestedMemoryAllowance) {
                return std::numeric_limits<size_t>::max();
            }
            return static_cast<size_t>(base + kNestedMemoryAllowance);
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

        ExtractInvocationStatus extract_entry_to_stream(ArchiveOpenSession& parent,
                                                        UInt32 entry_index,
                                                        std::string const& password,
                                                        ISequentialOutStream* output_stream,
                                                        ArchiveBackendHooks const& hooks,
                                                        std::atomic<bool>* cancel_requested,
                                                        std::function<bool()> wait_while_paused) {
            CArchiveLink& link = archive_session_link(parent);
            CArc const* arc = link.GetArc();
            if (arc == nullptr || arc->Archive == nullptr) {
                ExtractInvocationStatus status;
                status.hresult = E_FAIL;
                return status;
            }

            ArchiveBackendHooks const parent_hooks = make_session_password_hooks(parent, hooks);
            auto* callback = new NativeExtractCallback(arc,
                                                       std::filesystem::path{},
                                                       parent_hooks,
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
            callback->set_single_item_output_stream(output_stream);

            UInt32 const indices[1] = {entry_index};
            return invoke_archive_extract_with_callback(arc->Archive, indices, 1, /*test_mode=*/false, callback);
        }

        // Try to obtain a seekable IInStream for `entry_index` from the parent
        // archive via IInArchiveGetStream. Returns S_OK with populated holder on
        // success, S_FALSE if the parent format does not expose the interface /
        // stream, or an HRESULT on hard failure.
        HRESULT acquire_parent_sub_stream(ArchiveOpenSession& parent, UInt32 entry_index, ParentStreamRefHolder& out) {
            CArchiveLink& link = archive_session_link(parent);
            CArc const* arc = link.GetArc();
            if (arc == nullptr || arc->Archive == nullptr) {
                return E_FAIL;
            }

            CMyComPtr<IInArchiveGetStream> get_stream;
            HRESULT const query_get_stream =
                arc->Archive->QueryInterface(IID_IInArchiveGetStream, reinterpret_cast<void**>(&get_stream));
            if (query_get_stream != S_OK) {
                return query_get_stream == E_NOINTERFACE ? S_FALSE : query_get_stream;
            }
            if (!get_stream) {
                return E_FAIL;
            }

            CMyComPtr<ISequentialInStream> seq;
            const HRESULT get_res = get_stream->GetStream(entry_index, &seq);
            if (get_res != S_OK) {
                return get_res;
            }
            if (!seq) {
                return E_FAIL;
            }

            CMyComPtr<IInStream> seekable;
            HRESULT const query_seekable = seq.QueryInterface(IID_IInStream, &seekable);
            if (query_seekable != S_OK) {
                return query_seekable == E_NOINTERFACE ? S_FALSE : query_seekable;
            }
            if (!seekable) {
                return E_FAIL;
            }

            out.seq = std::move(seq);
            out.seekable = std::move(seekable);
            return S_OK;
        }

    } // namespace

    struct ArchiveExternalFileLease::State {
        std::filesystem::path directory;
        std::filesystem::path file;

        ~State() {
            std::error_code ec;
            std::filesystem::remove_all(directory, ec);
        }
    };

    ArchiveExternalFileLease::ArchiveExternalFileLease() = default;
    ArchiveExternalFileLease::~ArchiveExternalFileLease() = default;
    ArchiveExternalFileLease::ArchiveExternalFileLease(ArchiveExternalFileLease const&) = default;
    ArchiveExternalFileLease& ArchiveExternalFileLease::operator=(ArchiveExternalFileLease const&) = default;
    ArchiveExternalFileLease::ArchiveExternalFileLease(ArchiveExternalFileLease&&) noexcept = default;
    ArchiveExternalFileLease& ArchiveExternalFileLease::operator=(ArchiveExternalFileLease&&) noexcept = default;

    ArchiveExternalFileLease::ArchiveExternalFileLease(std::shared_ptr<State> state) : state_(std::move(state)) {}

    bool ArchiveExternalFileLease::valid() const {
        return state_ != nullptr && !state_->file.empty();
    }

    std::string ArchiveExternalFileLease::file_path() const {
        return valid() ? state_->file.generic_string() : std::string{};
    }

    struct ArchiveExternalFileLeaseAccess {
        static ArchiveExternalFileLease create(std::filesystem::path directory, std::filesystem::path file) {
            auto state = std::make_shared<ArchiveExternalFileLease::State>();
            state->directory = std::move(directory);
            state->file = std::move(file);
            return ArchiveExternalFileLease(std::move(state));
        }
    };

    OpenArchiveFromParentResult open_native_archive_session_from_parent(ArchiveSessionRegistry& registry,
                                                                        OpenArchiveFromParentRequest const& request,
                                                                        ArchiveBackendHooks const& hooks,
                                                                        std::atomic<bool>* cancel_requested,
                                                                        std::function<bool()> wait_while_paused) {
        OpenArchiveFromParentResult result;

        auto parent = registry.find(request.parent);
        if (!parent) {
            static_cast<OperationResult&>(result) = make_operation_failure<OperationResult>(
                ArchiveErrorDomain::kInvalidArguments, "Parent session not found", 7);
            return result;
        }

        std::vector<std::shared_ptr<ArchiveOpenSession>> parent_chain;
        for (std::shared_ptr<ArchiveOpenSession> cursor = parent; cursor != nullptr;
             cursor = ArchiveOpenSessionNativeAccess::parent(*cursor)) {
            parent_chain.push_back(cursor);
        }
        std::reverse(parent_chain.begin(), parent_chain.end());
        std::vector<std::unique_lock<std::recursive_mutex>> parent_locks;
        parent_locks.reserve(parent_chain.size());
        for (auto const& locked_session : parent_chain) {
            parent_locks.emplace_back(ArchiveOpenSessionNativeAccess::operation_mutex(*locked_session));
        }
        ScopedFilenameCodePage parent_filename_scope(parent->filename_code_page());

        // Resolve the explicit child selector before attempting any nested-open
        // strategy so underspecified requests fail fast and audibly.
        bool const has_entry_path = !request.entry_path.empty();
        bool const has_entry_index = request.entry_index.has_value();
        if (has_entry_path == has_entry_index) {
            static_cast<OperationResult&>(result) = make_operation_failure<OperationResult>(
                ArchiveErrorDomain::kInvalidArguments, "OpenArchiveFromParent requires exactly one selector", 7);
            return result;
        }

        CArchiveLink& parent_link = archive_session_link(*parent);
        CArc const* parent_arc = parent_link.GetArc();
        if (parent_arc == nullptr || parent_arc->Archive == nullptr) {
            static_cast<OperationResult&>(result) = make_operation_failure<OperationResult>(
                ArchiveErrorDomain::kInvalidArguments, "Parent archive unavailable", 7);
            return result;
        }

        UInt32 num_items = 0;
        if (parent_arc->Archive->GetNumberOfItems(&num_items) != S_OK) {
            static_cast<OperationResult&>(result) = make_operation_failure<OperationResult>(
                ArchiveErrorDomain::kUnknown, "GetNumberOfItems failed on parent", 2);
            return result;
        }

        UInt32 resolved_index = 0;
        std::string resolved_entry_path;
        if (has_entry_path) {
            std::string const needle = normalize_archive_item_path(request.entry_path);
            bool found = false;
            for (UInt32 i = 0; i < num_items; ++i) {
                std::string const candidate = archive_item_path_for_matching(*parent_arc, i);
                if (candidate == needle) {
                    resolved_index = i;
                    resolved_entry_path = candidate;
                    found = true;
                    break;
                }
            }
            if (!found) {
                static_cast<OperationResult&>(result) = make_operation_failure<OperationResult>(
                    ArchiveErrorDomain::kInvalidArguments,
                    "Entry path not found in parent archive: " + request.entry_path,
                    7);
                return result;
            }
        } else {
            resolved_index = static_cast<UInt32>(*request.entry_index);
            if (resolved_index >= num_items) {
                static_cast<OperationResult&>(result) = make_operation_failure<OperationResult>(
                    ArchiveErrorDomain::kInvalidArguments, "Entry index out of range in parent archive", 7);
                return result;
            }
            resolved_entry_path =
                normalize_archive_item_path(archive_item_path_for_matching(*parent_arc, resolved_index));
        }

        std::string const display_path =
            request.display_path_hint.empty() ? parent->display_path() : request.display_path_hint;

        auto child = std::make_shared<ArchiveOpenSession>();
        ArchiveOpenSessionNativeAccess::set_display_path(*child, display_path);
        ArchiveOpenSessionNativeAccess::set_filename_code_page(*child, request.filename_code_page);
        ArchiveOpenSessionNativeAccess::set_archive_type_hint(*child, request.archive_type_hint);
        ArchiveOpenSessionNativeAccess::set_parent(*child, parent);
        ArchiveOpenSessionNativeAccess::set_parent_generation_at_open(
            *child, ArchiveOpenSessionNativeAccess::generation(*parent));
        ArchiveOpenSessionNativeAccess::set_entry_path_from_parent(*child, resolved_entry_path);
        ArchiveOpenSessionNativeAccess::set_parent_entry_index(*child, resolved_index);
        reset_archive_session_open_state(*child);
        ArchiveOpenSessionState& child_state = archive_session_state(*child);
        std::string const parent_extraction_password =
            parent->password_defined() ? parent->password() : std::string{};

        auto finalize_success = [&](OpenArchiveSessionResult::Strategy used) {
            OpenArchiveDiagnostics inherited = archive_session_state(*parent).open_diagnostics;
            append_open_archive_diagnostics(inherited, child_state.open_diagnostics);
            child_state.open_diagnostics = std::move(inherited);
            ArchiveOpenSessionNativeAccess::set_strategy(*child, used);
            ArchiveOpenSessionNativeAccess::set_token(*child,
                                                      ArchiveSessionRegistryNativeAccess::allocate_token(registry));
            ArchiveSessionRegistryNativeAccess::register_session(registry, child);
            static_cast<OperationResult&>(result) = make_operation_success<OperationResult>("Nested archive opened");
            result.token = child->token();
            result.used_strategy = used;
            result.archive_path = child->display_path();
            result.parent_entry_index = resolved_index;
            result.disposition = OpenArchiveFromParentResult::Disposition::kArchiveSession;
        };

        auto return_unsupported = [&]() -> OpenArchiveFromParentResult {
            OperationResult failure = make_operation_failure<OperationResult>(
                ArchiveErrorDomain::kUnsupportedFormat,
                "Nested entry is not a supported archive",
                2);
            apply_open_archive_diagnostics(failure, child_state.open_diagnostics);
            static_cast<OperationResult&>(result) = std::move(failure);
            return result;
        };

        // Strategy 1: parent IInArchiveGetStream -> seekable IInStream.
        bool try_as_archive_after_extract = true;
        {
            ParentStreamRefHolder holder;
            const HRESULT acquire = acquire_parent_sub_stream(*parent, resolved_index, holder);
            if (acquire == E_ABORT) {
                static_cast<OperationResult&>(result) = make_operation_canceled<OperationResult>();
                return result;
            }
            if (acquire == S_OK) {
                IInStream* stream_raw = holder.seekable;
                CArc const* arc = nullptr;
                bool password_requested = false;
                bool wrong_password = false;
                std::string password;
                const HRESULT open_hr = open_archive_shared_from_stream(stream_raw,
                                                                        display_path,
                                                                        request.archive_type_hint,
                                                                        hooks,
                                                                        cancel_requested,
                                                                        wait_while_paused,
                                                                        OpenResultMessagePolicy::kSilentBrowse,
                                                                        /*allow_password_prompt=*/true,
                                                                        /*initial_password=*/{},
                                                                        /*codecs_already_loaded=*/false,
                                                                        *child_state.codecs,
                                                                        *child_state.types,
                                                                        *child_state.excluded_formats,
                                                                        *child_state.archive_link,
                                                                        arc,
                                                                        &password_requested,
                                                                        &wrong_password,
                                                                        &password,
                                                                        &child_state.open_diagnostics,
                                                                        request.filename_code_page);
                if (open_hr == S_OK) {
                    if (!password.empty()) {
                        child->set_password(std::move(password));
                    }
                    auto holder_heap = std::make_shared<ParentStreamRefHolder>(std::move(holder));
                    child_state.stream_ref_holder = std::static_pointer_cast<void>(holder_heap);
                    finalize_success(OpenArchiveSessionResult::Strategy::kStream);
                    return result;
                }
                if (password_requested || wrong_password) {
                    static_cast<OperationResult&>(result) = make_operation_failure<OperationResult>(
                        ArchiveErrorDomain::kPassword, "Password required or incorrect", 2);
                    return result;
                }
                if (open_hr == E_ABORT) {
                    static_cast<OperationResult&>(result) = make_operation_canceled<OperationResult>();
                    return result;
                }
                if (open_hr != S_FALSE) {
                    OperationResult failure = make_operation_failure_from_hresult<OperationResult>(open_hr);
                    apply_open_archive_diagnostics(failure, child_state.open_diagnostics);
                    static_cast<OperationResult&>(result) = std::move(failure);
                    return result;
                }
                if (request.unsupported_mode == UnsupportedNestedOpenMode::kFail) {
                    return return_unsupported();
                }
                // Normal Open needs the entry materialized for the external
                // program, but must not try the archive again after S_FALSE.
                try_as_archive_after_extract = false;
                reset_archive_session_open_state(*child);
            }
            // As in 26.02, failure to obtain a usable seekable substream is not
            // fatal by itself; the single extraction path below remains available.
        }

        size_t const file_limit = request.size_budget != 0
                                    ? request.size_budget
                                    : compute_nested_open_budget(child->depth());
        std::optional<uint64_t> const declared_size = declared_entry_size(*parent_arc, resolved_index);
        bool const prefer_file = declared_size.has_value()
                              && *declared_size > static_cast<uint64_t>(file_limit);
        size_t const memory_cap = nested_memory_cap(declared_size, file_limit);

        CMyComPtr<NativeSpillableOutStream> output;
        output.Attach(new NativeSpillableOutStream(memory_cap, resolved_entry_path, prefer_file));
        ExtractInvocationStatus const extract_status = extract_entry_to_stream(*parent,
                                                                               resolved_index,
                                                                               parent_extraction_password,
                                                                               output.Interface(),
                                                                               hooks,
                                                                               cancel_requested,
                                                                               wait_while_paused);
        if (extract_status.password_requested || extract_status.wrong_password) {
            static_cast<OperationResult&>(result) = make_operation_failure<OperationResult>(
                ArchiveErrorDomain::kPassword, "Password required or incorrect", 2);
            return result;
        }
        if (extract_status.hresult == E_ABORT) {
            static_cast<OperationResult&>(result) = make_operation_canceled<OperationResult>();
            return result;
        }
        if (extract_status.hresult != S_OK || extract_status.error_count != 0) {
            if (extract_status.error_count != 0 && !extract_status.diagnostic.empty()) {
                static_cast<OperationResult&>(result) =
                    make_operation_failure<OperationResult>(ArchiveErrorDomain::kIo, extract_status.diagnostic, 2);
            } else {
                static_cast<OperationResult&>(result) =
                    make_operation_failure_from_hresult<OperationResult>(extract_status.hresult);
            }
            return result;
        }
        HRESULT const finish_result = output->finish();
        if (finish_result != S_OK) {
            static_cast<OperationResult&>(result) = make_operation_failure<OperationResult>(
                ArchiveErrorDomain::kIo,
                output->failure_message().empty() ? "Failed to finalize nested archive extraction"
                                                  : output->failure_message(),
                2);
            return result;
        }

        auto finalize_external_file = [&]() -> OpenArchiveFromParentResult {
            if (!output->spilled_to_file()) {
                HRESULT const materialize_result = output->materialize_to_file();
                if (materialize_result != S_OK) {
                    static_cast<OperationResult&>(result) = make_operation_failure<OperationResult>(
                        ArchiveErrorDomain::kIo,
                        output->failure_message().empty() ? "Failed to materialize external-open fallback"
                                                          : output->failure_message(),
                        2);
                    return result;
                }
            }
            ArchiveExternalFileLease lease = ArchiveExternalFileLeaseAccess::create(
                output->temp_directory(), output->temp_file());
            output->release_temp_ownership();
            static_cast<OperationResult&>(result) =
                make_operation_success<OperationResult>("Nested entry materialized for external open");
            result.disposition = OpenArchiveFromParentResult::Disposition::kExternalFile;
            result.external_file = std::move(lease);
            result.archive_path = display_path;
            result.parent_entry_index = resolved_index;
            return result;
        };

        if (!try_as_archive_after_extract) {
            return finalize_external_file();
        }

        bool password_requested = false;
        bool wrong_password = false;
        std::string password;
        HRESULT open_hr = E_FAIL;
        std::shared_ptr<StreamRefHolder> memory_holder;
        if (output->spilled_to_file()) {
            CArc const* arc = nullptr;
            open_hr = open_archive_shared(output->temp_file().generic_string(),
                                          request.archive_type_hint,
                                          hooks,
                                          cancel_requested,
                                          wait_while_paused,
                                          OpenResultMessagePolicy::kSilentBrowse,
                                          /*allow_password_prompt=*/true,
                                          /*initial_password=*/{},
                                          /*codecs_already_loaded=*/false,
                                          *child_state.codecs,
                                          *child_state.types,
                                          *child_state.excluded_formats,
                                          *child_state.archive_link,
                                          arc,
                                          &password_requested,
                                          &wrong_password,
                                          &password,
                                          &child_state.open_diagnostics,
                                          request.filename_code_page);
        } else {
            memory_holder = std::make_shared<StreamRefHolder>();
            CMyComPtr2_Create<IInStream, CBufInStream> buffer_stream;
            buffer_stream->Init(output->memory_data(), output->memory_size(), /*ref=*/nullptr);
            memory_holder->stream = buffer_stream;
            CArc const* arc = nullptr;
            open_hr = open_archive_shared_from_stream(memory_holder->stream,
                                                      display_path,
                                                      request.archive_type_hint,
                                                      hooks,
                                                      cancel_requested,
                                                      wait_while_paused,
                                                      OpenResultMessagePolicy::kSilentBrowse,
                                                      /*allow_password_prompt=*/true,
                                                      /*initial_password=*/{},
                                                      /*codecs_already_loaded=*/false,
                                                      *child_state.codecs,
                                                      *child_state.types,
                                                      *child_state.excluded_formats,
                                                      *child_state.archive_link,
                                                      arc,
                                                      &password_requested,
                                                      &wrong_password,
                                                      &password,
                                                      &child_state.open_diagnostics,
                                                      request.filename_code_page);
        }

        if (open_hr == S_OK) {
            if (!password.empty()) {
                child->set_password(std::move(password));
            }
            if (output->spilled_to_file()) {
                child_state.temp_dir = output->temp_directory();
                child_state.temp_file = std::make_unique<std::filesystem::path>(output->temp_file());
                output->release_temp_ownership();
                finalize_success(OpenArchiveSessionResult::Strategy::kTempFile);
            } else {
                child_state.memory_buffer = output->take_buffer();
                child_state.stream_ref_holder = std::static_pointer_cast<void>(memory_holder);
                finalize_success(OpenArchiveSessionResult::Strategy::kMemory);
            }
            return result;
        }
        if (password_requested || wrong_password) {
            static_cast<OperationResult&>(result) = make_operation_failure<OperationResult>(
                ArchiveErrorDomain::kPassword, "Password required or incorrect", 2);
            return result;
        }
        if (open_hr == E_ABORT) {
            static_cast<OperationResult&>(result) = make_operation_canceled<OperationResult>();
            return result;
        }
        if (open_hr != S_FALSE) {
            OperationResult failure = make_operation_failure_from_hresult<OperationResult>(open_hr);
            apply_open_archive_diagnostics(failure, child_state.open_diagnostics);
            static_cast<OperationResult&>(result) = std::move(failure);
            return result;
        }
        if (request.unsupported_mode == UnsupportedNestedOpenMode::kMaterializeForExternalOpen) {
            return finalize_external_file();
        }
        return return_unsupported();
    }

    OperationResult set_native_archive_session_filename_code_page(
        ArchiveSessionRegistry& registry,
        SetArchiveSessionFilenameCodePageRequest const& request,
        ArchiveBackendHooks const& hooks,
        std::atomic<bool>* cancel_requested,
        std::function<bool()> wait_while_paused) {
        if (!request.token.is_valid()) {
            return make_operation_failure<OperationResult>(
                ArchiveErrorDomain::kInvalidArguments, "Unknown archive session token", 7);
        }
        if (request.filename_code_page.has_value()
            && !is_filename_code_page_supported(*request.filename_code_page)) {
            return make_operation_failure<OperationResult>(
                ArchiveErrorDomain::kInvalidArguments, "unsupported filename code page", 7);
        }

        std::shared_ptr<ArchiveOpenSession> session = registry.find(request.token);
        if (session == nullptr) {
            return make_operation_failure<OperationResult>(
                ArchiveErrorDomain::kInvalidArguments, "Unknown archive session token", 7);
        }

        std::vector<std::shared_ptr<ArchiveOpenSession>> lock_chain;
        for (std::shared_ptr<ArchiveOpenSession> cursor = session; cursor != nullptr;
             cursor = ArchiveOpenSessionNativeAccess::parent(*cursor)) {
            lock_chain.push_back(cursor);
        }
        std::reverse(lock_chain.begin(), lock_chain.end());
        std::vector<std::unique_lock<std::recursive_mutex>> session_locks;
        session_locks.reserve(lock_chain.size());
        for (auto const& locked_session : lock_chain) {
            session_locks.emplace_back(ArchiveOpenSessionNativeAccess::operation_mutex(*locked_session));
        }

        if (ArchiveOpenSessionNativeAccess::closed(*session)) {
            return make_operation_failure<OperationResult>(
                ArchiveErrorDomain::kInvalidArguments, "Unknown archive session token", 7);
        }
        if (session->filename_code_page() == request.filename_code_page) {
            return make_operation_success<OperationResult>("Archive filename code page unchanged");
        }
        if (registry.has_descendant(request.token)) {
            return make_operation_failure<OperationResult>(
                ArchiveErrorDomain::kInvalidArguments, "active descendant session", 7);
        }

        ArchiveOpenSessionState const& current_state = archive_session_state(*session);
        auto next_state = std::make_unique<ArchiveOpenSessionState>();
        next_state->source_version = current_state.source_version;
        next_state->archive_link = std::make_unique<CArchiveLink>();
        next_state->types = std::make_unique<CObjectVector<COpenType>>();
        next_state->excluded_formats = std::make_unique<CIntVector>();
        next_state->codecs = std::make_unique<CCodecs>();

        CArc const* arc = nullptr;
        bool password_requested = false;
        bool wrong_password = false;
        std::string opened_password;
        HRESULT open_hr = E_FAIL;
        std::string const initial_password = session->password_defined() ? session->password() : std::string();

        auto open_from_path = [&](std::filesystem::path const& path) {
            return open_archive_shared(path.string(),
                                       ArchiveOpenSessionNativeAccess::archive_type_hint(*session),
                                       hooks,
                                       cancel_requested,
                                       wait_while_paused,
                                       OpenResultMessagePolicy::kOperationMessages,
                                       /*allow_password_prompt=*/true,
                                       initial_password,
                                       /*codecs_already_loaded=*/false,
                                       *next_state->codecs,
                                       *next_state->types,
                                       *next_state->excluded_formats,
                                       *next_state->archive_link,
                                       arc,
                                       &password_requested,
                                       &wrong_password,
                                       &opened_password,
                                       &next_state->open_diagnostics,
                                       request.filename_code_page);
        };
        auto open_from_stream = [&](IInStream* stream) {
            return open_archive_shared_from_stream(stream,
                                                   session->display_path(),
                                                   ArchiveOpenSessionNativeAccess::archive_type_hint(*session),
                                                   hooks,
                                                   cancel_requested,
                                                   wait_while_paused,
                                                   OpenResultMessagePolicy::kOperationMessages,
                                                   /*allow_password_prompt=*/true,
                                                   initial_password,
                                                   /*codecs_already_loaded=*/false,
                                                   *next_state->codecs,
                                                   *next_state->types,
                                                   *next_state->excluded_formats,
                                                   *next_state->archive_link,
                                                   arc,
                                                   &password_requested,
                                                   &wrong_password,
                                                   &opened_password,
                                                   &next_state->open_diagnostics,
                                                   request.filename_code_page);
        };

        std::shared_ptr<ArchiveOpenSession> const& parent = ArchiveOpenSessionNativeAccess::parent(*session);
        if (parent == nullptr) {
            if (ArchiveOpenSessionNativeAccess::dirty(*session)) {
                if (current_state.temp_file == nullptr || current_state.temp_file->empty()) {
                    return make_operation_failure<OperationResult>(
                        ArchiveErrorDomain::kIo, "Dirty root session has no writable archive file", 2);
                }
                next_state->temp_dir = current_state.temp_dir;
                next_state->temp_file = std::make_unique<std::filesystem::path>(*current_state.temp_file);
                open_hr = open_from_path(*next_state->temp_file);
            } else {
                open_hr = open_from_path(ArchiveOpenSessionNativeAccess::source_archive_path(*session));
            }
        } else if (session->strategy() == OpenArchiveSessionResult::Strategy::kTempFile) {
            if (current_state.temp_file == nullptr || current_state.temp_file->empty()) {
                return make_operation_failure<OperationResult>(
                    ArchiveErrorDomain::kIo, "Session has no materialized archive file", 2);
            }
            next_state->temp_dir = current_state.temp_dir;
            next_state->temp_file = std::make_unique<std::filesystem::path>(*current_state.temp_file);
            open_hr = open_from_path(*next_state->temp_file);
        } else if (session->strategy() == OpenArchiveSessionResult::Strategy::kMemory) {
            next_state->memory_buffer = current_state.memory_buffer;
            auto holder = std::make_shared<StreamRefHolder>();
            CMyComPtr2_Create<IInStream, CBufInStream> buffer_stream;
            buffer_stream->Init(next_state->memory_buffer.data(), next_state->memory_buffer.size(), nullptr);
            holder->stream = buffer_stream;
            open_hr = open_from_stream(holder->stream);
            if (open_hr == S_OK) {
                next_state->stream_ref_holder = std::static_pointer_cast<void>(holder);
            }
        } else {
            CArc const* parent_arc = archive_session_link(*parent).GetArc();
            if (parent_arc == nullptr || parent_arc->Archive == nullptr) {
                return make_operation_failure<OperationResult>(
                    ArchiveErrorDomain::kIo, "Parent archive unavailable during encoding reload", 2);
            }
            ScopedFilenameCodePage parent_scope(parent->filename_code_page());
            UInt32 resolved_index = 0;
            if (std::optional<OperationResult> validation_error =
                    validate_archive_session_parent_item(*session, *parent_arc, &resolved_index);
                validation_error.has_value()) {
                return *validation_error;
            }
            ParentStreamRefHolder holder;
            HRESULT const acquire_hr = acquire_parent_sub_stream(*parent, resolved_index, holder);
            if (acquire_hr != S_OK) {
                return make_operation_failure<OperationResult>(
                    ArchiveErrorDomain::kIo, "Parent stream unavailable during encoding reload", acquire_hr);
            }
            open_hr = open_from_stream(holder.seekable);
            if (open_hr == S_OK) {
                next_state->stream_ref_holder =
                    std::static_pointer_cast<void>(std::make_shared<ParentStreamRefHolder>(std::move(holder)));
            }
        }

        if (open_hr != S_OK) {
            if (password_requested || wrong_password) {
                return make_operation_failure<OperationResult>(
                    ArchiveErrorDomain::kPassword, "Password required or incorrect", 2);
            }
            if (open_hr == E_ABORT) {
                return make_operation_canceled<OperationResult>();
            }
            OperationResult failure = make_operation_failure_from_hresult<OperationResult>(open_hr);
            apply_open_archive_diagnostics(failure, next_state->open_diagnostics);
            return failure;
        }

        if (parent != nullptr) {
            OpenArchiveDiagnostics inherited = archive_session_state(*parent).open_diagnostics;
            append_open_archive_diagnostics(inherited, next_state->open_diagnostics);
            next_state->open_diagnostics = std::move(inherited);
        }
        if (next_state->open_diagnostics.has_errors()) {
            return make_operation_failure_from_open_diagnostics<OperationResult>(next_state->open_diagnostics);
        }

        ArchiveOpenSessionNativeAccess::replace_state(*session, std::move(next_state));
        ArchiveOpenSessionNativeAccess::set_filename_code_page(*session, request.filename_code_page);
        if (!opened_password.empty()) {
            session->set_password(std::move(opened_password));
        }
        return make_operation_success<OperationResult>("Archive filename code page changed");
    }

} // namespace z7::app
