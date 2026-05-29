// src/archive_application/src/native_7z/backend/operation_runner.h
// Role: Typed operation pipeline (validation/preflight/lifecycle/error mapping).

#pragma once

#include <exception>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "core/internal.h"

namespace z7::app {

template <typename TRequest, typename TResult>
class OperationRunner final {
 public:
  using Handler =
      TResult (NativeArchiveBackend::*)(const TRequest&, const ArchiveBackendHooks&);
  using Validator = std::function<std::optional<OperationResult>(const TRequest&)>;
  using Preflight = std::function<std::optional<OperationResult>(const TRequest&)>;
  using CodecGate = std::function<bool(const TRequest&)>;
  enum class ExecutionEnvelope {
    kDirect,
    kCancelable,
    kPauseable
  };
  using EnvelopeSelector = std::function<ExecutionEnvelope(const TRequest&)>;
  enum class PauseableFlag {
    kNone,
    kHashing,
    kUpdating,
    kExtracting,
    kTesting,
    kBenchmarking
  };

  struct Options final {
    Validator validator;
    Preflight preflight;
    bool require_codecs = false;
    CodecGate require_codecs_when;
    ExecutionEnvelope execution_envelope = ExecutionEnvelope::kDirect;
    EnvelopeSelector execution_envelope_when;
    PauseableFlag pauseable_flag = PauseableFlag::kNone;
    bool enforce_lifecycle = true;
    std::string_view operation_name;
  };

  OperationRunner(NativeArchiveBackend& backend,
                  const ArchiveBackendHooks& callbacks,
                  Handler handler)
      : backend_(backend),
        callbacks_(callbacks),
        handler_(handler) {}

  TResult run(const TRequest& request, const Options& options = {}) const {
    LifecycleEnvelope lifecycle(callbacks_, options.enforce_lifecycle);

    if (options.validator) {
      if (std::optional<OperationResult> invalid = options.validator(request); invalid.has_value()) {
        TResult result = from_base_result<TResult>(std::move(*invalid));
        lifecycle.finish();
        normalize_result(&result);
        return result;
      }
    }

    if (options.preflight) {
      if (std::optional<OperationResult> preflight_error = options.preflight(request);
          preflight_error.has_value()) {
        TResult result = from_base_result<TResult>(std::move(*preflight_error));
        lifecycle.finish();
        normalize_result(&result);
        return result;
      }
    }

    const bool require_codecs =
        options.require_codecs ||
        (options.require_codecs_when && options.require_codecs_when(request));
    CCodecs preloaded_codecs;
    CCodecs* bound_codecs = nullptr;
    if (require_codecs) {
      if (std::optional<OperationResult> preflight_error =
              preflight_load_codecs(&preloaded_codecs);
          preflight_error.has_value()) {
        TResult result = from_base_result<TResult>(std::move(*preflight_error));
        lifecycle.finish();
        normalize_result(&result);
        return result;
      }
      bound_codecs = &preloaded_codecs;
    }

    ScopedCodecBinding codec_binding(backend_, bound_codecs);
    ExecutionEnvelope envelope = options.execution_envelope;
    if (options.execution_envelope_when) {
      envelope = options.execution_envelope_when(request);
    }

    try {
      auto invoke_handler = [&]() -> TResult {
        return (backend_.*handler_)(request, lifecycle.callbacks());
      };

      TResult result;
      switch (envelope) {
        case ExecutionEnvelope::kCancelable:
          result = backend_.run_with_cancelable_operation(invoke_handler);
          break;
        case ExecutionEnvelope::kPauseable: {
          std::atomic<bool>* active_flag =
              resolve_pauseable_flag(options.pauseable_flag);
          if (active_flag != nullptr) {
            result = backend_.run_with_pauseable_operation(*active_flag, invoke_handler);
          } else {
            result = invoke_handler();
          }
          break;
        }
        case ExecutionEnvelope::kDirect:
          result = invoke_handler();
          break;
      }

      lifecycle.finish();
      normalize_result(&result);
      return result;
    } catch (const std::exception& ex) {
      lifecycle.finish();
      return from_base_result<TResult>(make_exception_failure(options.operation_name, ex.what()));
    } catch (...) {
      lifecycle.finish();
      return from_base_result<TResult>(
          make_exception_failure(options.operation_name, "unknown exception"));
    }
  }

 private:
  class LifecycleEnvelope final {
   public:
    LifecycleEnvelope(const ArchiveBackendHooks& base_callbacks, bool enabled)
        : base_callbacks_(base_callbacks),
          enabled_(enabled && static_cast<bool>(base_callbacks.on_event)),
          callbacks_(base_callbacks) {
      if (!enabled_) {
        return;
      }
      forward_event_ = base_callbacks_.on_event;
      callbacks_.on_event = [this](const OperationEvent& event) { this->on_event(event); };
    }

    const ArchiveBackendHooks& callbacks() const {
      return enabled_ ? callbacks_ : base_callbacks_;
    }

    void finish() {
      if (!enabled_) {
        return;
      }
      emit_prepare_and_running_if_missing();
      emit_stage_if_missing(OperationStage::kFinalizing);
      emit_stage_if_missing(OperationStage::kCompleted);
    }

   private:
    void on_event(const OperationEvent& event) {
      if (event.kind == OperationEventKind::kLifecycle) {
        if (event.stage == OperationStage::kFinalizing ||
            event.stage == OperationStage::kCompleted) {
          emit_prepare_and_running_if_missing();
        }
        mark_stage(event.stage);
        forward_event_(event);
        return;
      }
      emit_prepare_and_running_if_missing();
      forward_event_(event);
    }

    void emit_prepare_and_running_if_missing() {
      emit_stage_if_missing(OperationStage::kPrepare);
      emit_stage_if_missing(OperationStage::kRunning);
    }

    void emit_stage_if_missing(OperationStage stage) {
      if (is_seen(stage)) {
        return;
      }
      mark_stage(stage);
      OperationEvent lifecycle_event;
      lifecycle_event.kind = OperationEventKind::kLifecycle;
      lifecycle_event.stage = stage;
      forward_event_(lifecycle_event);
    }

    bool is_seen(OperationStage stage) const {
      switch (stage) {
        case OperationStage::kPrepare:
          return seen_prepare_;
        case OperationStage::kRunning:
          return seen_running_;
        case OperationStage::kFinalizing:
          return seen_finalizing_;
        case OperationStage::kCompleted:
          return seen_completed_;
      }
      return false;
    }

    void mark_stage(OperationStage stage) {
      switch (stage) {
        case OperationStage::kPrepare:
          seen_prepare_ = true;
          break;
        case OperationStage::kRunning:
          seen_running_ = true;
          break;
        case OperationStage::kFinalizing:
          seen_finalizing_ = true;
          break;
        case OperationStage::kCompleted:
          seen_completed_ = true;
          break;
      }
    }

    const ArchiveBackendHooks& base_callbacks_;
    bool enabled_ = false;
    ArchiveBackendHooks callbacks_;
    NativeEventCallback forward_event_;
    bool seen_prepare_ = false;
    bool seen_running_ = false;
    bool seen_finalizing_ = false;
    bool seen_completed_ = false;
  };

  class ScopedCodecBinding final {
   public:
    ScopedCodecBinding(NativeArchiveBackend& backend, CCodecs* codecs)
        : backend_(backend),
          previous_(backend_.bound_codecs_) {
      backend_.bound_codecs_ = codecs;
    }

    ~ScopedCodecBinding() {
      backend_.bound_codecs_ = previous_;
    }

   private:
    NativeArchiveBackend& backend_;
    CCodecs* previous_ = nullptr;
  };

  std::atomic<bool>* resolve_pauseable_flag(PauseableFlag flag) const {
    switch (flag) {
      case PauseableFlag::kHashing:
        return &backend_.hashing_active_;
      case PauseableFlag::kUpdating:
        return &backend_.updating_active_;
      case PauseableFlag::kExtracting:
        return &backend_.extracting_active_;
      case PauseableFlag::kTesting:
        return &backend_.testing_active_;
      case PauseableFlag::kBenchmarking:
        return &backend_.benchmarking_active_;
      case PauseableFlag::kNone:
        return nullptr;
    }
    return nullptr;
  }

  static OperationResult make_exception_failure(std::string_view operation_name,
                                                std::string_view message) {
    std::string diagnostic;
    if (operation_name.empty()) {
      diagnostic = "Unhandled exception in archive operation: ";
    } else {
      diagnostic = "Unhandled exception in archive operation '";
      diagnostic.append(operation_name);
      diagnostic.append("': ");
    }
    diagnostic.append(message);

    OperationResult out;
    out.ok = false;
    out.error = make_archive_error(ArchiveErrorDomain::kUnknown, diagnostic, 2);
    out.native_exit_code = out.error.native_exit_code;
    out.native_execution.native_exit_code = out.native_exit_code;
    out.native_execution.termination_reason = NativeTerminationReason::kAborted;
    out.summary = describe_archive_error(out.error);
    return out;
  }

  static std::optional<OperationResult> preflight_load_codecs(CCodecs* codecs) {
    if (codecs == nullptr) {
      return make_exception_failure("codec-preflight", "missing codec storage");
    }
    const HRESULT load_res = load_codecs_shared(*codecs);
    if (load_res == S_OK) {
      return std::nullopt;
    }

    OperationResult result;
    result.ok = false;
    result.error = map_hresult_to_archive_error(load_res);
    result.native_exit_code = result.error.native_exit_code;
    result.native_execution.native_exit_code = result.native_exit_code;
    result.native_execution.termination_reason =
        result.error.domain == ArchiveErrorDomain::kCanceled
            ? NativeTerminationReason::kCanceled
            : NativeTerminationReason::kCompleted;
    result.summary = describe_archive_error(result.error);
    return result;
  }

  static void normalize_result(TResult* result) {
    if (result == nullptr) {
      return;
    }

    if (!result->ok && result->error.domain == ArchiveErrorDomain::kNone) {
      result->error =
          make_archive_error(ArchiveErrorDomain::kUnknown, "Unknown operation failure", 2);
    }

    if (result->native_exit_code == 0 && result->error.native_exit_code != 0) {
      result->native_exit_code = result->error.native_exit_code;
    }
    if (result->error.native_exit_code == 0 && result->native_exit_code != 0) {
      result->error.native_exit_code = result->native_exit_code;
    }

    if (result->native_execution.native_exit_code == 0 && result->native_exit_code != 0) {
      result->native_execution.native_exit_code = result->native_exit_code;
    }
    if (result->error.domain == ArchiveErrorDomain::kCanceled) {
      result->native_execution.termination_reason = NativeTerminationReason::kCanceled;
    }

    if (result->summary.empty()) {
      result->summary = describe_archive_error(result->error);
    }
  }

  NativeArchiveBackend& backend_;
  const ArchiveBackendHooks& callbacks_;
  Handler handler_;
};

}  // namespace z7::app
