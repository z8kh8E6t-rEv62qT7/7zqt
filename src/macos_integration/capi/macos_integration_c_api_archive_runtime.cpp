#include <condition_variable>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#include "archive_delegate_qt.h"
#include "internal.h"
#include "large_pages_settings.h"

namespace z7::macos_integration::capi_internal
{
    namespace
    {

        class WaitableArchiveDelegate final : public z7::app::IArchiveDelegate
        {
        public:
            explicit WaitableArchiveDelegate(std::shared_ptr<z7::app::IArchiveDelegate> forward) :
                forward_(std::move(forward))
            {
            }

            std::optional<z7::app::OverwriteDecision> request_overwrite(z7::app::OverwritePrompt const& prompt) override
            {
                if (forward_)
                {
                    return forward_->request_overwrite(prompt);
                }
                return std::nullopt;
            }

            std::optional<z7::app::PasswordReply> request_password(z7::app::PasswordPrompt const& prompt) override
            {
                if (forward_)
                {
                    return forward_->request_password(prompt);
                }
                return std::nullopt;
            }

            std::optional<z7::app::ChoiceReply> request_choice(z7::app::ChoicePrompt const& prompt) override
            {
                if (forward_)
                {
                    return forward_->request_choice(prompt);
                }
                return std::nullopt;
            }

            std::optional<z7::app::MemoryLimitReply>
            request_memory_limit(z7::app::MemoryLimitPrompt const& prompt) override
            {
                if (forward_)
                {
                    return forward_->request_memory_limit(prompt);
                }
                return std::nullopt;
            }

            void on_lifecycle(z7::app::OperationStage stage, std::string_view message) override
            {
                if (forward_)
                {
                    forward_->on_lifecycle(stage, message);
                }
            }

            void on_log(z7::app::ArchiveLog const& log) override
            {
                if (forward_)
                {
                    forward_->on_log(log);
                }
            }

            void on_progress(z7::app::ProgressSnapshot const& progress) override
            {
                if (forward_)
                {
                    forward_->on_progress(progress);
                }
            }

            bool on_list_entries_batch(std::vector<z7::app::ArchiveListEntry>&& batch) override
            {
                if (forward_)
                {
                    return forward_->on_list_entries_batch(std::move(batch));
                }
                return true;
            }

            void on_finished(z7::app::OperationOutcome const& outcome) override
            {
                if (forward_)
                {
                    forward_->on_finished(outcome);
                }
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    outcome_ = outcome;
                    done_ = true;
                }
                cv_.notify_all();
            }

            z7::app::OperationOutcome await_outcome()
            {
                std::unique_lock<std::mutex> lock(mutex_);
                cv_.wait(lock, [this]() { return done_; });
                return outcome_;
            }

        private:
            std::shared_ptr<z7::app::IArchiveDelegate> forward_;
            std::mutex mutex_;
            std::condition_variable cv_;
            z7::app::OperationOutcome outcome_;
            bool done_ = false;
        };

        z7::app::OperationOutcome outcome_from_result(z7::app::OperationResult const& result)
        {
            z7::app::OperationOutcome outcome;
            if (z7::app::is_operation_canceled(result.error))
            {
                outcome.status = z7::app::OperationStatus::kCanceled;
            }
            else if (result.error.domain == z7::app::ArchiveErrorDomain::kPartialSuccess)
            {
                outcome.status = z7::app::OperationStatus::kPartialSuccess;
            }
            else if (result.error.domain == z7::app::ArchiveErrorDomain::kPassword)
            {
                outcome.status = z7::app::OperationStatus::kWrongPassword;
            }
            else if (result.ok && result.error.domain == z7::app::ArchiveErrorDomain::kNone)
            {
                outcome.status = z7::app::OperationStatus::kSuccess;
            }
            else
            {
                outcome.status = z7::app::OperationStatus::kFailed;
            }
            outcome.error_domain = result.error.domain;
            outcome.native_code = result.native_exit_code;
            outcome.summary = result.summary;
            outcome.error = result.error;
            outcome.native_execution = result.native_execution;
            outcome.ok = result.ok;
            return outcome;
        }

    } // namespace

    bool should_report_canceled(z7::app::OperationOutcome const& outcome, bool cancel_requested)
    {
        if (outcome.status == z7::app::OperationStatus::kCanceled
            || outcome.error.domain == z7::app::ArchiveErrorDomain::kCanceled
            || outcome.native_execution.termination_reason == z7::app::NativeTerminationReason::kCanceled)
        {
            return true;
        }
        return cancel_requested
            && outcome.status == z7::app::OperationStatus::kFailed
            && outcome.native_execution.termination_reason == z7::app::NativeTerminationReason::kAborted;
    }

    QString canceled_message(z7::app::OperationOutcome const& outcome)
    {
        QString const summary = z7::ui::archive_support::from_utf8_string(outcome.summary).trimmed();
        if (!summary.isEmpty())
        {
            return summary;
        }
        return QStringLiteral("Operation canceled.");
    }

    z7::app::OperationOutcome run_archive_request_sync(z7::app::ArchiveRequest const& request,
                                                       std::shared_ptr<AsyncTaskState> const& task_state,
                                                       std::shared_ptr<z7::app::IArchiveDelegate> delegate)
    {
        auto waitable = std::make_shared<WaitableArchiveDelegate>(std::move(delegate));
        z7::app::ArchiveEngine engine;
        z7::ui::runtime_support::apply_configured_large_pages_mode();
        z7::app::ArchiveSession session = engine.start(request, waitable);
        if (!session.valid())
        {
            clear_active_archive_session(task_state);
            return outcome_from_result(z7::app::make_backend_unavailable_result());
        }
        set_active_archive_session(task_state, session);
        z7::app::OperationOutcome const outcome = waitable->await_outcome();
        clear_active_archive_session(task_state);
        return outcome;
    }

} // namespace z7::macos_integration::capi_internal
