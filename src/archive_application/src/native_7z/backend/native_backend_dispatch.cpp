// src/archive_application/src/native_7z/backend/native_backend_dispatch.cpp
// Role: Native backend ArchiveRequest dispatch.

#include "backend/native_request_policy.h"

#include <type_traits>

namespace z7::app {
namespace {

template <typename TRequest, typename TResult>
NativeInvokeResult invoke_typed_backend(
    NativeArchiveBackend& backend,
    const ArchiveBackendHooks& callbacks,
    const TRequest& request,
    typename OperationRunner<TRequest, TResult>::Handler handler) {
  OperationRunner<TRequest, TResult> runner(backend, callbacks, handler);
  TResult result = runner.run(request, make_runner_options<TRequest, TResult>(request));
  return {static_cast<OperationResult>(result), result};
}

}  // namespace

NativeInvokeResult NativeArchiveBackend::invoke(
    const ArchiveRequest& request,
    const ArchiveBackendHooks& callbacks) {
  return std::visit(
      [&](const auto& typed_request) -> NativeInvokeResult {
        using Request = std::decay_t<decltype(typed_request)>;
        if constexpr (std::is_same_v<Request, AddRequest>) {
          return invoke_typed_backend<Request, AddResult>(
              *this, callbacks, typed_request, &NativeArchiveBackend::add);
        } else if constexpr (std::is_same_v<Request, ExtractRequest>) {
          return invoke_typed_backend<Request, ExtractResult>(
              *this, callbacks, typed_request, &NativeArchiveBackend::extract);
        } else if constexpr (std::is_same_v<Request, TestRequest>) {
          return invoke_typed_backend<Request, TestResult>(
              *this, callbacks, typed_request, &NativeArchiveBackend::test);
        } else if constexpr (std::is_same_v<Request, BenchmarkRequest>) {
          return invoke_typed_backend<Request, BenchmarkResult>(
              *this, callbacks, typed_request, &NativeArchiveBackend::benchmark);
        } else if constexpr (std::is_same_v<Request, SplitRequest>) {
          return invoke_typed_backend<Request, SplitResult>(
              *this, callbacks, typed_request, &NativeArchiveBackend::split);
        } else if constexpr (std::is_same_v<Request, CombineRequest>) {
          return invoke_typed_backend<Request, CombineResult>(
              *this, callbacks, typed_request, &NativeArchiveBackend::combine);
        } else if constexpr (std::is_same_v<Request, HashRequest>) {
          return invoke_typed_backend<Request, HashResult>(
              *this, callbacks, typed_request, &NativeArchiveBackend::hash);
        } else if constexpr (std::is_same_v<Request, DeleteRequest>) {
          return invoke_typed_backend<Request, DeleteResult>(
              *this, callbacks, typed_request, &NativeArchiveBackend::remove);
        } else if constexpr (std::is_same_v<Request, OpenArchiveRequest>) {
          return invoke_typed_backend<Request, OpenArchiveResult>(
              *this, callbacks, typed_request, &NativeArchiveBackend::open_archive);
        } else if constexpr (std::is_same_v<Request, OpenArchiveFromPathRequest>) {
          return invoke_typed_backend<Request, OpenArchiveSessionResult>(
              *this,
              callbacks,
              typed_request,
              &NativeArchiveBackend::open_archive_from_path);
        } else if constexpr (std::is_same_v<Request, OpenArchiveFromParentRequest>) {
          return invoke_typed_backend<Request, OpenArchiveSessionResult>(
              *this,
              callbacks,
              typed_request,
              &NativeArchiveBackend::open_archive_from_parent);
        } else if constexpr (std::is_same_v<Request, CloseArchiveSessionRequest>) {
          NativeInvokeResult out;
          OperationResult result = close_archive_session(typed_request, callbacks);
          out.base = result;
          out.payload = std::monostate{};
          return out;
        } else if constexpr (std::is_same_v<Request, ListRequest>) {
          return invoke_typed_backend<Request, ListResult>(
              *this, callbacks, typed_request, &NativeArchiveBackend::list);
        } else if constexpr (std::is_same_v<Request, ArchivePropertiesRequest>) {
          return invoke_typed_backend<Request, ArchivePropertiesResult>(
              *this, callbacks, typed_request, &NativeArchiveBackend::properties);
        } else if constexpr (std::is_same_v<Request, NavigateRequest>) {
          return invoke_typed_backend<Request, NavigateResult>(
              *this, callbacks, typed_request, &NativeArchiveBackend::navigate);
        } else if constexpr (std::is_same_v<Request, CopyRequest>) {
          return invoke_typed_backend<Request, CopyResult>(
              *this, callbacks, typed_request, &NativeArchiveBackend::copy);
        } else if constexpr (std::is_same_v<Request, MoveRequest>) {
          return invoke_typed_backend<Request, MoveResult>(
              *this, callbacks, typed_request, &NativeArchiveBackend::move);
        } else if constexpr (std::is_same_v<Request, RenameRequest>) {
          return invoke_typed_backend<Request, RenameResult>(
              *this, callbacks, typed_request, &NativeArchiveBackend::rename);
        } else if constexpr (std::is_same_v<Request, CreateRequest>) {
          return invoke_typed_backend<Request, CreateResult>(
              *this, callbacks, typed_request, &NativeArchiveBackend::create);
        } else if constexpr (std::is_same_v<Request, ArchiveCommentRequest>) {
          return invoke_typed_backend<Request, ArchiveCommentResult>(
              *this, callbacks, typed_request, &NativeArchiveBackend::comment_archive);
        } else if constexpr (std::is_same_v<Request, FilesystemCommentRequest>) {
          return invoke_typed_backend<Request, FilesystemCommentResult>(
              *this, callbacks, typed_request, &NativeArchiveBackend::comment_filesystem);
        } else if constexpr (std::is_same_v<Request, GetEntryInfoRequest>) {
          return invoke_typed_backend<Request, GetEntryInfoResult>(
              *this, callbacks, typed_request, &NativeArchiveBackend::get_entry_info);
        } else {
          static_assert(sizeof(Request) == 0, "Unhandled archive request type");
        }
      },
      request.payload);
}

}  // namespace z7::app
