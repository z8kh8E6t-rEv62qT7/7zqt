// src/ui/filemanager/src/main_window/archive/archive_open_actions.cpp
// Role: Archive temp-dir/session helpers and drag materialization actions.

#include <QEventLoop>
#include <memory>
#include <optional>
#include <utility>
#include <variant>

#include "archive_delegate_qt.h"
#include "archive_failure_messages.h"
#include "archive_process_runner/core_prompts.h"
#include "archive_session_helpers.h"
#include "extract_memory_settings.h"
#include "large_pages_settings.h"
#include "main_window/deps.h"
#include "main_window/internal.h"

namespace z7::ui::filemanager {
    namespace {

        QString archive_drag_extract_error_message(z7::app::OperationOutcome const& outcome,
                                                   std::optional<z7::app::ExtractResult> const& result,
                                                   QString const& fallback_message) {
            QString error_message;
            if (result.has_value()) {
                error_message = z7::ui::archive_support::from_utf8_string(result->summary);
            }
            if (error_message.trimmed().isEmpty()) {
                error_message = z7::ui::archive_support::from_utf8_string(outcome.error.message);
            }
            if (error_message.trimmed().isEmpty()) {
                error_message = fallback_message;
            }
            return z7::ui::runtime_support::localize_archive_failure_message(error_message).trimmed();
        }

        bool is_password_failure(z7::app::OperationOutcome const& outcome) {
            return outcome.status == z7::app::OperationStatus::kWrongPassword
                || outcome.error_domain == z7::app::ArchiveErrorDomain::kPassword
                || outcome.error.domain == z7::app::ArchiveErrorDomain::kPassword;
        }

        z7::app::OperationOutcome make_canceled_outcome() {
            z7::app::OperationResult const result =
                z7::app::make_immediate_result(5, z7::app::ArchiveErrorDomain::kCanceled, "Operation canceled.");
            return z7::app::archive_session_helpers::make_outcome(result, z7::app::OperationPayload{std::monostate{}});
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
            return false;
        }

        class PasswordRetryRelayDelegate final : public z7::ui::archive_support::OutcomeRelayDelegate {
        public:
            using PasswordPromptFn =
                std::function<std::optional<z7::app::PasswordReply>(z7::app::PasswordPrompt const&)>;

            PasswordRetryRelayDelegate(QObject* owner, FinishedCallback finished_cb, PasswordPromptFn password_prompt) :
                z7::ui::archive_support::OutcomeRelayDelegate(
                    owner,
                    std::move(finished_cb),
                    nullptr,
                    z7::ui::archive_support::MissingTargetPolicy::kInvokeDirect),
                password_prompt_(std::move(password_prompt)) {}

            std::optional<z7::app::PasswordReply> request_password(z7::app::PasswordPrompt const& prompt) override {
                if (!password_prompt_) {
                    return std::nullopt;
                }
                return password_prompt_(prompt);
            }

        private:
            PasswordPromptFn password_prompt_;
        };

        z7::app::PasswordReply prompt_for_wrong_password(MainWindow* owner,
                                                         z7::app::ArchiveRequest const& request,
                                                         z7::app::OperationOutcome const& outcome) {
            z7::app::PasswordPrompt prompt;
            prompt.archive_path = password_prompt_archive_path(request, outcome);
            prompt.reason = "wrong_password";
            prompt.reason_kind = z7::app::PasswordPromptReason::kWrongPassword;
            return show_default_password_prompt(owner, prompt);
        }

        z7::app::OperationOutcome run_archive_drag_extract_request_blocking(MainWindow* owner,
                                                                            z7::app::ArchiveRequest request) {
            std::optional<std::string> retry_next_password;
            bool prompt_canceled = false;

            for (;;) {
                QEventLoop wait_loop;
                z7::app::OperationOutcome outcome = z7::app::make_backend_unavailable_outcome();
                bool finished = false;

                auto password_prompt =
                    [owner, &retry_next_password, &prompt_canceled](
                        z7::app::PasswordPrompt const& prompt) -> std::optional<z7::app::PasswordReply> {
                    return z7::ui::archive_support::call_on_target_blocking<z7::app::PasswordReply>(
                        owner,
                        prompt,
                        z7::app::PasswordReply{},
                        [owner, &retry_next_password, &prompt_canceled](z7::app::PasswordPrompt const& prompt_value) {
                            if (retry_next_password.has_value()) {
                                z7::app::PasswordReply retry_reply;
                                retry_reply.kind = z7::app::PasswordReplyKind::kProvide;
                                retry_reply.password = std::move(*retry_next_password);
                                retry_next_password.reset();
                                prompt_canceled = false;
                                return retry_reply;
                            }
                            z7::app::PasswordReply reply = show_default_password_prompt(owner, prompt_value);
                            prompt_canceled = reply.kind != z7::app::PasswordReplyKind::kProvide;
                            return reply;
                        });
                };

                auto delegate = std::make_shared<PasswordRetryRelayDelegate>(
                    owner,
                    [&outcome, &finished, &wait_loop](z7::app::OperationOutcome const& value) {
                        outcome = value;
                        finished = true;
                        if (wait_loop.isRunning()) {
                            wait_loop.quit();
                        }
                    },
                    std::move(password_prompt));

                z7::app::ArchiveEngine engine;
                z7::ui::runtime_support::apply_configured_large_pages_mode();
                z7::app::ArchiveSession const session = engine.start(request, delegate);
                if (!session.valid()) {
                    return outcome;
                }

                if (!finished) {
                    wait_loop.exec();
                }

                if (!is_password_failure(outcome)) {
                    return outcome;
                }
                if (prompt_canceled) {
                    return make_canceled_outcome();
                }
                z7::app::PasswordReply const reply = prompt_for_wrong_password(owner, request, outcome);
                if (reply.kind != z7::app::PasswordReplyKind::kProvide) {
                    return make_canceled_outcome();
                }
                prompt_canceled = false;
                if (!store_retry_password_in_request(&request, reply.password)) {
                    retry_next_password = reply.password;
                }
            }
        }

    } // namespace

    QSharedPointer<OwnedTemporaryDirectory>
    MainWindow::create_temporary_directory_with_prefix(QString const& prefix, QString const& failure_caption) {
        QSharedPointer<OwnedTemporaryDirectory> const temp_dir(new OwnedTemporaryDirectory(prefix));
        if (temp_dir == nullptr || !temp_dir->isValid()) {
            QMessageBox::warning(this, failure_caption, QStringLiteral("Failed to create temporary directory."));
            return {};
        }
        return temp_dir;
    }

    QSharedPointer<OwnedTemporaryDirectory>
    MainWindow::create_archive_open_temporary_directory(QString const& failure_caption) {
        return create_temporary_directory_with_prefix(QStringLiteral("7zO"), failure_caption);
    }

    QSharedPointer<OwnedTemporaryDirectory>
    MainWindow::create_archive_drag_temporary_directory(QString const& failure_caption) {
        return create_temporary_directory_with_prefix(QStringLiteral("7zE"), failure_caption);
    }

    void MainWindow::materialize_archive_drag_entries_for_panel(
        int panel_index,
        QStringList const& entries,
        std::function<void(QStringList const&, QString const&)> const& finished_cb) {
        auto const finish = [finished_cb](QStringList const& paths, QString const& error_message) {
            if (finished_cb) {
                finished_cb(paths, error_message);
            }
        };

        QStringList normalized_entries;
        normalized_entries.reserve(entries.size());
        for (QString const& entry : entries) {
            QString const normalized = z7::ui::archive_support::normalize_virtual_dir(entry);
            if (!normalized.isEmpty()) {
                normalized_entries << normalized;
            }
        }
        normalized_entries.removeDuplicates();
        if (normalized_entries.isEmpty()) {
            finish({}, QString());
            return;
        }

        PanelController const& panel = panel_controller(panel_index);
        if (!in_archive_view_for_panel(panel_index) || panel.archive.source_archive.trimmed().isEmpty()) {
            finish({}, QStringLiteral("Archive drag materialization context is unavailable."));
            return;
        }

        QSharedPointer<OwnedTemporaryDirectory> const temp_dir = create_archive_drag_temporary_directory(
            z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(540)));
        if (temp_dir == nullptr || !temp_dir->isValid()) {
            finish({}, QStringLiteral("Failed to allocate temporary directory for drag-out."));
            return;
        }

        z7::app::ExtractRequest request;
        if (panel.archive.current_token.is_valid()) {
            request.session_token = panel.archive.current_token;
        } else {
            request.archive_path = z7::ui::archive_support::to_native_string(panel.archive.source_archive);
        }
        request.output_dir = z7::ui::archive_support::to_native_string(temp_dir->path());
        request.archive_type_hint = z7::ui::archive_support::to_utf8_string(panel.archive.type_hint.trimmed());
        request.overwrite_mode = z7::app::OverwriteMode::kOverwrite;
        request.path_mode = z7::app::ExtractPathMode::kFullPaths;
        request.entries.reserve(normalized_entries.size());
        for (QString const& entry : normalized_entries) {
            request.entries.push_back(z7::ui::archive_support::to_utf8_string(entry));
        }
        uint64_t const configured_limit = z7::ui::runtime_support::configured_extract_memory_limit_bytes();
        if (configured_limit != 0) {
            request.configured_memory_limit_bytes = configured_limit;
            request.configured_memory_limit_defined = true;
        }

        struct DragMaterializationTask final : public std::enable_shared_from_this<DragMaterializationTask> {
            QPointer<MainWindow> owner;
            QSharedPointer<OwnedTemporaryDirectory> temp_dir;
            QStringList normalized_entries;
            QString archive_path;
            QString archive_type_hint;
            std::function<void(QStringList const&, QString const&)> finished;
            z7::app::ArchiveEngine engine;
            z7::app::ArchiveSession session;
            std::shared_ptr<z7::app::IArchiveDelegate> delegate;
            z7::app::ArchiveRequest request;
            std::optional<std::string> retry_next_password;
            bool prompt_canceled = false;

            void begin(z7::app::ArchiveRequest&& initial_request) {
                request = std::move(initial_request);
                start_attempt();
            }

            void start_attempt() {
                auto self = shared_from_this();
                auto password_prompt =
                    [self](z7::app::PasswordPrompt const& prompt) -> std::optional<z7::app::PasswordReply> {
                    return z7::ui::archive_support::call_on_target_blocking<z7::app::PasswordReply>(
                        self->owner.data(),
                        prompt,
                        z7::app::PasswordReply{},
                        [self](z7::app::PasswordPrompt const& prompt_value) {
                            if (self->retry_next_password.has_value()) {
                                z7::app::PasswordReply retry_reply;
                                retry_reply.kind = z7::app::PasswordReplyKind::kProvide;
                                retry_reply.password = std::move(*self->retry_next_password);
                                self->retry_next_password.reset();
                                self->prompt_canceled = false;
                                return retry_reply;
                            }
                            z7::app::PasswordReply reply =
                                show_default_password_prompt(self->owner.data(), prompt_value);
                            self->prompt_canceled = reply.kind != z7::app::PasswordReplyKind::kProvide;
                            return reply;
                        });
                };
                delegate = std::make_shared<PasswordRetryRelayDelegate>(
                    owner.data(),
                    [self](z7::app::OperationOutcome const& outcome) { self->on_finished(outcome); },
                    std::move(password_prompt));
                z7::ui::runtime_support::apply_configured_large_pages_mode();
                session = engine.start(request, delegate);
                if (!session.valid()) {
                    z7::app::OperationOutcome const unavailable = z7::app::make_backend_unavailable_outcome();
                    QString const error = z7::ui::archive_support::from_utf8_string(unavailable.summary);
                    complete({},
                             error.trimmed().isEmpty()
                                 ? z7::ui::archive_support::from_utf8_string(unavailable.error.message)
                                 : error);
                }
            }

            void on_finished(z7::app::OperationOutcome const& outcome) {
                if (is_password_failure(outcome)) {
                    session = z7::app::ArchiveSession{};
                    delegate.reset();
                    if (prompt_canceled || owner.isNull()) {
                        complete({}, QString());
                        return;
                    }
                    z7::app::PasswordReply const reply = prompt_for_wrong_password(owner.data(), request, outcome);
                    if (reply.kind != z7::app::PasswordReplyKind::kProvide) {
                        complete({}, QString());
                        return;
                    }
                    prompt_canceled = false;
                    if (!store_retry_password_in_request(&request, reply.password)) {
                        retry_next_password = reply.password;
                    }
                    start_attempt();
                    return;
                }

                auto const result = z7::app::outcome_payload_as<z7::app::ExtractResult>(outcome);
                if (!result.has_value() || !result->ok) {
                    QString error_message;
                    if (result.has_value()) {
                        error_message = z7::ui::archive_support::from_utf8_string(result->summary);
                    }
                    if (error_message.trimmed().isEmpty()) {
                        error_message = z7::ui::archive_support::from_utf8_string(outcome.error.message);
                    }
                    if (error_message.trimmed().isEmpty()) {
                        error_message = QStringLiteral("Failed to materialize archive drag entries.");
                    }
                    complete({}, z7::ui::runtime_support::localize_archive_failure_message(error_message));
                    return;
                }

                QStringList extracted_paths;
                extracted_paths.reserve(normalized_entries.size());
                for (QString const& entry : normalized_entries) {
                    QString const rel_path = QDir::fromNativeSeparators(entry);
                    QFileInfo const extracted_info(QDir(temp_dir->path()).filePath(rel_path));
                    if (!extracted_info.exists()) {
                        continue;
                    }
                    extracted_paths << extracted_info.absoluteFilePath();
                }
                extracted_paths.removeDuplicates();
                if (extracted_paths.isEmpty()) {
                    complete({}, QStringLiteral("No extracted paths produced for archive drag entries."));
                    return;
                }

                if (!owner.isNull()) {
                    QSharedPointer<MainWindow::ArchiveTempSession> drag_session(new MainWindow::ArchiveTempSession);
                    drag_session->purpose = MainWindow::ArchiveTempSessionPurpose::kDragOut;
                    drag_session->temp_dir = temp_dir;
                    drag_session->archive_path = archive_path;
                    drag_session->archive_type_hint = archive_type_hint;
                    drag_session->command_caption =
                        z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(540));
                    drag_session->extracted_paths = extracted_paths;
                    owner->retain_archive_temp_session(drag_session);
                }
                complete(extracted_paths, QString());
            }

            void complete(QStringList const& paths, QString const& error_message) {
                session = z7::app::ArchiveSession{};
                delegate.reset();
                if (finished) {
                    finished(paths, error_message);
                }
            }
        };

        auto task = std::make_shared<DragMaterializationTask>();
        task->owner = this;
        task->temp_dir = temp_dir;
        task->normalized_entries = normalized_entries;
        task->archive_path = panel.archive.source_archive;
        task->archive_type_hint = panel.archive.type_hint.trimmed();
        task->finished = finish;
        task->begin(z7::app::ArchiveRequest{std::move(request)});
    }

    bool MainWindow::export_archive_drag_entry_to_destination_for_panel(int panel_index,
                                                                        QString const& archive_entry,
                                                                        bool entry_is_dir,
                                                                        QString const& destination_path,
                                                                        QString* error) {
        struct ExportPrompt {
            int panel_index = -1;
            QString archive_entry;
            bool entry_is_dir = false;
            QString destination_path;
        };

        struct ExportResult {
            bool ok = false;
            QString error;
        };

        ExportPrompt const prompt{panel_index,
                                  z7::ui::archive_support::normalize_virtual_dir(archive_entry),
                                  entry_is_dir,
                                  QDir::cleanPath(QDir::fromNativeSeparators(destination_path.trimmed()))};
        ExportResult const result = z7::ui::archive_support::call_on_target_blocking<ExportResult>(
            this, prompt, ExportResult{}, [this](ExportPrompt const& request) {
                ExportResult out;
                int const panel_count = static_cast<int>(sizeof(panels_) / sizeof(panels_[0]));
                if (request.panel_index < 0 || request.panel_index >= panel_count) {
                    out.error = QStringLiteral("Archive drag direct export panel is invalid.");
                    return out;
                }

                if (request.destination_path.trimmed().isEmpty()) {
                    out.error = QStringLiteral("Archive drag direct export destination path is empty.");
                    return out;
                }
                QFileInfo const destination_info(request.destination_path);
                QString const destination_directory = destination_info.absolutePath().trimmed();
                QString const destination_name = destination_info.fileName().trimmed();
                if (destination_directory.isEmpty() || destination_name.isEmpty()) {
                    out.error = QStringLiteral("Archive drag direct export destination path is invalid.");
                    return out;
                }

                PanelController const& panel = panel_controller(request.panel_index);
                if (!in_archive_view_for_panel(request.panel_index)) {
                    out.error = QStringLiteral("Archive drag direct export context is unavailable.");
                    return out;
                }

                if (panel.archive.current_token.is_valid()) {
                    if (panel.archive.source_archive.trimmed().isEmpty()
                        && panel.archive.virtual_display_source.trimmed().isEmpty()) {
                        out.error = QStringLiteral("Archive drag direct export source is unavailable.");
                        return out;
                    }
                } else if (panel.archive.source_archive.trimmed().isEmpty()) {
                    out.error = QStringLiteral("Archive drag direct export source is unavailable.");
                    return out;
                }

                bool const is_virtual_root = request.archive_entry.isEmpty();
                if (is_virtual_root && !panel.archive.virtual_dir.trimmed().isEmpty()) {
                    out.error =
                        QStringLiteral("Only the top-level archive root can be exported without an entry path.");
                    return out;
                }

                z7::app::ExtractRequest extract_request;
                if (panel.archive.current_token.is_valid()) {
                    extract_request.session_token = panel.archive.current_token;
                } else {
                    extract_request.archive_path =
                        z7::ui::archive_support::to_native_string(panel.archive.source_archive);
                }
                extract_request.archive_type_hint =
                    z7::ui::archive_support::to_utf8_string(panel.archive.type_hint.trimmed());
                extract_request.overwrite_mode = z7::app::OverwriteMode::kOverwrite;

                if (!is_virtual_root) {
                    extract_request.entries.push_back(z7::ui::archive_support::to_utf8_string(request.archive_entry));
                }
                extract_request.output_dir = z7::ui::archive_support::to_native_string(destination_directory);
                extract_request.path_mode = z7::app::ExtractPathMode::kFullPaths;
                extract_request.eliminate_root_duplication = false;
                z7::app::ExtractPathRemap remap;
                remap.match_kind = z7::app::ExtractPathRemapMatchKind::kRequestRoot;
                remap.destination_path = z7::ui::archive_support::to_native_string(request.destination_path);
                extract_request.path_remaps.push_back(std::move(remap));

                uint64_t const configured_limit = z7::ui::runtime_support::configured_extract_memory_limit_bytes();
                if (configured_limit != 0) {
                    extract_request.configured_memory_limit_bytes = configured_limit;
                    extract_request.configured_memory_limit_defined = true;
                }

                z7::app::OperationOutcome const outcome = run_archive_drag_extract_request_blocking(
                    this, z7::app::ArchiveRequest{std::move(extract_request)});
                auto const result = z7::app::outcome_payload_as<z7::app::ExtractResult>(outcome);
                if (!result.has_value() || !result->ok) {
                    out.error = archive_drag_extract_error_message(
                        outcome, result, QStringLiteral("Failed to export archive drag entry to destination."));
                    return out;
                }
                bool const expected_dir = is_virtual_root || request.entry_is_dir;
                bool const destination_exists = destination_info.exists();
                if (!destination_exists
                    || (expected_dir && !destination_info.isDir())
                    || (!expected_dir && destination_info.isDir())) {
                    out.error = QStringLiteral("Archive drag direct export did not create the expected destination: %1")
                                    .arg(QDir::toNativeSeparators(request.destination_path));
                    return out;
                }

                out.ok = true;
                return out;
            });

        if (!result.ok && error != nullptr) {
            *error = result.error;
        }
        return result.ok;
    }

} // namespace z7::ui::filemanager
