// src/ui/gui/src/gui_task_runner/shared.cpp
// Role: Shared private helpers for GuiTaskRunner blocking and async flows.

#include "shared.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <optional>
#include <sstream>
#include <string>
#include <utility>

#include "../gui_task_runner_helpers.h"
#include "archive_delegate_qt.h"
#include "archive_error.h"
#include "archive_failure_messages.h"
#include "archive_format.h"
#include "archive_string_codec_qt.h"
#include "common/archive_type_normalization.h"
#include "extract_memory_settings.h"
#include "platform_support.h"

namespace z7::ui::gui::gui_task_runner_shared {
    namespace {

        std::optional<z7::ui::runtime_support::TaskProgressRatioInfo>
        to_task_progress_ratio_info(std::optional<z7::app::ProgressRatioInfo> const& ratio_info) {
            if (!ratio_info.has_value()) {
                return std::nullopt;
            }
            z7::ui::runtime_support::TaskProgressRatioInfo out;
            out.input_size_known = ratio_info->input_size_known;
            out.input_size = static_cast<quint64>(ratio_info->input_size);
            out.output_size_known = ratio_info->output_size_known;
            out.output_size = static_cast<quint64>(ratio_info->output_size);
            out.compressing_mode = ratio_info->compressing_mode;
            return out;
        }

        void apply_log_to_dialog(z7::ui::runtime_support::TaskProgressDialogBase* dialog,
                                 GuiTaskRunResult* out,
                                 z7::app::ArchiveLog const& log) {
            if (dialog == nullptr) {
                return;
            }
            QString const stage_text = z7::ui::archive_support::stage_label_for(log.stage);
            QString const log_line = z7::ui::archive_support::from_local8_string(log.message);
            if (!stage_text.isEmpty()) {
                dialog->set_stage(stage_text);
            }
            if (out != nullptr && !log_line.isEmpty()) {
                QString const display_log_line = log.channel == z7::app::OutputChannel::kStdErr
                                                   ? z7::ui::runtime_support::localize_archive_failure_message(log_line)
                                                   : log_line;
                out->log_lines.push_back(display_log_line);
                if (log.channel == z7::app::OutputChannel::kStdErr) {
                    out->result_messages.push_back(display_log_line);
                    dialog->append_failure_result_message(display_log_line);
                }
                dialog->append_log(display_log_line);
            }
        }

        void apply_progress_to_dialog(z7::ui::runtime_support::TaskProgressDialogBase* dialog,
                                      GuiTaskRunResult* out,
                                      z7::app::ProgressSnapshot const& progress) {
            if (dialog == nullptr) {
                return;
            }
            QString const stage_text = z7::ui::archive_support::stage_label_for(progress.stage);
            QString const log_line = z7::ui::archive_support::from_local8_string(progress.message);
            QString const current_path = z7::ui::archive_support::from_local8_string(progress.current_path);
            if (!stage_text.isEmpty()) {
                dialog->set_stage(stage_text);
            }
            if (out != nullptr && !log_line.isEmpty()) {
                out->log_lines.push_back(log_line);
                dialog->append_log(log_line);
            }
            dialog->set_percent(progress.percent);
            dialog->set_detailed_progress(progress.totals_known,
                                          static_cast<quint64>(progress.total_bytes),
                                          static_cast<quint64>(progress.completed_bytes),
                                          static_cast<quint64>(progress.total_files),
                                          static_cast<quint64>(progress.completed_files),
                                          static_cast<quint64>(progress.error_count),
                                          to_task_progress_ratio_info(progress.ratio_info),
                                          current_path);
        }

        class ProgressDialogArchiveDelegate final
            : public z7::ui::archive_support::OwnerRelayDelegate<z7::ui::runtime_support::TaskProgressDialogBase> {
        public:
            using FinishedCallback = std::function<void(z7::app::OperationOutcome const&)>;

            ProgressDialogArchiveDelegate(z7::ui::runtime_support::TaskProgressDialogBase* dialog,
                                          GuiTaskRunResult* out,
                                          FinishedCallback on_finished,
                                          PasswordPromptParentProvider password_prompt_parent_provider,
                                          std::shared_ptr<PasswordRetryState> password_retry_state) :
                z7::ui::archive_support::OwnerRelayDelegate<z7::ui::runtime_support::TaskProgressDialogBase>(
                    dialog,
                    std::move(on_finished),
                    QCoreApplication::instance(),
                    z7::ui::archive_support::MissingTargetPolicy::kInvokeDirect),
                out_(out),
                password_prompt_parent_provider_(std::move(password_prompt_parent_provider)),
                password_retry_state_(std::move(password_retry_state)) {}

            void on_log(z7::app::ArchiveLog const& log) override {
                z7::app::ArchiveLog const log_copy = log;
                post_to_owner([out = out_, log_copy = std::move(log_copy)](
                                  z7::ui::runtime_support::TaskProgressDialogBase* dialog) {
                    apply_log_to_dialog(dialog, out, log_copy);
                });
            }

            void on_progress(z7::app::ProgressSnapshot const& progress) override {
                z7::app::ProgressSnapshot progress_copy = progress;
                post_to_owner([out = out_, progress_copy = std::move(progress_copy)](
                                  z7::ui::runtime_support::TaskProgressDialogBase* dialog) {
                    apply_progress_to_dialog(dialog, out, progress_copy);
                });
            }

            std::optional<z7::app::OverwriteDecision>
            request_overwrite(z7::app::OverwritePrompt const& prompt) override {
                z7::ui::runtime_support::TaskProgressDialogBase* dialog = owner();
                return z7::ui::archive_support::call_on_target_blocking<z7::app::OverwriteDecision>(
                    dialog,
                    prompt,
                    z7::app::OverwriteDecision::kCancel,
                    [dialog](z7::app::OverwritePrompt const& prompt_value) {
                        if (dialog == nullptr) {
                            return z7::app::OverwriteDecision::kCancel;
                        }
                        QWidget* prompt_dialog = dialog;
                        return show_overwrite_prompt_dialog(prompt_dialog, prompt_value);
                    });
            }

            std::optional<z7::app::PasswordReply> request_password(z7::app::PasswordPrompt const& prompt) override {
                z7::ui::runtime_support::TaskProgressDialogBase* dialog = owner();
                return z7::ui::archive_support::call_on_target_blocking<z7::app::PasswordReply>(
                    dialog,
                    prompt,
                    z7::app::PasswordReply{},
                    [this, dialog](z7::app::PasswordPrompt const& prompt_value) {
                        if (password_retry_state_ && password_retry_state_->next_password.has_value()) {
                            z7::app::PasswordReply retry_reply;
                            retry_reply.kind = z7::app::PasswordReplyKind::kProvide;
                            retry_reply.password = std::move(*password_retry_state_->next_password);
                            password_retry_state_->next_password.reset();
                            password_retry_state_->prompt_canceled = false;
                            return retry_reply;
                        }

                        QWidget* prompt_dialog = nullptr;
                        if (password_prompt_parent_provider_) {
                            prompt_dialog = password_prompt_parent_provider_();
                        }
                        if (prompt_dialog == nullptr) {
                            prompt_dialog = dialog;
                        }
                        if (prompt_dialog == nullptr) {
                            return z7::app::PasswordReply{};
                        }
                        z7::app::PasswordReply reply = show_password_prompt_dialog(prompt_dialog, prompt_value);
                        if (password_retry_state_) {
                            password_retry_state_->prompt_canceled = reply.kind != z7::app::PasswordReplyKind::kProvide;
                        }
                        return reply;
                    });
            }

            std::optional<z7::app::ChoiceReply> request_choice(z7::app::ChoicePrompt const& prompt) override {
                z7::ui::runtime_support::TaskProgressDialogBase* dialog = owner();
                return z7::ui::archive_support::call_on_target_blocking<z7::app::ChoiceReply>(
                    dialog, prompt, z7::app::ChoiceReply{}, [dialog](z7::app::ChoicePrompt const& prompt_value) {
                        if (dialog == nullptr) {
                            return z7::app::ChoiceReply{};
                        }
                        QWidget* prompt_dialog = dialog;
                        return show_choice_prompt_dialog(prompt_dialog, prompt_value);
                    });
            }

            std::optional<z7::app::MemoryLimitReply>
            request_memory_limit(z7::app::MemoryLimitPrompt const& prompt) override {
                z7::ui::runtime_support::TaskProgressDialogBase* dialog = owner();
                return z7::ui::archive_support::call_on_target_blocking<z7::app::MemoryLimitReply>(
                    dialog,
                    prompt,
                    z7::app::MemoryLimitReply{},
                    [dialog](z7::app::MemoryLimitPrompt const& prompt_value) {
                        if (dialog == nullptr) {
                            return z7::app::MemoryLimitReply{};
                        }
                        QWidget* prompt_dialog = dialog;
                        return show_memory_limit_prompt_dialog(prompt_dialog, prompt_value);
                    });
            }

        private:
            GuiTaskRunResult* out_ = nullptr;
            PasswordPromptParentProvider password_prompt_parent_provider_;
            std::shared_ptr<PasswordRetryState> password_retry_state_;
        };

    } // namespace

    QString localize_failure_message(QString message) {
        return z7::ui::runtime_support::localize_archive_failure_message(std::move(message));
    }

    std::shared_ptr<z7::app::IArchiveDelegate>
    make_progress_dialog_delegate(z7::ui::runtime_support::TaskProgressDialogBase* dialog,
                                  GuiTaskRunResult* out,
                                  std::function<void(z7::app::OperationOutcome const&)> on_finished,
                                  PasswordPromptParentProvider password_prompt_parent_provider,
                                  std::shared_ptr<PasswordRetryState> password_retry_state) {
        return std::make_shared<ProgressDialogArchiveDelegate>(dialog,
                                                               out,
                                                               std::move(on_finished),
                                                               std::move(password_prompt_parent_provider),
                                                               std::move(password_retry_state));
    }

    void prepare_progress_dialog(z7::ui::runtime_support::TaskProgressDialogBase* dialog,
                                 QString const& title,
                                 bool test_mode) {
        if (dialog == nullptr) {
            return;
        }
        dialog->set_header(title);
        dialog->set_test_mode(test_mode);
        dialog->set_pause_available(true);
        dialog->set_running(true);
    }

    SessionControlBindings bind_session_controls(z7::ui::runtime_support::TaskProgressDialogBase* dialog,
                                                 z7::app::ArchiveSession const& session,
                                                 SharedTaskCancellation cancel_requested) {
        SessionControlBindings bindings;
        if (dialog == nullptr || !session.valid()) {
            return bindings;
        }

        bindings.cancel_connection =
            QObject::connect(dialog,
                             &z7::ui::runtime_support::TaskProgressDialogBase::cancel_requested,
                             [session]() mutable { session.cancel(); });
        bindings.pause_connection = QObject::connect(dialog,
                                                     &z7::ui::runtime_support::TaskProgressDialogBase::pause_requested,
                                                     [session]() mutable { session.pause(); });
        bindings.resume_connection =
            QObject::connect(dialog,
                             &z7::ui::runtime_support::TaskProgressDialogBase::resume_requested,
                             [session]() mutable { session.resume(); });
        if (cancel_requested) {
            if (cancel_requested->is_canceled()) {
                session.cancel();
            } else {
                bindings.remote_cancel_connection = QObject::connect(
                    cancel_requested.data(),
                    &TaskCancellation::cancel_requested,
                    dialog,
                    [session]() mutable { session.cancel(); },
                    Qt::QueuedConnection);
            }
        }
        return bindings;
    }

    void release_session_controls(SessionControlBindings* bindings) {
        if (bindings == nullptr) {
            return;
        }
        if (bindings->remote_cancel_connection) {
            QObject::disconnect(bindings->remote_cancel_connection);
        }
        QObject::disconnect(bindings->cancel_connection);
        QObject::disconnect(bindings->pause_connection);
        QObject::disconnect(bindings->resume_connection);
        *bindings = SessionControlBindings{};
    }

    QString canceled_export_message() {
        return QStringLiteral("Operation canceled.");
    }

    bool is_export_canceled(SharedTaskCancellation const& cancel_requested) {
        return cancel_requested && cancel_requested->is_canceled();
    }

    QString normalize_virtual_entry_path(QString entry) {
        QString out = QDir::fromNativeSeparators(entry.trimmed());
        while (out.startsWith(QLatin1Char('/'))) {
            out.remove(0, 1);
        }
        while (out.endsWith(QLatin1Char('/'))) {
            out.chop(1);
        }
        while (out.contains(QStringLiteral("//"))) {
            out.replace(QStringLiteral("//"), QStringLiteral("/"));
        }
        return out;
    }

    QString infer_archive_format(QString const& archive_path, QString const& type_hint) {
        QString const trimmed_hint = type_hint.trimmed();
        if (!trimmed_hint.isEmpty()) {
            return QString::fromStdString(z7::common::normalize_archive_type_token_copy(trimmed_hint.toStdString()));
        }
        std::string const suffix = QFileInfo(archive_path).suffix().toLower().toStdString();
        std::string const format = z7::common::canonical_archive_type_from_filename_suffix_copy(suffix);
        return format.empty() ? QStringLiteral("7z") : QString::fromStdString(format);
    }

    QString normalized_mode_token(QString value) {
        value = value.trimmed().toLower();
        while (value.startsWith(QLatin1Char('-'))) {
            value.remove(0, 1);
        }
        value.replace(QLatin1Char('-'), QLatin1Char('_'));
        value.replace(QLatin1Char(' '), QLatin1Char('_'));
        return value;
    }

    z7::app::OverwriteMode parse_archive_export_overwrite_mode(QString const& overwrite_mode) {
        QString const token = normalized_mode_token(overwrite_mode);
        if (token.isEmpty() || token == QStringLiteral("ask") || token == QStringLiteral("ask_before")) {
            return z7::app::OverwriteMode::kAsk;
        }
        if (token == QStringLiteral("overwrite")
            || token == QStringLiteral("replace")
            || token == QStringLiteral("aoa")
            || token == QStringLiteral("-aoa")) {
            return z7::app::OverwriteMode::kOverwrite;
        }
        if (token == QStringLiteral("skip") || token == QStringLiteral("aos") || token == QStringLiteral("-aos")) {
            return z7::app::OverwriteMode::kSkip;
        }
        if (token == QStringLiteral("rename_existing")
            || token == QStringLiteral("rename_old")
            || token == QStringLiteral("aot")
            || token == QStringLiteral("-aot")) {
            return z7::app::OverwriteMode::kRenameExisting;
        }
        if (token == QStringLiteral("rename_extracted")
            || token == QStringLiteral("rename_new")
            || token == QStringLiteral("rename")
            || token == QStringLiteral("aou")
            || token == QStringLiteral("-aou")) {
            return z7::app::OverwriteMode::kRenameExtracted;
        }
        return z7::app::OverwriteMode::kAsk;
    }

    z7::app::ExtractPathMode parse_archive_export_path_mode(QString const& path_mode) {
        QString const token = normalized_mode_token(path_mode);
        if (token == QStringLiteral("no")
            || token == QStringLiteral("no_paths")
            || token == QStringLiteral("nopaths")) {
            return z7::app::ExtractPathMode::kNoPaths;
        }
        if (token == QStringLiteral("absolute")
            || token == QStringLiteral("abs")
            || token == QStringLiteral("absolute_paths")) {
            return z7::app::ExtractPathMode::kAbsolutePaths;
        }
        return z7::app::ExtractPathMode::kFullPaths;
    }

    z7::app::ExtractZoneIdMode parse_extract_zone_id_mode(QString const& zone_id_mode) {
        QString const token = normalized_mode_token(zone_id_mode);
        if (token == QStringLiteral("all") || token == QStringLiteral("yes") || token == QStringLiteral("1")) {
            return z7::app::ExtractZoneIdMode::kAll;
        }
        if (token == QStringLiteral("office") || token == QStringLiteral("2")) {
            return z7::app::ExtractZoneIdMode::kOffice;
        }
        return z7::app::ExtractZoneIdMode::kNone;
    }

    z7::app::ExtractPathRemapMatchKind
    to_archive_extract_remap_match_kind(z7::ui::gui::ExtractPathRemapMatchKind kind) {
        switch (kind) {
            case z7::ui::gui::ExtractPathRemapMatchKind::kExactArchivePath:
                return z7::app::ExtractPathRemapMatchKind::kExactArchivePath;
            case z7::ui::gui::ExtractPathRemapMatchKind::kArchivePrefix:
                return z7::app::ExtractPathRemapMatchKind::kArchivePrefix;
            case z7::ui::gui::ExtractPathRemapMatchKind::kRequestRoot:
            default:
                return z7::app::ExtractPathRemapMatchKind::kRequestRoot;
        }
    }

    QString outcome_summary(z7::app::OperationOutcome const& outcome) {
        QString summary = z7::ui::archive_support::from_utf8_string(outcome.summary).trimmed();
        if (!summary.isEmpty()) {
            return summary;
        }
        return z7::ui::archive_support::from_utf8_string(z7::app::describe_archive_error(outcome.error)).trimmed();
    }

    int result_code_for_failure(z7::app::ArchiveErrorDomain const domain) {
        switch (domain) {
            case z7::app::ArchiveErrorDomain::kInvalidArguments:
                return 1;
            case z7::app::ArchiveErrorDomain::kIo:
                return 3;
            case z7::app::ArchiveErrorDomain::kCanceled:
                return 5;
            default:
                return 2;
        }
    }

    GuiTaskRunResult make_failure_result(int code, z7::app::ArchiveErrorDomain domain, QString const& message) {
        GuiTaskRunResult result;
        result.result = z7::app::make_immediate_result(code, domain, z7::ui::archive_support::to_utf8_string(message));
        return result;
    }

    void apply_configured_extract_memory_limit(z7::app::TestRequest* request) {
        if (request == nullptr) {
            return;
        }
        uint64_t const bytes = z7::ui::runtime_support::configured_extract_memory_limit_bytes();
        if (bytes == 0) {
            return;
        }
        request->configured_memory_limit_bytes = bytes;
        request->configured_memory_limit_defined = true;
    }

    void apply_configured_extract_memory_limit(z7::app::ExtractRequest* request) {
        if (request == nullptr) {
            return;
        }
        uint64_t const bytes = z7::ui::runtime_support::configured_extract_memory_limit_bytes();
        if (bytes == 0) {
            return;
        }
        request->configured_memory_limit_bytes = bytes;
        request->configured_memory_limit_defined = true;
    }

    bool build_archive_export_task_options(z7::ui::gui::ArchiveExportTaskSpec const& spec,
                                           ArchiveExportTaskOptions* out,
                                           QString* error_message) {
        if (error_message != nullptr) {
            error_message->clear();
        }
        if (out == nullptr) {
            if (error_message != nullptr) {
                *error_message = QStringLiteral("Archive export task output is null.");
            }
            return false;
        }

        ArchiveExportTaskOptions options;
        QString const archive_path = z7::ui::archive_support::from_native_string(spec.root_archive_path);
        if (archive_path.isEmpty()) {
            if (error_message != nullptr) {
                *error_message = QStringLiteral("Archive export task root archive path is empty.");
            }
            return false;
        }
        QFileInfo const archive_info(archive_path);
        if (!archive_info.exists() || !archive_info.isFile()) {
            if (error_message != nullptr) {
                *error_message = QStringLiteral("Archive export task root archive path does not exist.");
            }
            return false;
        }
        options.archive_path = archive_info.absoluteFilePath();
        options.archive_type_hint =
            QString::fromStdString(z7::common::normalize_archive_type_token_copy(spec.root_archive_type));

        QString output_dir = z7::ui::archive_support::from_native_string(spec.output_dir);
        if (output_dir.isEmpty()) {
            if (error_message != nullptr) {
                *error_message = QStringLiteral("Archive export task output directory is empty.");
            }
            return false;
        }
        output_dir = QDir::fromNativeSeparators(output_dir);
        if (QDir::isRelativePath(output_dir)) {
            output_dir = QDir::current().absoluteFilePath(output_dir);
        }
        options.output_dir = QDir::cleanPath(output_dir);

        QStringList entry_paths;
        for (std::string const& value : spec.archive_entry_paths) {
            entry_paths << z7::ui::archive_support::from_utf8_string(value);
        }
        for (QString const& raw_entry : entry_paths) {
            QString const normalized = normalize_virtual_entry_path(raw_entry);
            if (normalized.isEmpty() || QDir::isAbsolutePath(QDir::fromNativeSeparators(raw_entry))) {
                if (error_message != nullptr) {
                    *error_message = QStringLiteral("Archive export task entry path is invalid.");
                }
                return false;
            }
            options.entry_paths << normalized;
        }
        options.entry_paths.removeDuplicates();
        if (options.entry_paths.isEmpty()) {
            if (error_message != nullptr) {
                *error_message = QStringLiteral("Archive export task requires entry paths.");
            }
            return false;
        }

        for (std::string const& value : spec.nested_archive_entries) {
            QString const raw_nested_entry = z7::ui::archive_support::from_utf8_string(value);
            QString const normalized = normalize_virtual_entry_path(raw_nested_entry);
            if (normalized.isEmpty() || QDir::isAbsolutePath(QDir::fromNativeSeparators(raw_nested_entry))) {
                if (error_message != nullptr) {
                    *error_message = QStringLiteral("Archive export task nested archive entry is invalid.");
                }
                return false;
            }
            options.nested_archive_entries << normalized;
        }
        if (spec.filename_code_pages.size() != spec.nested_archive_entries.size() + 1) {
            if (error_message != nullptr) {
                *error_message = QStringLiteral(
                    "Archive export filename code-page count must equal root plus nested layers.");
            }
            return false;
        }
        options.filename_code_pages = spec.filename_code_pages;

        options.overwrite_mode =
            parse_archive_export_overwrite_mode(z7::ui::archive_support::from_native_string(spec.overwrite_mode));
        options.path_mode = parse_archive_export_path_mode(z7::ui::archive_support::from_native_string(spec.path_mode));
        options.eliminate_root_duplication = spec.eliminate_root_duplication;
        options.restore_file_security =
            spec.restore_file_security
            && z7::ui::runtime_support::is_platform_supported(z7::ui::runtime_support::PlatformSupport::kWindowsOnly);
        options.zone_id_mode =
            parse_extract_zone_id_mode(z7::ui::archive_support::from_native_string(spec.zone_id_mode));
        options.password = z7::ui::archive_support::from_utf8_string(spec.password);

        options.path_remaps.reserve(spec.path_remaps.size());
        for (z7::ui::gui::ExtractPathRemap const& remap : spec.path_remaps) {
            z7::app::ExtractPathRemap out_remap;
            out_remap.match_kind = to_archive_extract_remap_match_kind(remap.match_kind);
            QString const source_path = z7::ui::archive_support::from_utf8_string(remap.source_path);
            QString const destination_path =
                z7::ui::archive_support::from_native_string(remap.destination_path);
            if (destination_path.isEmpty()) {
                if (error_message != nullptr) {
                    *error_message = QStringLiteral("Archive export task path remap destination is empty.");
                }
                return false;
            }
            out_remap.source_path = z7::ui::archive_support::to_utf8_string(normalize_virtual_entry_path(source_path));
            QString normalized_destination = QDir::fromNativeSeparators(destination_path);
            if (!normalized_destination.isEmpty() && QDir::isRelativePath(normalized_destination)) {
                normalized_destination = QDir(options.output_dir).absoluteFilePath(normalized_destination);
            }
            out_remap.destination_path =
                z7::ui::archive_support::to_native_string(QDir::cleanPath(normalized_destination));
            options.path_remaps.push_back(std::move(out_remap));
        }

        *out = std::move(options);
        return true;
    }

    bool build_archive_hash_task_options(z7::ui::gui::ArchiveHashTaskSpec const& spec,
                                         ArchiveHashTaskOptions* out,
                                         QString* error_message) {
        if (error_message != nullptr) {
            error_message->clear();
        }
        if (out == nullptr) {
            if (error_message != nullptr) {
                *error_message = QStringLiteral("Archive hash task output is null.");
            }
            return false;
        }

        ArchiveHashTaskOptions options;
        QString const archive_path = z7::ui::archive_support::from_native_string(spec.archive_path);
        if (archive_path.isEmpty()) {
            if (error_message != nullptr) {
                *error_message = QStringLiteral("Archive hash task archive path is empty.");
            }
            return false;
        }

        QFileInfo const archive_info(archive_path);
        if (!archive_info.exists() || !archive_info.isFile()) {
            if (error_message != nullptr) {
                *error_message = QStringLiteral("Archive hash task archive path does not exist.");
            }
            return false;
        }

        options.archive_path = archive_info.absoluteFilePath();
        options.archive_type_hint =
            QString::fromStdString(z7::common::normalize_archive_type_token_copy(spec.archive_type));
        options.hash_method = z7::ui::archive_support::from_native_string(spec.hash_method).trimmed();
        if (options.hash_method.isEmpty()) {
            options.hash_method = QStringLiteral("CRC32");
        }

        QStringList entry_paths;
        for (std::string const& value : spec.archive_entry_paths) {
            entry_paths << z7::ui::archive_support::from_utf8_string(value);
        }
        for (QString const& raw_entry : entry_paths) {
            QString const normalized = normalize_virtual_entry_path(raw_entry);
            if (normalized.isEmpty() || QDir::isAbsolutePath(QDir::fromNativeSeparators(raw_entry))) {
                if (error_message != nullptr) {
                    *error_message = QStringLiteral("Archive hash task entry path is invalid.");
                }
                return false;
            }
            options.entry_paths << normalized;
        }
        options.entry_paths.removeDuplicates();
        if (options.entry_paths.isEmpty()) {
            if (error_message != nullptr) {
                *error_message = QStringLiteral("Archive hash task requires entry paths.");
            }
            return false;
        }

        for (std::string const& value : spec.nested_archive_entries) {
            QString const raw_nested_entry = z7::ui::archive_support::from_utf8_string(value);
            QString const normalized = normalize_virtual_entry_path(raw_nested_entry);
            if (normalized.isEmpty() || QDir::isAbsolutePath(QDir::fromNativeSeparators(raw_nested_entry))) {
                if (error_message != nullptr) {
                    *error_message = QStringLiteral("Archive hash task nested archive entry is invalid.");
                }
                return false;
            }
            options.nested_archive_entries << normalized;
        }
        if (spec.filename_code_pages.size() != spec.nested_archive_entries.size() + 1) {
            if (error_message != nullptr) {
                *error_message = QStringLiteral(
                    "Archive hash filename code-page count must equal root plus nested layers.");
            }
            return false;
        }
        options.filename_code_pages = spec.filename_code_pages;

        *out = std::move(options);
        return true;
    }

    bool build_archive_test_task_options(z7::ui::gui::ArchiveTestTaskSpec const& spec,
                                         ArchiveTestTaskOptions* out,
                                         QString* error_message) {
        if (error_message != nullptr) {
            error_message->clear();
        }
        if (out == nullptr) {
            if (error_message != nullptr) {
                *error_message = QStringLiteral("Archive test task output is null.");
            }
            return false;
        }

        ArchiveTestTaskOptions options;
        QString const archive_path = z7::ui::archive_support::from_native_string(spec.archive_path);
        if (archive_path.isEmpty()) {
            if (error_message != nullptr) {
                *error_message = QStringLiteral("Archive test task archive path is empty.");
            }
            return false;
        }
        QFileInfo const archive_info(archive_path);
        if (!archive_info.exists() || !archive_info.isFile()) {
            if (error_message != nullptr) {
                *error_message = QStringLiteral("Archive test task archive path does not exist.");
            }
            return false;
        }

        options.archive_path = archive_info.absoluteFilePath();
        options.archive_type_hint =
            QString::fromStdString(z7::common::normalize_archive_type_token_copy(spec.archive_type));
        QStringList entry_paths;
        for (std::string const& value : spec.archive_entry_paths) {
            entry_paths << z7::ui::archive_support::from_utf8_string(value);
        }
        for (QString const& raw_entry : entry_paths) {
            QString const normalized = normalize_virtual_entry_path(raw_entry);
            if (normalized.isEmpty() || QDir::isAbsolutePath(QDir::fromNativeSeparators(raw_entry))) {
                if (error_message != nullptr) {
                    *error_message = QStringLiteral("Archive test task entry path is invalid.");
                }
                return false;
            }
            options.entry_paths << normalized;
        }
        options.entry_paths.removeDuplicates();

        for (std::string const& value : spec.nested_archive_entries) {
            QString const raw_nested_entry = z7::ui::archive_support::from_utf8_string(value);
            QString const normalized = normalize_virtual_entry_path(raw_nested_entry);
            if (normalized.isEmpty() || QDir::isAbsolutePath(QDir::fromNativeSeparators(raw_nested_entry))) {
                if (error_message != nullptr) {
                    *error_message = QStringLiteral("Archive test task nested archive entry is invalid.");
                }
                return false;
            }
            options.nested_archive_entries << normalized;
        }
        if (spec.filename_code_pages.size() != spec.nested_archive_entries.size() + 1) {
            if (error_message != nullptr) {
                *error_message = QStringLiteral(
                    "Archive test filename code-page count must equal root plus nested layers.");
            }
            return false;
        }
        options.filename_code_pages = spec.filename_code_pages;

        *out = std::move(options);
        return true;
    }

} // namespace z7::ui::gui::gui_task_runner_shared
