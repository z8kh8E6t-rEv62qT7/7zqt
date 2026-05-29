// src/ui/filemanager/src/archive_process_runner/core.cpp
// Role: Lifecycle and execution plumbing for archive process runner.

#include "archive_process_runner.h"

#include "archive_error.h"
#include "portable_settings.h"
#include "archive_delegate_qt.h"
#include "archive_format.h"
#include "archive_session_helpers.h"
#include "archive_string_codec_qt.h"
#include "large_pages_settings.h"
#include "official_lang_catalog.h"
#include "core_prompts.h"
#include "helpers.h"

namespace z7::ui::filemanager {

using namespace runner_helpers;

namespace {

void emit_stage_and_log(ArchiveProcessRunner* owner,
                        const z7::app::ArchiveLog& log) {
  if (owner == nullptr) {
    return;
  }
  const QString stage_text =
      z7::ui::archive_support::stage_label_for(log.stage);
  const QString log_line =
      z7::ui::archive_support::from_local8_string(log.message);
  if (!stage_text.isEmpty()) {
    emit owner->stage_changed(stage_text);
  }
  if (!log_line.isEmpty()) {
    emit owner->log_line(log_line);
  }
}

void emit_progress_signals(ArchiveProcessRunner* owner,
                           const z7::app::ProgressSnapshot& progress) {
  if (owner == nullptr) {
    return;
  }
  const QString stage_text =
      z7::ui::archive_support::stage_label_for(progress.stage);
  const QString log_line =
      z7::ui::archive_support::from_local8_string(progress.message);
  const QString current_path =
      z7::ui::archive_support::from_local8_string(progress.current_path);
  if (!stage_text.isEmpty()) {
    emit owner->stage_changed(stage_text);
  }
  if (!log_line.isEmpty()) {
    emit owner->log_line(log_line);
  }
  if (progress.percent >= 0 && progress.percent <= 100) {
    emit owner->progress_changed(progress.percent);
  }
  emit owner->detailed_progress_changed(progress.totals_known,
                                        static_cast<quint64>(progress.total_bytes),
                                        static_cast<quint64>(progress.completed_bytes),
                                        static_cast<quint64>(progress.total_files),
                                        static_cast<quint64>(progress.completed_files),
                                        static_cast<quint64>(progress.error_count),
                                        progress.ratio_info.has_value() &&
                                            progress.ratio_info->input_size_known,
                                        progress.ratio_info.has_value()
                                            ? static_cast<quint64>(progress.ratio_info->input_size)
                                            : 0,
                                        progress.ratio_info.has_value() &&
                                            progress.ratio_info->output_size_known,
                                        progress.ratio_info.has_value()
                                            ? static_cast<quint64>(progress.ratio_info->output_size)
                                            : 0,
                                        progress.ratio_info.has_value()
                                            ? progress.ratio_info->compressing_mode
                                            : true,
                                        current_path);
}

void emit_finished_queued(ArchiveProcessRunner* owner,
                          bool ok,
                          int exit_code,
                          int error_domain,
                          const QString& summary) {
  QPointer<ArchiveProcessRunner> guarded_owner(owner);
  if (guarded_owner.isNull()) {
    return;
  }
  QMetaObject::invokeMethod(
      guarded_owner.data(),
      [guarded_owner, ok, exit_code, error_domain, summary]() {
        if (guarded_owner.isNull()) {
          return;
        }
        emit guarded_owner->finished(ok, exit_code, error_domain, summary);
      },
      Qt::QueuedConnection);
}

class RunnerDelegate final
    : public z7::ui::archive_support::OwnerRelayDelegate<ArchiveProcessRunner> {
 public:
  using OverwritePromptFn =
      std::function<std::optional<z7::app::OverwriteDecision>(
          const z7::app::OverwritePrompt&)>;
  using PasswordPromptFn =
      std::function<std::optional<z7::app::PasswordReply>(
          const z7::app::PasswordPrompt&)>;
  using ChoicePromptFn =
      std::function<std::optional<z7::app::ChoiceReply>(
          const z7::app::ChoicePrompt&)>;
  using MemoryLimitPromptFn =
      std::function<std::optional<z7::app::MemoryLimitReply>(
          const z7::app::MemoryLimitPrompt&)>;

  RunnerDelegate(ArchiveProcessRunner* owner,
                 OverwritePromptFn overwrite_prompt,
                 PasswordPromptFn password_prompt,
                 ChoicePromptFn choice_prompt,
                 MemoryLimitPromptFn memory_limit_prompt)
      : z7::ui::archive_support::OwnerRelayDelegate<ArchiveProcessRunner>(
            owner,
            [owner](
                const z7::app::OperationOutcome& outcome) {
              owner->on_task_finished(outcome);
            },
            nullptr,
            z7::ui::archive_support::MissingTargetPolicy::kDrop),
        overwrite_prompt_(std::move(overwrite_prompt)),
        password_prompt_(std::move(password_prompt)),
        choice_prompt_(std::move(choice_prompt)),
        memory_limit_prompt_(std::move(memory_limit_prompt)) {}

  void on_log(const z7::app::ArchiveLog& log) override {
    const z7::app::ArchiveLog log_copy = log;
    post_to_owner([log_copy](ArchiveProcessRunner* owner) {
      emit_stage_and_log(owner, log_copy);
    });
  }

  void on_progress(const z7::app::ProgressSnapshot& progress) override {
    z7::app::ProgressSnapshot progress_copy = progress;
    post_to_owner([progress_copy = std::move(progress_copy)](
                      ArchiveProcessRunner* owner) {
      emit_progress_signals(owner, progress_copy);
    });
  }

  std::optional<z7::app::OverwriteDecision> request_overwrite(
      const z7::app::OverwritePrompt& prompt) override {
    if (!overwrite_prompt_) {
      return std::nullopt;
    }
    return overwrite_prompt_(prompt);
  }

  std::optional<z7::app::PasswordReply> request_password(
      const z7::app::PasswordPrompt& prompt) override {
    if (!password_prompt_) {
      return std::nullopt;
    }
    return password_prompt_(prompt);
  }

  std::optional<z7::app::ChoiceReply> request_choice(
      const z7::app::ChoicePrompt& prompt) override {
    if (!choice_prompt_) {
      return std::nullopt;
    }
    return choice_prompt_(prompt);
  }

  std::optional<z7::app::MemoryLimitReply> request_memory_limit(
      const z7::app::MemoryLimitPrompt& prompt) override {
    if (!memory_limit_prompt_) {
      return std::nullopt;
    }
    return memory_limit_prompt_(prompt);
  }

 private:
  OverwritePromptFn overwrite_prompt_;
  PasswordPromptFn password_prompt_;
  ChoicePromptFn choice_prompt_;
  MemoryLimitPromptFn memory_limit_prompt_;
};

}  // namespace

ArchiveProcessRunner::ArchiveProcessRunner(QObject* parent)
    : QObject(parent) {}

void ArchiveProcessRunner::cancel() {
  if (!running_) {
    return;
  }
  cancel_requested_ = true;
  active_task_.cancel();
  emit log_line(z7::ui::runtime_support::L(448));
}

void ArchiveProcessRunner::pause() {
  if (!running_) {
    return;
  }
  active_task_.pause();
}

void ArchiveProcessRunner::resume() {
  if (!running_) {
    return;
  }
  active_task_.resume();
}

bool ArchiveProcessRunner::is_running() const {
  return running_;
}

bool ArchiveProcessRunner::supports_pause() const {
  if (!active_task_.valid()) {
    return false;
  }
  const z7::app::ArchiveSessionState state = active_task_.state();
  return state == z7::app::ArchiveSessionState::kRunning ||
         state == z7::app::ArchiveSessionState::kPaused;
}

z7::app::BackendCapabilities ArchiveProcessRunner::backend_capabilities() const {
  return engine_.capabilities();
}

z7::app::BackendCapabilities ArchiveProcessRunner::query_backend_capabilities() {
  return z7::app::ArchiveEngine::query_capabilities();
}

const z7::app::OperationResult& ArchiveProcessRunner::last_result() const {
  return last_result_;
}

const z7::app::OperationOutcome& ArchiveProcessRunner::last_outcome() const {
  return last_outcome_;
}

QString ArchiveProcessRunner::last_operation() const {
  return last_operation_;
}

void ArchiveProcessRunner::set_overwrite_prompt_handler(OverwritePromptHandler handler) {
  overwrite_prompt_handler_ = std::move(handler);
}

void ArchiveProcessRunner::set_prompt_parent_provider(
    PromptParentProvider provider) {
  prompt_parent_provider_ = std::move(provider);
}

void ArchiveProcessRunner::on_task_finished(const z7::app::OperationOutcome& outcome) {
  if (!running_) {
    return;
  }
  last_outcome_ = outcome;
  last_result_ = z7::app::operation_result_from_outcome(outcome);

  if (pending_list_result_) {
    if (const auto list_result = z7::app::outcome_payload_as<z7::app::ListResult>(outcome)) {
      *pending_list_result_ = *list_result;
    } else {
      pending_list_result_->reset();
    }
  }
  if (pending_session_result_) {
    if (const auto session_result =
            z7::app::outcome_payload_as<z7::app::OpenArchiveSessionResult>(outcome)) {
      *pending_session_result_ = *session_result;
    } else {
      pending_session_result_->reset();
    }
  }

  const bool canceled = outcome.status == z7::app::OperationStatus::kCanceled ||
                        z7::app::is_operation_canceled(last_result_.error);
  const bool ok = last_result_.ok && !canceled;
  const int exit_code = last_result_.native_exit_code;
  const int error_domain = static_cast<int>(last_result_.error.domain);
  const QString summary = QString::fromStdString(last_result_.summary);

  running_ = false;
  active_delegate_.reset();
  active_task_ = z7::app::ArchiveSession();
  pending_list_result_.reset();
  pending_session_result_.reset();
  cancel_requested_ = false;

  emit_finished_queued(this, ok, exit_code, error_domain, summary);
}

bool ArchiveProcessRunner::start_operation(
    const QString& operation,
    const QStringList& targets,
    const z7::app::ArchiveRequest& request,
    std::shared_ptr<std::optional<z7::app::ListResult>> out_list_result,
    std::shared_ptr<std::optional<z7::app::OpenArchiveSessionResult>> out_session_result) {
  if (running_) {
    return false;
  }

  cancel_requested_ = false;
  last_operation_ = operation;
  pending_list_result_ = std::move(out_list_result);
  pending_session_result_ = std::move(out_session_result);
  z7::ui::runtime_support::apply_configured_large_pages_mode();

  auto overwrite_prompt = [this](
                              const z7::app::OverwritePrompt& prompt)
      -> std::optional<z7::app::OverwriteDecision> {
    return z7::ui::archive_support::call_on_target_blocking<
        z7::app::OverwriteDecision>(
        this,
        prompt,
        z7::app::OverwriteDecision::kCancel,
        [this](const z7::app::OverwritePrompt& prompt_value) {
          if (overwrite_prompt_handler_) {
            return overwrite_prompt_handler_(prompt_value);
          }
          return show_default_overwrite_prompt(prompt_value);
        });
  };
  auto password_prompt = [this](
                             const z7::app::PasswordPrompt& prompt)
      -> std::optional<z7::app::PasswordReply> {
    return z7::ui::archive_support::call_on_target_blocking<
        z7::app::PasswordReply>(
        this,
        prompt,
        z7::app::PasswordReply{},
        [this](const z7::app::PasswordPrompt& prompt_value) {
          QWidget* parent = nullptr;
          if (prompt_parent_provider_) {
            parent = prompt_parent_provider_();
          }
          return show_default_password_prompt(parent, prompt_value);
        });
  };
  auto choice_prompt = [this](
                           const z7::app::ChoicePrompt& prompt)
      -> std::optional<z7::app::ChoiceReply> {
    return z7::ui::archive_support::call_on_target_blocking<
        z7::app::ChoiceReply>(
        this,
        prompt,
        z7::app::ChoiceReply{},
        [](const z7::app::ChoicePrompt& prompt_value) {
          return show_default_choice_prompt(prompt_value);
        });
  };
  auto memory_limit_prompt = [this](
                                 const z7::app::MemoryLimitPrompt& prompt)
      -> std::optional<z7::app::MemoryLimitReply> {
    return z7::ui::archive_support::call_on_target_blocking<
        z7::app::MemoryLimitReply>(
        this,
        prompt,
        z7::app::MemoryLimitReply{},
        [](const z7::app::MemoryLimitPrompt& prompt_value) {
          return show_default_memory_limit_prompt(prompt_value);
        });
  };

  active_delegate_ = std::make_shared<RunnerDelegate>(this,
                                                       std::move(overwrite_prompt),
                                                       std::move(password_prompt),
                                                       std::move(choice_prompt),
                                                       std::move(memory_limit_prompt));
  running_ = true;
  active_task_ = engine_.start(request, active_delegate_);
  if (!active_task_.valid()) {
    running_ = false;
    active_delegate_.reset();
    pending_list_result_.reset();
    pending_session_result_.reset();
    return finish_immediately(z7::app::make_backend_unavailable_result());
  }

  emit started(QStringLiteral("native_session"), operation, targets);
  emit progress_changed(-1);
  return true;
}

bool ArchiveProcessRunner::finish_immediately(const z7::app::OperationResult& result) {
  last_result_ = result;
  last_outcome_ = z7::app::archive_session_helpers::make_outcome(
      result,
      z7::app::OperationPayload{std::monostate{}});
  const bool canceled = z7::app::is_operation_canceled(result.error);
  const bool ok = result.ok && !canceled;
  const int exit_code = result.native_exit_code;
  const int error_domain = static_cast<int>(result.error.domain);
  const QString summary = QString::fromStdString(result.summary);
  running_ = false;
  active_delegate_.reset();
  active_task_ = z7::app::ArchiveSession();
  pending_list_result_.reset();
  pending_session_result_.reset();
  cancel_requested_ = false;
  emit_finished_queued(this, ok, exit_code, error_domain, summary);
  return true;
}

}  // namespace z7::ui::filemanager
