// src/archive_application/src/ports/archive_backend_port.h
// Role: Narrow private port from archive session runtime to archive backend.

#pragma once

#include <functional>
#include <memory>
#include <vector>

#include "archive_session.h"

namespace z7::app {

    using NativeEventCallback = std::function<void(OperationEvent const&)>;

    struct ArchiveBackendHooks {
        NativeEventCallback on_event;
        AskOverwriteCallback ask_overwrite;
        std::function<PasswordReply(PasswordPrompt const&)> ask_password;
        std::function<ChoiceReply(ChoicePrompt const&)> ask_choice;
        std::function<MemoryLimitReply(MemoryLimitPrompt const&)> ask_memory_limit;
        // Non-null only when ListRequest::streaming_mode == true. Called on the
        // backend worker thread with each batch; return false to abort listing.
        std::function<bool(std::vector<ArchiveListEntry>&&)> on_list_batch;
    };

    struct NativeInvokeResult {
        OperationResult base;
        OperationPayload payload;
    };

    class INativeArchiveBackend {
    public:
        virtual ~INativeArchiveBackend() = default;

        virtual char const* backend_name() const = 0;
        virtual BackendCapabilities capabilities() const = 0;
        virtual NativeInvokeResult invoke(ArchiveRequest const& request, ArchiveBackendHooks const& callbacks) = 0;

        virtual void cancel() = 0;
        virtual bool supports_pause() const = 0;
        virtual void pause() = 0;
        virtual void resume() = 0;
    };

    std::unique_ptr<INativeArchiveBackend> create_native_archive_backend();
    BenchmarkMemoryEstimate estimate_benchmark_memory_native(BenchmarkRequest const& request);
    CompressionResourcesEstimate estimate_compression_resources_native(AddRequest const& request);
    BenchmarkSystemInfo query_benchmark_system_info_native();

} // namespace z7::app
