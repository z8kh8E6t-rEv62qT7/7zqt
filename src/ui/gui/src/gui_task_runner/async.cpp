// src/ui/gui/src/gui_task_runner/async.cpp
// Role: Unified sequenced task runner for async and blocking GUI archive flows.

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QMessageBox>
#include <QPointer>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include "../gui_app_controller/helpers.h"
#include "../gui_task_runner_helpers.h"
#include "archive_error.h"
#include "archive_failure_messages.h"
#include "archive_string_codec_qt.h"
#include "delayed_progress_dialog_presenter.h"
#include "gui_task_progress_dialog.h"
#include "hash_result_dialog.h"
#include "internal.h"
#include "large_pages_settings.h"
#include "official_lang_catalog.h"
#include "shared.h"
#include "task_result_presentation.h"

namespace z7::ui::gui {

    using namespace gui_task_runner_shared;

    namespace {

        template <typename TResult, typename WorkFn, typename FinishFn>
        void run_background_task(QObject* owner, WorkFn&& work, FinishFn&& on_finished) {
            if (owner == nullptr) {
                return;
            }
            auto* watcher = new QFutureWatcher<TResult>(owner);
            QObject::connect(watcher,
                             &QFutureWatcher<TResult>::finished,
                             owner,
                             [watcher, on_finished = std::forward<FinishFn>(on_finished)]() mutable {
                                 TResult const result = watcher->result();
                                 watcher->deleteLater();
                                 on_finished(result);
                             });
            watcher->setFuture(QtConcurrent::run(std::forward<WorkFn>(work)));
        }

        template <typename TResult>
        void finish_run(SequencedTaskRunSpec run_spec, TResult result) {
            if (run_spec.on_finished) {
                run_spec.on_finished(std::move(result));
            }
        }

        QString normalized_modal_title(GuiTaskSpec const& spec, QString const& requested_title) {
            QString const header = progress_header_for_spec(spec, requested_title);
            return header.trimmed().isEmpty() ? requested_title : header;
        }

        enum class ArchiveMemberRunKind {
            kExport,
            kTest,
            kHash
        };

        QString default_member_title(ArchiveMemberRunKind run_kind) {
            switch (run_kind) {
                case ArchiveMemberRunKind::kHash:
                    return QStringLiteral("7zG Hash");
                case ArchiveMemberRunKind::kTest:
                    return QStringLiteral("7zG Test");
                case ArchiveMemberRunKind::kExport:
                default:
                    return QStringLiteral("7zG Copy");
            }
        }

        bool is_password_failure(z7::app::OperationOutcome const& outcome) {
            return outcome.status == z7::app::OperationStatus::kWrongPassword
                || outcome.error_domain == z7::app::ArchiveErrorDomain::kPassword
                || outcome.error.domain == z7::app::ArchiveErrorDomain::kPassword;
        }

        QString normalized_absolute_path(QString path) {
            path = QDir::fromNativeSeparators(path.trimmed());
            return path.isEmpty() ? QString() : QFileInfo(path).absoluteFilePath();
        }

        void remove_redundant_test_archive_context(QStringList* messages, QString const& archive_context) {
            if (messages == nullptr || archive_context.isEmpty()) {
                return;
            }
            QString const normalized_context = normalized_absolute_path(archive_context);
            messages->removeIf([&normalized_context](QString const& message) {
                return !message.contains(QLatin1Char('\n'))
                    && normalized_absolute_path(message) == normalized_context;
            });
        }

        QStringList normalized_failure_lines(QString const& message) {
            QString normalized = message;
            normalized.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
            normalized.replace(QLatin1Char('\r'), QLatin1Char('\n'));
            QStringList lines;
            for (QString const& line : normalized.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
                QString const trimmed = line.trimmed();
                if (!trimmed.isEmpty()) {
                    lines.push_back(trimmed);
                }
            }
            return lines;
        }

        bool final_failure_is_fully_presented(GuiTaskRunResult const& out) {
            if (!out.archive_open_diagnostic_presented || out.result.error.message.empty()) {
                return false;
            }

            QStringList presented_lines;
            for (QString const& message : out.result_messages) {
                presented_lines.append(normalized_failure_lines(message));
            }

            QString const localized_error = z7::ui::runtime_support::localize_archive_failure_message(
                z7::ui::archive_support::from_native_string(out.result.error.message));
            QStringList required_lines = normalized_failure_lines(localized_error);
            required_lines.removeIf([](QString const& line) {
                QString header = line;
                if (header.endsWith(QLatin1Char(':'))) {
                    header.chop(1);
                }
                return header.trimmed().compare(QStringLiteral("Archive open error"), Qt::CaseInsensitive) == 0;
            });
            if (required_lines.isEmpty()) {
                return false;
            }
            return std::all_of(required_lines.cbegin(), required_lines.cend(), [&](QString const& required) {
                return presented_lines.contains(required);
            });
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
            if (auto const* open = std::get_if<z7::app::OpenArchiveRequest>(&request.payload)) {
                return open->archive_path;
            }
            if (auto const* open = std::get_if<z7::app::OpenArchiveFromPathRequest>(&request.payload)) {
                return open->archive_path;
            }
            if (auto const* open = std::get_if<z7::app::OpenArchiveFromParentRequest>(&request.payload)) {
                return open->display_path_hint;
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

        class ArchiveTaskAsyncSequencer : public QObject {
        public:
            using CompletionHandler = std::function<void(ArchiveTaskAsyncSequencer*, z7::app::OperationOutcome const&)>;

            explicit ArchiveTaskAsyncSequencer(SequencedTaskRunSpec run_spec) :
                callback_(std::move(run_spec.on_finished)),
                cancel_requested_(std::move(run_spec.cancel_requested)),
                test_mode_(run_spec.test_mode),
                hash_mode_(run_spec.hash_mode),
                redundant_test_archive_context_(std::move(run_spec.redundant_test_archive_context)),
                background_mode_(std::make_shared<z7::ui::common::TaskBackgroundModeController>()) {
                if (run_spec.dialog != nullptr) {
                    dialog_ = run_spec.dialog;
                } else {
                    auto* owned_dialog = new TaskProgressDialog(run_spec.parent);
                    owned_dialog->setAttribute(Qt::WA_DeleteOnClose);
                    dialog_ = owned_dialog;
                }

                if (dialog_.isNull()) {
                    return;
                }

                progress_presenter_.set_dialog(dialog_.data());
                prepare_progress_dialog(dialog_.data(), run_spec.title, run_spec.test_mode);
                background_connection_ =
                    QObject::connect(dialog_.data(),
                                     &z7::ui::runtime_support::TaskProgressDialogBase::background_requested,
                                     [background_mode = background_mode_](bool backgrounded) {
                                         background_mode->set_backgrounded(backgrounded);
                                     });
            }

            ~ArchiveTaskAsyncSequencer() override {
                release_active_request();
                if (background_connection_) {
                    QObject::disconnect(background_connection_);
                }
                background_mode_->restore();
            }

            bool has_dialog() const { return !dialog_.isNull(); }

            void show_dialog() { progress_presenter_.show_now(); }

            z7::ui::runtime_support::TaskProgressDialogBase* ensure_dialog_visible_for_password_prompt() {
                if (dialog_.isNull()) {
                    return nullptr;
                }
                show_dialog();
                dialog_->raise();
                dialog_->activateWindow();
                QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
                return dialog_.data();
            }

            void schedule_delayed_dialog_show() {
                if ((!test_mode_ && !hash_mode_) || dialog_.isNull()) {
                    show_dialog();
                    return;
                }
                if (progress_presenter_.is_pending() || progress_presenter_.was_shown()) {
                    return;
                }
                progress_presenter_.schedule(this, z7::ui::runtime_support::kOriginal7ZipProgressDialogDelayMs);
            }

            void set_pause_available(bool available) {
                if (!dialog_.isNull()) {
                    dialog_->set_pause_available(available);
                }
            }

            void append_session_token(z7::app::ArchiveSessionToken const& token) {
                if (token.is_valid()) {
                    tokens_.push_back(token);
                }
            }

            QVector<z7::app::ArchiveSessionToken> const& tokens() const { return tokens_; }

            GuiTaskRunResult* result() { return &out_; }

            SharedTaskCancellation cancel_requested() const { return cancel_requested_; }

            bool cancel_already_requested() const { return is_export_canceled(cancel_requested_); }

            void fail_immediate(int code, z7::app::ArchiveErrorDomain domain, QString const& message) {
                out_.result =
                    z7::app::make_immediate_result(code, domain, z7::ui::archive_support::to_utf8_string(message));
            }

            void set_result_from_outcome(z7::app::OperationOutcome const& outcome) {
                out_.result = z7::app::operation_result_from_outcome(outcome);
            }

            void start_request(z7::app::ArchiveRequest const& request, CompletionHandler on_complete) {
                auto request_copy = std::make_shared<z7::app::ArchiveRequest>(request);
                auto completion = std::make_shared<CompletionHandler>(std::move(on_complete));
                start_request_attempt(std::move(request_copy), std::move(completion));
            }

            void start_request_attempt(std::shared_ptr<z7::app::ArchiveRequest> request,
                                       std::shared_ptr<CompletionHandler> on_complete) {
                if (finished_) {
                    return;
                }
                if (dialog_.isNull()) {
                    if (on_complete && *on_complete) {
                        (*on_complete)(this, z7::app::make_backend_unavailable_outcome());
                    }
                    return;
                }

                release_active_request();
                active_completion_ = [this, request, on_complete](ArchiveTaskAsyncSequencer*,
                                                                  z7::app::OperationOutcome const& outcome) mutable {
                    handle_request_outcome(std::move(request), std::move(on_complete), outcome);
                };
                active_delegate_ = make_progress_dialog_delegate(
                    dialog_.data(),
                    &out_,
                    [this](z7::app::OperationOutcome const& outcome) {
                        CompletionHandler completion = std::move(active_completion_);
                        release_active_request();
                        if (completion) {
                            completion(this, outcome);
                        }
                    },
                    [this]() { return ensure_dialog_visible_for_password_prompt(); },
                    password_retry_state_);

                z7::ui::runtime_support::apply_configured_large_pages_mode();
                active_session_ = active_engine_.start(*request, active_delegate_);
                if (!active_session_.valid()) {
                    active_delegate_.reset();
                    CompletionHandler completion = std::move(active_completion_);
                    if (completion) {
                        completion(this, z7::app::make_backend_unavailable_outcome());
                    }
                    return;
                }

                active_bindings_ = bind_session_controls(dialog_.data(), active_session_, cancel_requested_);
            }

            void close_tokens_then(std::function<void(ArchiveTaskAsyncSequencer*)> after_close) {
                after_close_ = std::move(after_close);
                close_next_token();
            }

            template <typename TResult, typename FinishFn>
            void start_background_task(std::function<TResult()> work, FinishFn&& on_finished) {
                if (finished_) {
                    return;
                }
                set_pause_available(false);
                run_background_task<TResult>(
                    this,
                    std::move(work),
                    [this, on_finished = std::forward<FinishFn>(on_finished)](TResult const& result) mutable {
                        set_pause_available(true);
                        on_finished(this, result);
                    });
            }

            void finalize() {
                if (finished_) {
                    return;
                }
                finished_ = true;
                release_active_request();
                progress_presenter_.cancel_pending();
                if (test_mode_) {
                    remove_redundant_test_archive_context(
                        &out_.result_messages, redundant_test_archive_context_);
                }
                z7::ui::runtime_support::TaskResultPresentation presentation =
                    z7::ui::runtime_support::classify_task_result(out_.result.ok,
                                                                  out_.result.error.domain,
                                                                  !out_.result_messages.isEmpty());
                if (final_failure_is_fully_presented(out_)
                    && presentation == z7::ui::runtime_support::TaskResultPresentation::kFinalErrorThenMessages) {
                    presentation = z7::ui::runtime_support::TaskResultPresentation::kMessages;
                    out_.final_error_presented = true;
                }
                bool const delayed_result_success =
                    (test_mode_ || hash_mode_) && out_.result.ok && out_.result.native_exit_code == 0;
                bool const show_result_messages = should_show_result_messages(presentation);
                bool const dialog_was_shown = progress_presenter_.was_shown();
                if (!dialog_.isNull()) {
                    dialog_->set_running(false);
                    if (!show_result_messages && (!delayed_result_success || !dialog_was_shown)) {
                        dialog_->close();
                    }
                }
                if (background_connection_) {
                    QObject::disconnect(background_connection_);
                }
                background_mode_->restore();
                QString const test_result_message =
                    test_mode_ ? gui_app_controller_helpers::build_test_result_message(
                                     out_.result,
                                     out_.result.hash_summary.has_value() ? out_.result.hash_summary->num_archives : 1)
                               : QString();
                bool const show_test_result =
                    test_mode_ && delayed_result_success
                    && presentation == z7::ui::runtime_support::TaskResultPresentation::kNormal
                    && !test_result_message.trimmed().isEmpty();
                bool show_hash_result = hash_mode_ && delayed_result_success && out_.result.hash_summary.has_value();
#ifdef Z7_TESTING
                if (show_hash_result && gui_app_controller_helpers::suppress_result_dialogs_for_tests()) {
                    show_hash_result = false;
                }
#endif
                if (show_test_result) {
                    gui_app_controller_helpers::show_test_result_dialog(
                        dialog_was_shown ? dialog_.data() : nullptr,
                        z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(3302)),
                        test_result_message);
                }
                if (show_hash_result) {
                    HashResultDialog hash_dialog(dialog_was_shown ? dialog_.data() : nullptr);
                    hash_dialog.set_rows(gui_app_controller_helpers::hash_result_dialog_rows(out_));
                    hash_dialog.exec();
                }

                if (presentation == z7::ui::runtime_support::TaskResultPresentation::kFinalErrorThenMessages
                    && show_result_messages) {
                    show_final_error_dialog(dialog_was_shown ? dialog_.data() : nullptr);
                }
                if (show_result_messages) {
                    show_progress_result_messages();
                    return;
                }

                if (dialog_was_shown && !dialog_.isNull()) {
                    dialog_->close();
                }
                if (callback_) {
                    GuiTaskRunner::FinishedCallback callback = std::move(callback_);
                    callback(std::move(out_));
                }
                deleteLater();
            }

        private:
            void handle_request_outcome(std::shared_ptr<z7::app::ArchiveRequest> request,
                                        std::shared_ptr<CompletionHandler> on_complete,
                                        z7::app::OperationOutcome const& outcome) {
                if (!is_password_failure(outcome)) {
                    if (password_retry_state_) {
                        password_retry_state_->next_password.reset();
                        password_retry_state_->prompt_canceled = false;
                    }
                    if (on_complete && *on_complete) {
                        (*on_complete)(this, outcome);
                    }
                    return;
                }

                if (password_retry_state_ && password_retry_state_->prompt_canceled) {
                    password_retry_state_->next_password.reset();
                    password_retry_state_->prompt_canceled = false;
                    if (on_complete && *on_complete) {
                        (*on_complete)(this, make_canceled_outcome("Operation canceled."));
                    }
                    return;
                }

                QWidget* prompt_parent = ensure_dialog_visible_for_password_prompt();
                if (prompt_parent == nullptr) {
                    if (on_complete && *on_complete) {
                        (*on_complete)(this, outcome);
                    }
                    return;
                }

                z7::app::PasswordPrompt prompt;
                prompt.archive_path = password_prompt_archive_path(*request, outcome);
                prompt.reason_kind = z7::app::PasswordPromptReason::kWrongPassword;
                prompt.reason = "wrong_password";
                z7::app::PasswordReply const reply = show_password_prompt_dialog(prompt_parent, prompt);
                if (reply.kind != z7::app::PasswordReplyKind::kProvide) {
                    if (password_retry_state_) {
                        password_retry_state_->next_password.reset();
                        password_retry_state_->prompt_canceled = false;
                    }
                    if (on_complete && *on_complete) {
                        (*on_complete)(this, make_canceled_outcome("Operation canceled."));
                    }
                    return;
                }

                bool const request_stores_password = store_retry_password_in_request(request.get(), reply.password);
                if (password_retry_state_) {
                    if (request_stores_password) {
                        password_retry_state_->next_password.reset();
                    } else {
                        password_retry_state_->next_password = reply.password;
                    }
                    password_retry_state_->prompt_canceled = false;
                }
                start_request_attempt(std::move(request), std::move(on_complete));
            }

            bool should_show_result_messages(
                z7::ui::runtime_support::TaskResultPresentation presentation) const {
#ifdef Z7_TESTING
                if (gui_app_controller_helpers::suppress_result_dialogs_for_tests()) {
                    return false;
                }
#endif
                return !dialog_.isNull()
                    && (presentation == z7::ui::runtime_support::TaskResultPresentation::kMessages
                        || presentation
                            == z7::ui::runtime_support::TaskResultPresentation::kFinalErrorThenMessages);
            }

            void complete_after_result_dialog_closed() {
                QPointer<ArchiveTaskAsyncSequencer> self(this);
                QObject::connect(dialog_.data(), &QDialog::finished, this, [self]() mutable {
                    if (self.isNull()) {
                        return;
                    }
                    if (self->callback_) {
                        GuiTaskRunner::FinishedCallback callback = std::move(self->callback_);
                        callback(std::move(self->out_));
                    }
                    self->deleteLater();
                });
            }

            void show_final_error_dialog(QWidget* parent) {
                QString const raw_summary = out_.result.summary.empty()
                                              ? z7::ui::archive_support::from_native_string(
                                                    z7::app::describe_archive_error(out_.result.error))
                                              : z7::ui::archive_support::from_native_string(out_.result.summary);
                QString const summary =
                    z7::ui::runtime_support::localize_archive_failure_message(raw_summary);
                if (!summary.isEmpty()) {
                    QMessageBox::critical(parent, QStringLiteral("7-Zip"), summary);
                    out_.final_error_presented = true;
                }
            }

            void show_progress_result_messages() {
                if (test_mode_) {
                    dialog_->set_stage(QStringLiteral("Running"));
                }
                dialog_->set_running(false);
                dialog_->set_failure_result_messages(out_.result_messages);
                dialog_->set_failure_result_mode();
                show_dialog();
                if (background_connection_) {
                    QObject::disconnect(background_connection_);
                }
                background_mode_->restore();
                complete_after_result_dialog_closed();
            }

            void release_active_request() {
                release_session_controls(&active_bindings_);
                active_session_ = z7::app::ArchiveSession();
                active_delegate_.reset();
                active_completion_ = nullptr;
            }

            void close_next_token() {
                if (tokens_.isEmpty()) {
                    std::function<void(ArchiveTaskAsyncSequencer*)> after_close = std::move(after_close_);
                    after_close_ = nullptr;
                    if (after_close) {
                        after_close(this);
                    }
                    return;
                }

                z7::app::CloseArchiveSessionRequest request;
                request.token = tokens_.takeLast();
                start_request(z7::app::ArchiveRequest{request},
                              [](ArchiveTaskAsyncSequencer* self, z7::app::OperationOutcome const&) {
                                  self->close_next_token();
                              });
            }

            QPointer<z7::ui::runtime_support::TaskProgressDialogBase> dialog_;
            z7::ui::runtime_support::DelayedProgressDialogPresenter progress_presenter_;
            GuiTaskRunResult out_;
            GuiTaskRunner::FinishedCallback callback_;
            SharedTaskCancellation cancel_requested_;
            bool test_mode_ = false;
            bool hash_mode_ = false;
            QString redundant_test_archive_context_;
            std::shared_ptr<z7::ui::common::TaskBackgroundModeController> background_mode_;
            QMetaObject::Connection background_connection_;
            QVector<z7::app::ArchiveSessionToken> tokens_;
            z7::app::ArchiveEngine active_engine_;
            z7::app::ArchiveSession active_session_;
            std::shared_ptr<z7::app::IArchiveDelegate> active_delegate_;
            SessionControlBindings active_bindings_;
            CompletionHandler active_completion_;
            std::function<void(ArchiveTaskAsyncSequencer*)> after_close_;
            std::shared_ptr<PasswordRetryState> password_retry_state_ = std::make_shared<PasswordRetryState>();
            bool finished_ = false;
        };

        ArchiveTaskAsyncSequencer* make_sequence_or_finish(SequencedTaskRunSpec run_spec) {
            auto* sequence = new ArchiveTaskAsyncSequencer(std::move(run_spec));
            if (sequence->has_dialog()) {
                return sequence;
            }
            sequence->fail_immediate(2,
                                     z7::app::ArchiveErrorDomain::kBackendUnavailable,
                                     QStringLiteral("Task progress dialog is unavailable."));
            sequence->finalize();
            return nullptr;
        }

        void start_open_root(ArchiveTaskAsyncSequencer* sequence,
                             QString const& archive_path,
                             QString const& archive_type_hint,
                             std::string_view diagnostic_stage,
                             QStringList nested_archive_entries,
                             std::vector<std::optional<uint32_t>> filename_code_pages,
                             std::function<void(ArchiveTaskAsyncSequencer*)> on_opened) {
            Q_UNUSED(diagnostic_stage);
            if (sequence == nullptr) {
                return;
            }
            if (sequence->cancel_already_requested()) {
                sequence->fail_immediate(5, z7::app::ArchiveErrorDomain::kCanceled, canceled_export_message());
                sequence->close_tokens_then([](ArchiveTaskAsyncSequencer* self) { self->finalize(); });
                return;
            }

            z7::app::OpenArchiveFromPathRequest open_root_request;
            open_root_request.archive_path = z7::ui::archive_support::to_native_string(archive_path);
            open_root_request.archive_type_hint = z7::ui::archive_support::to_native_string(archive_type_hint);
            if (filename_code_pages.size() != static_cast<size_t>(nested_archive_entries.size() + 1)) {
                sequence->fail_immediate(
                    1,
                    z7::app::ArchiveErrorDomain::kInvalidArguments,
                    QStringLiteral("Archive filename code-page count must equal root plus nested layers."));
                sequence->finalize();
                return;
            }
            open_root_request.filename_code_page = filename_code_pages.front();
            sequence->start_request(
                z7::app::ArchiveRequest{open_root_request},
                [nested_archive_entries = std::move(nested_archive_entries),
                 filename_code_pages = std::move(filename_code_pages),
                 on_opened = std::move(on_opened)](
                    ArchiveTaskAsyncSequencer* self, z7::app::OperationOutcome const& outcome) mutable {
                    auto const root_payload = z7::app::outcome_payload_as<z7::app::OpenArchiveSessionResult>(outcome);
                    if (!root_payload.has_value() || !root_payload->ok || !root_payload->token.is_valid()) {
                        self->set_result_from_outcome(outcome);
                        if (self->result()->result.summary.empty()) {
                            self->fail_immediate(2,
                                                 z7::app::ArchiveErrorDomain::kUnknown,
                                                 QStringLiteral("Failed to open archive session."));
                        }
                        self->close_tokens_then([](ArchiveTaskAsyncSequencer* sequence) { sequence->finalize(); });
                        return;
                    }
                    self->append_session_token(root_payload->token);

                    if (nested_archive_entries.isEmpty()) {
                        on_opened(self);
                        return;
                    }

                    auto nested_entries = std::make_shared<QStringList>(std::move(nested_archive_entries));
                    auto nested_code_pages =
                        std::make_shared<std::vector<std::optional<uint32_t>>>(std::move(filename_code_pages));
                    auto open_nested = std::make_shared<std::function<void(int)>>();
                    *open_nested = [self, nested_entries, nested_code_pages, open_nested, on_opened](int index) mutable {
                        if (self->cancel_already_requested()) {
                            self->fail_immediate(5, z7::app::ArchiveErrorDomain::kCanceled, canceled_export_message());
                            self->close_tokens_then([](ArchiveTaskAsyncSequencer* sequence) { sequence->finalize(); });
                            return;
                        }
                        if (index >= nested_entries->size()) {
                            on_opened(self);
                            return;
                        }

                        QString const nested_entry = nested_entries->at(index);
                        z7::app::OpenArchiveFromParentRequest open_child_request;
                        open_child_request.parent = self->tokens().back();
                        open_child_request.entry_path = z7::ui::archive_support::to_utf8_string(nested_entry);
                        open_child_request.archive_type_hint =
                            z7::ui::archive_support::to_native_string(infer_archive_format(nested_entry, QString()));
                        open_child_request.display_path_hint = z7::ui::archive_support::to_utf8_string(nested_entry);
                        open_child_request.filename_code_page = nested_code_pages->at(static_cast<size_t>(index + 1));
                        self->start_request(
                            z7::app::ArchiveRequest{open_child_request},
                            [index, open_nested](ArchiveTaskAsyncSequencer* sequence,
                                                 z7::app::OperationOutcome const& outcome) {
                                auto const child_payload =
                                    z7::app::outcome_payload_as<z7::app::OpenArchiveSessionResult>(outcome);
                                if (!child_payload.has_value()
                                    || !child_payload->ok
                                    || !child_payload->token.is_valid()) {
                                    QString const summary = outcome_summary(outcome);
                                    sequence->fail_immediate(
                                        result_code_for_failure(outcome.error.domain),
                                        outcome.error.domain,
                                        summary.isEmpty()
                                            ? QStringLiteral("nested level %1 failed").arg(index + 1)
                                            : QStringLiteral("nested level %1: %2").arg(index + 1).arg(summary));
                                    sequence->close_tokens_then(
                                        [](ArchiveTaskAsyncSequencer* self) { self->finalize(); });
                                    return;
                                }
                                sequence->append_session_token(child_payload->token);
                                (*open_nested)(index + 1);
                            });
                    };
                    (*open_nested)(0);
                });
        }

        template <typename TOptions, typename TStartFn>
        void run_archive_member_async_impl(TOptions options,
                                           SequencedTaskRunSpec run_spec,
                                           ArchiveMemberRunKind run_kind,
                                           std::string_view open_root_diagnostic_stage,
                                           TStartFn&& start_member_request) {
            run_spec.title =
                run_spec.title.trimmed().isEmpty() ? default_member_title(run_kind) : run_spec.title.trimmed();
            run_spec.test_mode = run_kind == ArchiveMemberRunKind::kTest;
            run_spec.hash_mode = run_kind == ArchiveMemberRunKind::kHash;
            if (run_kind == ArchiveMemberRunKind::kTest && options.nested_archive_entries.isEmpty()) {
                run_spec.redundant_test_archive_context = options.archive_path;
            }
            ArchiveTaskAsyncSequencer* sequence = make_sequence_or_finish(std::move(run_spec));
            if (sequence == nullptr) {
                return;
            }

            sequence->schedule_delayed_dialog_show();
            QString const archive_path = options.archive_path;
            QString const archive_type_hint = options.archive_type_hint;
            QStringList const nested_archive_entries = options.nested_archive_entries;
            auto const filename_code_pages = options.filename_code_pages;
            start_open_root(
                sequence,
                archive_path,
                archive_type_hint,
                open_root_diagnostic_stage,
                nested_archive_entries,
                filename_code_pages,
                [options = std::move(options), start_member_request = std::forward<TStartFn>(start_member_request)](
                    ArchiveTaskAsyncSequencer* self) mutable {
                    if (self->cancel_already_requested()) {
                        self->fail_immediate(5, z7::app::ArchiveErrorDomain::kCanceled, canceled_export_message());
                        self->close_tokens_then([](ArchiveTaskAsyncSequencer* sequence) { sequence->finalize(); });
                        return;
                    }
                    start_member_request(self, options);
                });
        }

        void run_archive_hash_async_impl(ArchiveHashTaskOptions const& options, SequencedTaskRunSpec run_spec) {
            run_archive_member_async_impl(
                options,
                std::move(run_spec),
                ArchiveMemberRunKind::kHash,
                {},
                [](ArchiveTaskAsyncSequencer* self, ArchiveHashTaskOptions const& options) {
                    z7::app::HashRequest request;
                    request.session_token = self->tokens().back();
                    request.entries = z7::ui::archive_support::to_utf8_string_list(options.entry_paths);
                    request.hash_method = z7::ui::archive_support::to_native_string(options.hash_method);
                    request.recursive_dirs = true;
                    self->start_request(
                        z7::app::ArchiveRequest{request},
                        [](ArchiveTaskAsyncSequencer* sequence, z7::app::OperationOutcome const& outcome) {
                            sequence->set_result_from_outcome(outcome);
                            sequence->close_tokens_then([](ArchiveTaskAsyncSequencer* self) { self->finalize(); });
                        });
                });
        }

        void run_archive_test_async_impl(ArchiveTestTaskOptions const& options, SequencedTaskRunSpec run_spec) {
            run_archive_member_async_impl(
                options,
                std::move(run_spec),
                ArchiveMemberRunKind::kTest,
                "gui.test.open_root.start",
                [](ArchiveTaskAsyncSequencer* self, ArchiveTestTaskOptions const& options) {
                    z7::app::TestRequest request;
                    request.session_token = self->tokens().back();
                    request.entries = z7::ui::archive_support::to_utf8_string_list(options.entry_paths);
                    apply_configured_extract_memory_limit(&request);
                    self->start_request(
                        z7::app::ArchiveRequest{request},
                        [](ArchiveTaskAsyncSequencer* sequence, z7::app::OperationOutcome const& outcome) {
                            sequence->set_result_from_outcome(outcome);
                            sequence->close_tokens_then([](ArchiveTaskAsyncSequencer* self) { self->finalize(); });
                        });
                });
        }

        void run_archive_export_async_impl(ArchiveExportTaskOptions const& options, SequencedTaskRunSpec run_spec) {
            run_archive_member_async_impl(
                options,
                std::move(run_spec),
                ArchiveMemberRunKind::kExport,
                "gui.archive_export.open_root.start",
                [](ArchiveTaskAsyncSequencer* self, ArchiveExportTaskOptions const& options) {
                    z7::app::ExtractRequest request;
                    request.session_token = self->tokens().back();
                    request.output_dir = z7::ui::archive_support::to_native_string(options.output_dir);
                    request.archive_type_hint = z7::ui::archive_support::to_native_string(options.archive_type_hint);
                    request.overwrite_mode = options.overwrite_mode;
                    request.path_mode = options.path_mode;
                    request.eliminate_root_duplication = options.eliminate_root_duplication;
                    request.restore_file_security = options.restore_file_security;
                    request.zone_id_mode = options.zone_id_mode;
                    request.archive_path = z7::ui::archive_support::to_native_string(options.archive_path);
                    request.password = z7::ui::archive_support::to_utf8_string(options.password);
                    request.entries = z7::ui::archive_support::to_utf8_string_list(options.entry_paths);
                    request.path_remaps = options.path_remaps;
                    apply_configured_extract_memory_limit(&request);
                    self->start_request(
                        z7::app::ArchiveRequest{request},
                        [](ArchiveTaskAsyncSequencer* sequence, z7::app::OperationOutcome const& outcome) {
                            sequence->set_result_from_outcome(outcome);
                            sequence->close_tokens_then([](ArchiveTaskAsyncSequencer* self) { self->finalize(); });
                        });
                });
        }

        void run_archive_export_async_task_impl(ArchiveExportTaskSpec const& spec, SequencedTaskRunSpec run_spec) {
            ArchiveExportTaskOptions options;
            QString error_message;
            if (!build_archive_export_task_options(spec, &options, &error_message)) {
                finish_run(std::move(run_spec),
                           make_failure_result(1,
                                               z7::app::ArchiveErrorDomain::kInvalidArguments,
                                               error_message.trimmed().isEmpty()
                                                   ? QStringLiteral("Invalid archive export request.")
                                                   : error_message.trimmed()));
                return;
            }
            run_archive_export_async_impl(options, std::move(run_spec));
        }

        void run_archive_hash_async_task_impl(ArchiveHashTaskSpec const& spec, SequencedTaskRunSpec run_spec) {
            ArchiveHashTaskOptions options;
            QString error_message;
            if (!build_archive_hash_task_options(spec, &options, &error_message)) {
                finish_run(std::move(run_spec),
                           make_failure_result(1,
                                               z7::app::ArchiveErrorDomain::kInvalidArguments,
                                               error_message.trimmed().isEmpty()
                                                   ? QStringLiteral("Invalid archive hash request.")
                                                   : error_message.trimmed()));
                return;
            }
            run_archive_hash_async_impl(options, std::move(run_spec));
        }

        void run_archive_test_async_task_impl(ArchiveTestTaskSpec const& spec, SequencedTaskRunSpec run_spec) {
            ArchiveTestTaskOptions options;
            QString error_message;
            if (!build_archive_test_task_options(spec, &options, &error_message)) {
                finish_run(std::move(run_spec),
                           make_failure_result(1,
                                               z7::app::ArchiveErrorDomain::kInvalidArguments,
                                               error_message.trimmed().isEmpty()
                                                   ? QStringLiteral("Invalid archive test request.")
                                                   : error_message.trimmed()));
                return;
            }
            run_archive_test_async_impl(options, std::move(run_spec));
        }

    } // namespace

    void run_archive_export_async_task(ArchiveExportTaskSpec const& spec, SequencedTaskRunSpec run_spec) {
        run_archive_export_async_task_impl(spec, std::move(run_spec));
    }

    void run_archive_hash_async_task(ArchiveHashTaskSpec const& spec, SequencedTaskRunSpec run_spec) {
        run_archive_hash_async_task_impl(spec, std::move(run_spec));
    }

    void run_archive_test_async_task(ArchiveTestTaskSpec const& spec, SequencedTaskRunSpec run_spec) {
        run_archive_test_async_task_impl(spec, std::move(run_spec));
    }

    void run_modal_async_with_request(GuiTaskSpec const& spec,
                                      z7::app::ArchiveRequest const& request,
                                      SequencedTaskRunSpec run_spec) {
        run_spec.title = normalized_modal_title(spec, run_spec.title);
        run_spec.test_mode = std::holds_alternative<TestTaskSpec>(spec);
        if (run_spec.test_mode) {
            if (auto const* test_request = std::get_if<z7::app::TestRequest>(&request.payload);
                test_request != nullptr && test_request->archive_paths.size() == 1) {
                run_spec.redundant_test_archive_context =
                    z7::ui::archive_support::from_native_string(test_request->archive_paths.front());
            }
        }
        ArchiveTaskAsyncSequencer* sequence = make_sequence_or_finish(std::move(run_spec));
        if (sequence == nullptr) {
            return;
        }

        sequence->schedule_delayed_dialog_show();
        sequence->start_request(request, [](ArchiveTaskAsyncSequencer* self, z7::app::OperationOutcome const& outcome) {
            self->set_result_from_outcome(outcome);
            self->finalize();
        });
    }

} // namespace z7::ui::gui
