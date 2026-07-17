// src/ui/filemanager/src/archive_process_runner/core.cpp
// Role: Lifecycle and execution plumbing for archive process runner.

#include <utility>
#include <variant>

#include "archive_delegate_qt.h"
#include "archive_error.h"
#include "archive_failure_messages.h"
#include "archive_format.h"
#include "archive_process_runner.h"
#include "archive_session_helpers.h"
#include "archive_string_codec_qt.h"
#include "core_prompts.h"
#include "helpers.h"
#include "large_pages_settings.h"
#include "official_lang_catalog.h"
#include "portable_settings.h"

namespace z7::ui::filemanager {

    using namespace runner_helpers;

    namespace {

        void emit_stage_and_log(ArchiveProcessRunner* owner, z7::app::ArchiveLog const& log) {
            if (owner == nullptr) {
                return;
            }
            QString const stage_text = z7::ui::archive_support::stage_label_for(log.stage);
            QString const log_line = z7::ui::archive_support::from_local8_string(log.message);
            if (!stage_text.isEmpty()) {
                emit owner->stage_changed(stage_text);
            }
            if (!log_line.isEmpty()) {
                QString const display_log_line = log.channel == z7::app::OutputChannel::kStdErr
                                                   ? z7::ui::runtime_support::localize_archive_failure_message(log_line)
                                                   : log_line;
                if (log.channel == z7::app::OutputChannel::kStdErr) {
                    emit owner->failure_message(display_log_line);
                }
                emit owner->log_line(display_log_line);
            }
        }

        void emit_progress_signals(ArchiveProcessRunner* owner, z7::app::ProgressSnapshot const& progress) {
            if (owner == nullptr) {
                return;
            }
            QString const stage_text = z7::ui::archive_support::stage_label_for(progress.stage);
            QString const log_line = z7::ui::archive_support::from_local8_string(progress.message);
            QString const current_path = z7::ui::archive_support::from_local8_string(progress.current_path);
            if (!stage_text.isEmpty()) {
                emit owner->stage_changed(stage_text);
            }
            if (!log_line.isEmpty()) {
                emit owner->log_line(log_line);
            }
            if (progress.percent >= 0 && progress.percent <= 100) {
                emit owner->progress_changed(progress.percent);
            }
            emit owner->detailed_progress_changed(
                progress.totals_known,
                static_cast<quint64>(progress.total_bytes),
                static_cast<quint64>(progress.completed_bytes),
                static_cast<quint64>(progress.total_files),
                static_cast<quint64>(progress.completed_files),
                static_cast<quint64>(progress.error_count),
                progress.ratio_info.has_value() && progress.ratio_info->input_size_known,
                progress.ratio_info.has_value() ? static_cast<quint64>(progress.ratio_info->input_size) : 0,
                progress.ratio_info.has_value() && progress.ratio_info->output_size_known,
                progress.ratio_info.has_value() ? static_cast<quint64>(progress.ratio_info->output_size) : 0,
                progress.ratio_info.has_value() ? progress.ratio_info->compressing_mode : true,
                current_path);
        }

        void emit_finished_queued(
            ArchiveProcessRunner* owner, bool ok, int exit_code, int error_domain, QString const& summary) {
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

        bool is_password_failure(z7::app::OperationOutcome const& outcome) {
            return outcome.status == z7::app::OperationStatus::kWrongPassword
                || outcome.error_domain == z7::app::ArchiveErrorDomain::kPassword
                || outcome.error.domain == z7::app::ArchiveErrorDomain::kPassword;
        }

        z7::app::OperationOutcome make_canceled_outcome(std::string summary) {
            if (summary.empty()) {
                summary = "Operation canceled.";
            }
            z7::app::OperationResult const result =
                z7::app::make_immediate_result(5, z7::app::ArchiveErrorDomain::kCanceled, summary);
            z7::app::OperationOutcome outcome;
            outcome.status = z7::app::OperationStatus::kCanceled;
            outcome.error_domain = z7::app::ArchiveErrorDomain::kCanceled;
            outcome.native_code = result.native_exit_code;
            outcome.summary = result.summary;
            outcome.error = result.error;
            outcome.native_execution = result.native_execution;
            outcome.ok = false;
            outcome.payload = std::monostate{};
            if (!summary.empty()) {
                outcome.diagnostics.push_back(summary);
            }
            return outcome;
        }

        std::string password_prompt_archive_path(z7::app::ArchiveRequest const& request,
                                                 z7::app::OperationOutcome const& outcome) {
            if (auto const* extract = std::get_if<z7::app::ExtractRequest>(&request.payload)) {
                if (!extract->archive_path.empty()) {
                    return extract->archive_path;
                }
                if (!extract->archive_paths.empty()) {
                    return extract->archive_paths.front();
                }
            }
            if (auto const* test = std::get_if<z7::app::TestRequest>(&request.payload)) {
                if (!test->archive_path.empty()) {
                    return test->archive_path;
                }
                if (!test->archive_paths.empty()) {
                    return test->archive_paths.front();
                }
            }
            if (auto const* add = std::get_if<z7::app::AddRequest>(&request.payload)) {
                return add->archive_path;
            }
            if (auto const* del = std::get_if<z7::app::DeleteRequest>(&request.payload)) {
                return del->archive_path;
            }
            if (auto const* props = std::get_if<z7::app::ArchivePropertiesRequest>(&request.payload)) {
                return props->archive_path;
            }
            if (auto const* open = std::get_if<z7::app::OpenArchiveRequest>(&request.payload)) {
                return open->archive_path;
            }
            if (auto const* open = std::get_if<z7::app::OpenArchiveFromPathRequest>(&request.payload)) {
                return open->archive_path;
            }
            if (auto const* open = std::get_if<z7::app::OpenArchiveFromParentRequest>(&request.payload)) {
                return open->display_path_hint;
            }
            if (auto const* list = std::get_if<z7::app::ListRequest>(&request.payload)) {
                return list->archive_path;
            }
            if (auto const* rename = std::get_if<z7::app::RenameRequest>(&request.payload)) {
                return rename->archive_path;
            }
            if (auto const* comment = std::get_if<z7::app::ArchiveCommentRequest>(&request.payload)) {
                return comment->archive_path;
            }
            if (auto const* entry = std::get_if<z7::app::GetEntryInfoRequest>(&request.payload)) {
                return entry->archive_path;
            }
            if (!outcome.summary.empty()) {
                return outcome.summary;
            }
            return {};
        }

        bool store_retry_password_in_request(z7::app::ArchiveRequest* request, std::string password) {
            if (request == nullptr) {
                return false;
            }
            if (auto* extract = std::get_if<z7::app::ExtractRequest>(&request->payload)) {
                extract->password = std::move(password);
                return true;
            }
            if (auto* add = std::get_if<z7::app::AddRequest>(&request->payload)) {
                add->password = std::move(password);
                return true;
            }
            if (auto* del = std::get_if<z7::app::DeleteRequest>(&request->payload)) {
                del->password = std::move(password);
                return true;
            }
            return false;
        }

        class RunnerDelegate final : public z7::ui::archive_support::OwnerRelayDelegate<ArchiveProcessRunner> {
        public:
            using OverwritePromptFn =
                std::function<std::optional<z7::app::OverwriteDecision>(z7::app::OverwritePrompt const&)>;
            using PasswordPromptFn =
                std::function<std::optional<z7::app::PasswordReply>(z7::app::PasswordPrompt const&)>;
            using ChoicePromptFn = std::function<std::optional<z7::app::ChoiceReply>(z7::app::ChoicePrompt const&)>;
            using MemoryLimitPromptFn =
                std::function<std::optional<z7::app::MemoryLimitReply>(z7::app::MemoryLimitPrompt const&)>;

            RunnerDelegate(ArchiveProcessRunner* owner,
                           OverwritePromptFn overwrite_prompt,
                           PasswordPromptFn password_prompt,
                           ChoicePromptFn choice_prompt,
                           MemoryLimitPromptFn memory_limit_prompt) :
                z7::ui::archive_support::OwnerRelayDelegate<ArchiveProcessRunner>(
                    owner,
                    [owner](z7::app::OperationOutcome const& outcome) { owner->on_task_finished(outcome); },
                    nullptr,
                    z7::ui::archive_support::MissingTargetPolicy::kDrop),
                overwrite_prompt_(std::move(overwrite_prompt)),
                password_prompt_(std::move(password_prompt)),
                choice_prompt_(std::move(choice_prompt)),
                memory_limit_prompt_(std::move(memory_limit_prompt)) {}

            void on_log(z7::app::ArchiveLog const& log) override {
                z7::app::ArchiveLog const log_copy = log;
                post_to_owner([log_copy](ArchiveProcessRunner* owner) { emit_stage_and_log(owner, log_copy); });
            }

            void on_progress(z7::app::ProgressSnapshot const& progress) override {
                z7::app::ProgressSnapshot progress_copy = progress;
                post_to_owner([progress_copy = std::move(progress_copy)](ArchiveProcessRunner* owner) {
                    emit_progress_signals(owner, progress_copy);
                });
            }

            std::optional<z7::app::OverwriteDecision>
            request_overwrite(z7::app::OverwritePrompt const& prompt) override {
                if (!overwrite_prompt_) {
                    return std::nullopt;
                }
                return overwrite_prompt_(prompt);
            }

            std::optional<z7::app::PasswordReply> request_password(z7::app::PasswordPrompt const& prompt) override {
                if (!password_prompt_) {
                    return std::nullopt;
                }
                return password_prompt_(prompt);
            }

            std::optional<z7::app::ChoiceReply> request_choice(z7::app::ChoicePrompt const& prompt) override {
                if (!choice_prompt_) {
                    return std::nullopt;
                }
                return choice_prompt_(prompt);
            }

            std::optional<z7::app::MemoryLimitReply>
            request_memory_limit(z7::app::MemoryLimitPrompt const& prompt) override {
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

    } // namespace

    ArchiveProcessRunner::ArchiveProcessRunner(QObject* parent) : QObject(parent) {}

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
        z7::app::ArchiveSessionState const state = active_task_.state();
        return state == z7::app::ArchiveSessionState::kRunning || state == z7::app::ArchiveSessionState::kPaused;
    }

    z7::app::BackendCapabilities ArchiveProcessRunner::backend_capabilities() const {
        return engine_.capabilities();
    }

    z7::app::BackendCapabilities ArchiveProcessRunner::query_backend_capabilities() {
        return z7::app::ArchiveEngine::query_capabilities();
    }

    z7::app::OperationResult const& ArchiveProcessRunner::last_result() const {
        return last_result_;
    }

    z7::app::OperationOutcome const& ArchiveProcessRunner::last_outcome() const {
        return last_outcome_;
    }

    QString ArchiveProcessRunner::last_operation() const {
        return last_operation_;
    }

    void ArchiveProcessRunner::set_overwrite_prompt_handler(OverwritePromptHandler handler) {
        overwrite_prompt_handler_ = std::move(handler);
    }

    void ArchiveProcessRunner::set_choice_prompt_handler(ChoicePromptHandler handler) {
        choice_prompt_handler_ = std::move(handler);
    }

    void ArchiveProcessRunner::set_prompt_parent_provider(PromptParentProvider provider) {
        prompt_parent_provider_ = std::move(provider);
    }

    void ArchiveProcessRunner::on_task_finished(z7::app::OperationOutcome const& outcome) {
        if (!running_) {
            return;
        }

        if (is_password_failure(outcome) && active_request_.has_value() && !cancel_requested_) {
            active_delegate_.reset();
            active_task_ = z7::app::ArchiveSession();
            running_ = false;

            if (password_prompt_canceled_) {
                finalize_outcome(make_canceled_outcome("Operation canceled."));
                return;
            }

            z7::app::PasswordPrompt prompt;
            prompt.archive_path = password_prompt_archive_path(*active_request_, outcome);
            prompt.reason = "wrong_password";
            prompt.reason_kind = z7::app::PasswordPromptReason::kWrongPassword;
            QWidget* parent = nullptr;
            if (prompt_parent_provider_) {
                parent = prompt_parent_provider_();
            }
            z7::app::PasswordReply const reply = show_default_password_prompt(parent, prompt);
            if (reply.kind != z7::app::PasswordReplyKind::kProvide) {
                finalize_outcome(make_canceled_outcome("Operation canceled."));
                return;
            }

            password_prompt_canceled_ = false;
            if (!store_retry_password_in_request(&*active_request_, reply.password)) {
                retry_next_password_ = reply.password;
            }
            start_active_request_attempt();
            return;
        }

        finalize_outcome(outcome);
    }

    void ArchiveProcessRunner::finalize_outcome(z7::app::OperationOutcome const& outcome) {
        last_outcome_ = outcome;
        last_result_ = z7::app::operation_result_from_outcome(outcome);

        if (pending_list_result_) {
            if (auto const list_result = z7::app::outcome_payload_as<z7::app::ListResult>(outcome)) {
                *pending_list_result_ = *list_result;
            } else {
                pending_list_result_->reset();
            }
        }
        if (pending_session_result_) {
            if (auto const session_result = z7::app::outcome_payload_as<z7::app::OpenArchiveSessionResult>(outcome)) {
                *pending_session_result_ = *session_result;
            } else {
                pending_session_result_->reset();
            }
        }
        if (pending_parent_session_result_) {
            if (auto const session_result =
                    z7::app::outcome_payload_as<z7::app::OpenArchiveFromParentResult>(outcome)) {
                *pending_parent_session_result_ = *session_result;
            } else {
                pending_parent_session_result_->reset();
            }
        }

        bool const canceled =
            outcome.status == z7::app::OperationStatus::kCanceled || z7::app::is_operation_canceled(last_result_.error);
        bool const ok = last_result_.ok && !canceled;
        int const exit_code = last_result_.native_exit_code;
        int const error_domain = static_cast<int>(last_result_.error.domain);
        QString const summary = QString::fromStdString(last_result_.summary);

        running_ = false;
        active_delegate_.reset();
        active_task_ = z7::app::ArchiveSession();
        pending_list_result_.reset();
        pending_session_result_.reset();
        pending_parent_session_result_.reset();
        active_request_.reset();
        active_targets_.clear();
        retry_next_password_.reset();
        password_prompt_canceled_ = false;
        cancel_requested_ = false;

        emit_finished_queued(this, ok, exit_code, error_domain, summary);
    }

    bool ArchiveProcessRunner::start_operation(
        QString const& operation,
        QStringList const& targets,
        z7::app::ArchiveRequest request,
        std::shared_ptr<std::optional<z7::app::ListResult>> out_list_result,
        std::shared_ptr<std::optional<z7::app::OpenArchiveSessionResult>> out_session_result,
        std::shared_ptr<std::optional<z7::app::OpenArchiveFromParentResult>> out_parent_session_result) {
        if (running_) {
            return false;
        }

        cancel_requested_ = false;
        password_prompt_canceled_ = false;
        retry_next_password_.reset();
        last_operation_ = operation;
        active_targets_ = targets;
        active_request_ = std::move(request);
        pending_list_result_ = std::move(out_list_result);
        pending_session_result_ = std::move(out_session_result);
        pending_parent_session_result_ = std::move(out_parent_session_result);
        z7::ui::runtime_support::apply_configured_large_pages_mode();

        return start_active_request_attempt();
    }

    bool ArchiveProcessRunner::start_active_request_attempt() {
        if (!active_request_.has_value()) {
            return finish_immediately(z7::app::make_backend_unavailable_result());
        }

        auto overwrite_prompt =
            [this](z7::app::OverwritePrompt const& prompt) -> std::optional<z7::app::OverwriteDecision> {
            return z7::ui::archive_support::call_on_target_blocking<z7::app::OverwriteDecision>(
                this,
                prompt,
                z7::app::OverwriteDecision::kCancel,
                [this](z7::app::OverwritePrompt const& prompt_value) {
                    if (overwrite_prompt_handler_) {
                        return overwrite_prompt_handler_(prompt_value);
                    }
                    return show_default_overwrite_prompt(prompt_value);
                });
        };
        auto password_prompt = [this](z7::app::PasswordPrompt const& prompt) -> std::optional<z7::app::PasswordReply> {
            return z7::ui::archive_support::call_on_target_blocking<z7::app::PasswordReply>(
                this, prompt, z7::app::PasswordReply{}, [this](z7::app::PasswordPrompt const& prompt_value) {
                    if (retry_next_password_.has_value()) {
                        z7::app::PasswordReply retry_reply;
                        retry_reply.kind = z7::app::PasswordReplyKind::kProvide;
                        retry_reply.password = std::move(*retry_next_password_);
                        retry_next_password_.reset();
                        password_prompt_canceled_ = false;
                        return retry_reply;
                    }

                    QWidget* parent = nullptr;
                    if (prompt_parent_provider_) {
                        parent = prompt_parent_provider_();
                    }
                    z7::app::PasswordReply reply = show_default_password_prompt(parent, prompt_value);
                    password_prompt_canceled_ = reply.kind != z7::app::PasswordReplyKind::kProvide;
                    return reply;
                });
        };
        auto choice_prompt = [this](z7::app::ChoicePrompt const& prompt) -> std::optional<z7::app::ChoiceReply> {
            return z7::ui::archive_support::call_on_target_blocking<z7::app::ChoiceReply>(
                this, prompt, z7::app::ChoiceReply{}, [this](z7::app::ChoicePrompt const& prompt_value) {
                    if (choice_prompt_handler_) {
                        return choice_prompt_handler_(prompt_value);
                    }
                    return show_default_choice_prompt(prompt_value);
                });
        };
        auto memory_limit_prompt =
            [this](z7::app::MemoryLimitPrompt const& prompt) -> std::optional<z7::app::MemoryLimitReply> {
            return z7::ui::archive_support::call_on_target_blocking<z7::app::MemoryLimitReply>(
                this, prompt, z7::app::MemoryLimitReply{}, [](z7::app::MemoryLimitPrompt const& prompt_value) {
                    return show_default_memory_limit_prompt(prompt_value);
                });
        };

        active_delegate_ = std::make_shared<RunnerDelegate>(this,
                                                            std::move(overwrite_prompt),
                                                            std::move(password_prompt),
                                                            std::move(choice_prompt),
                                                            std::move(memory_limit_prompt));
        running_ = true;
        active_task_ = engine_.start(*active_request_, active_delegate_);
        if (!active_task_.valid()) {
            running_ = false;
            active_delegate_.reset();
            return finish_immediately(z7::app::make_backend_unavailable_result());
        }

        emit started(QStringLiteral("native_session"), last_operation_, active_targets_);
        emit progress_changed(-1);
        return true;
    }

    bool ArchiveProcessRunner::finish_immediately(z7::app::OperationResult const& result) {
        finalize_outcome(
            z7::app::archive_session_helpers::make_outcome(result, z7::app::OperationPayload{std::monostate{}}));
        return true;
    }

} // namespace z7::ui::filemanager
