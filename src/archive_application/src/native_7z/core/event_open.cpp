// src/archive_application/src/native_7z/core/event_open.cpp
// Role: Shared archive-open helpers for native backend operations.

#include "core/internal.h"
#include "third_party_adapter/callbacks_update_operation.h"

namespace z7::app {
    namespace {

        bool cancel_requested_now(std::atomic<bool> const* cancel_requested) {
            return cancel_requested != nullptr && cancel_requested->load(std::memory_order_relaxed);
        }

        HRESULT abort_if_canceled(std::atomic<bool> const* cancel_requested) {
            return cancel_requested_now(cancel_requested) ? E_ABORT : S_OK;
        }

        // Shared COpenOptions setup for both path-based and stream-based opens.
        // `display_path` is used to set `filePath` (affects extension / format hint
        // resolution). `in_stream` is optional; when non-null, CArchiveLink::Open2
        // reads from the stream instead of opening `filePath` from disk.
        HRESULT prepare_open_options(std::string const& display_path,
                                     std::string const& archive_type_hint,
                                     bool recursive_autodetect,
                                     CCodecs& codecs,
                                     CObjectVector<COpenType>& types,
                                     CIntVector& excluded_formats,
                                     IInStream* in_stream,
                                     COpenOptions& open_options) {
            const HRESULT type_res = prepare_open_types_for_archive(archive_type_hint, codecs, types);
            if (type_res != S_OK) {
                return type_res;
            }
            if (recursive_autodetect && archive_type_hint.empty()) {
                // An empty type vector is the upstream signal to keep opening a
                // handler's main subfile.  The old synthetic non-recursive type
                // stopped path operations at DMG/GPT/container layers.
                types.Clear();
            }

            excluded_formats.Clear();
#ifndef Z7_SFX
            open_options.props = nullptr;
#endif
            open_options.codecs = &codecs;
            open_options.types = &types;
            open_options.excludedFormats = &excluded_formats;
            open_options.filePath = utf8_to_ustring(display_path);
            open_options.stream = in_stream;
            return S_OK;
        }

    } // namespace

    int open_archive_shared(std::string const& archive_path,
                            std::string const& archive_type_hint,
                            ArchiveBackendHooks const& hooks,
                            std::atomic<bool>* cancel_requested,
                            std::function<bool()> wait_while_paused,
                            bool enable_open_callback,
                            bool codecs_already_loaded,
                            CCodecs& codecs,
                            CObjectVector<COpenType>& types,
                            CIntVector& excluded_formats,
                            CArchiveLink& archive_link,
                            CArc const*& arc,
                            bool* out_password_requested,
                            bool* out_wrong_password,
                            std::string* out_password) {
        arc = nullptr;
        if (out_password_requested != nullptr) {
            *out_password_requested = false;
        }
        if (out_wrong_password != nullptr) {
            *out_wrong_password = false;
        }
        if (out_password != nullptr) {
            out_password->clear();
        }
        if (const HRESULT canceled = abort_if_canceled(cancel_requested); canceled != S_OK) {
            return canceled;
        }

        if (!codecs_already_loaded) {
            const HRESULT load_res = load_codecs_shared(codecs);
            if (load_res != S_OK) {
                return load_res;
            }
        }

        COpenOptions open_options;
        const HRESULT prep_res = prepare_open_options(
            archive_path, archive_type_hint, true, codecs, types, excluded_formats, nullptr, open_options);
        if (prep_res != S_OK) {
            return prep_res;
        }

        bool const capture_password_state =
            out_password_requested != nullptr || out_wrong_password != nullptr || out_password != nullptr;
        std::unique_ptr<NativeUpdateOperationCallback> open_callback;
        if (enable_open_callback || capture_password_state) {
            ArchiveBackendHooks callback_hooks = hooks;
            if (!enable_open_callback) {
                callback_hooks.on_event = {};
                callback_hooks.ask_password = {};
            }
            open_callback = std::make_unique<NativeUpdateOperationCallback>(callback_hooks,
                                                                            cancel_requested,
                                                                            std::move(wait_while_paused),
                                                                            archive_path,
                                                                            NativeUpdateOperationCallback::Mode::kAdd);
        }

        IOpenCallbackUI* callback_ui = open_callback ? open_callback.get() : nullptr;
        // Path-based operations match the original extract/list pipeline: open
        // through container and filesystem layers (for example DMG -> GPT ->
        // HFS/APFS) and operate on the deepest supported archive.  Stream-based
        // parent-session opens below intentionally remain one explicit layer.
        const HRESULT open_res = archive_link.Open_Strict(open_options, callback_ui);
        if (open_callback) {
            if (out_password_requested != nullptr) {
                *out_password_requested = open_callback->password_requested();
            }
            if (out_wrong_password != nullptr) {
                *out_wrong_password = open_callback->wrong_password()
                                   || (open_res != S_OK && open_callback->password_defined());
            }
            if (out_password != nullptr) {
                *out_password = open_callback->password();
            }
        }
        if (open_res != S_OK) {
            return open_res;
        }
        if (const HRESULT canceled = abort_if_canceled(cancel_requested); canceled != S_OK) {
            return canceled;
        }

        arc = archive_link.GetArc();
        if (arc == nullptr || arc->Archive == nullptr) {
            return E_FAIL;
        }
        return S_OK;
    }

    int open_archive_shared_from_stream(IInStream* in_stream,
                                        std::string const& display_path,
                                        std::string const& archive_type_hint,
                                        ArchiveBackendHooks const& hooks,
                                        std::atomic<bool>* cancel_requested,
                                        std::function<bool()> wait_while_paused,
                                        bool enable_open_callback,
                                        bool codecs_already_loaded,
                                        CCodecs& codecs,
                                        CObjectVector<COpenType>& types,
                                        CIntVector& excluded_formats,
                                        CArchiveLink& archive_link,
                                        CArc const*& arc,
                                        bool* out_password_requested,
                                        bool* out_wrong_password,
                                        std::string* out_password) {
        arc = nullptr;
        if (out_password_requested != nullptr) {
            *out_password_requested = false;
        }
        if (out_wrong_password != nullptr) {
            *out_wrong_password = false;
        }
        if (out_password != nullptr) {
            out_password->clear();
        }
        if (in_stream == nullptr) {
            return E_INVALIDARG;
        }
        if (const HRESULT canceled = abort_if_canceled(cancel_requested); canceled != S_OK) {
            return canceled;
        }

        if (!codecs_already_loaded) {
            const HRESULT load_res = load_codecs_shared(codecs);
            if (load_res != S_OK) {
                return load_res;
            }
        }

        COpenOptions open_options;
        const HRESULT prep_res = prepare_open_options(
            display_path, archive_type_hint, false, codecs, types, excluded_formats, in_stream, open_options);
        if (prep_res != S_OK) {
            return prep_res;
        }

        bool const capture_password_state =
            out_password_requested != nullptr || out_wrong_password != nullptr || out_password != nullptr;
        std::unique_ptr<NativeUpdateOperationCallback> open_callback;
        if (enable_open_callback || capture_password_state) {
            ArchiveBackendHooks callback_hooks = hooks;
            if (!enable_open_callback) {
                callback_hooks.on_event = {};
                callback_hooks.ask_password = {};
            }
            open_callback = std::make_unique<NativeUpdateOperationCallback>(callback_hooks,
                                                                            cancel_requested,
                                                                            std::move(wait_while_paused),
                                                                            display_path,
                                                                            NativeUpdateOperationCallback::Mode::kAdd);
        }

        IOpenCallbackUI* callback_ui = open_callback ? open_callback.get() : nullptr;
        const HRESULT open_res = archive_link.Open2(open_options, callback_ui);
        if (open_callback) {
            if (out_password_requested != nullptr) {
                *out_password_requested = open_callback->password_requested();
            }
            if (out_wrong_password != nullptr) {
                *out_wrong_password = open_callback->wrong_password()
                                   || (open_res != S_OK && open_callback->password_defined());
            }
            if (out_password != nullptr) {
                *out_password = open_callback->password();
            }
        }
        if (open_res != S_OK) {
            return open_res;
        }
        if (const HRESULT canceled = abort_if_canceled(cancel_requested); canceled != S_OK) {
            return canceled;
        }

        arc = archive_link.GetArc();
        if (arc == nullptr || arc->Archive == nullptr) {
            return E_FAIL;
        }
        return S_OK;
    }

    std::optional<ArchiveError> complete_operation_open_error(CArchiveLink const& archive_link) {
        std::string diagnostic;
        for (unsigned level = 0; level < archive_link.Arcs.Size(); ++level) {
            CArc const& arc = archive_link.Arcs[level];
            CArcErrorInfo const& error_info = arc.ErrorInfo;
            UInt32 const error_flags = error_info.GetErrorFlags();
            std::string const error_message =
                z7::common::trim_ascii_space_copy(ustring_to_utf8(error_info.ErrorMessage));
            if (error_flags == 0 && error_message.empty()) {
                continue;
            }

            if (!diagnostic.empty()) {
                diagnostic += "; ";
            }
            if (level != 0 && !arc.Path.IsEmpty()) {
                diagnostic += ustring_to_utf8(arc.Path);
                diagnostic += ": ";
            }
            if (!error_message.empty()) {
                diagnostic += error_message;
            } else {
                diagnostic += "Archive open error";
            }
            if (error_flags != 0) {
                diagnostic += " (error flags ";
                diagnostic += std::to_string(error_flags);
                diagnostic += ')';
            }
        }

        if (diagnostic.empty()) {
            return std::nullopt;
        }
        return make_archive_error(ArchiveErrorDomain::kIo, std::move(diagnostic), 2);
    }

} // namespace z7::app
