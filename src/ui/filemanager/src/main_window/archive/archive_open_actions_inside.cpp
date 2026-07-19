// src/ui/filemanager/src/main_window/archive/archive_open_actions_inside.cpp
// Role: Open-inside archive navigation actions.

#include "main_window/deps.h"
#include "main_window/internal.h"

namespace z7::ui::filemanager {

    void MainWindow::open_archive_file_inside_for_panel(int panel_index,
                                                        QString const& entry_path,
                                                        QString const& archive_type_hint,
                                                        std::optional<uint32_t> archive_index,
                                                        bool allow_external_fallback) {
        PanelController& panel = panel_controller(panel_index);
        if (!in_archive_view_for_panel(panel_index)
            || panel.archive.source_archive.isEmpty()
            || !panel.archive.current_token.is_valid()) {
            return;
        }

        QString const normalized_entry = z7::ui::archive_support::normalize_virtual_dir(entry_path);
        if (normalized_entry.isEmpty()) {
            return;
        }

        QString const effective_hint = archive_type_hint.trimmed();
        QString const archive_path = panel.archive.source_archive;
        QString const origin_dir = panel.archive.origin_dir;
        QString const display_source = panel.archive_display_source();
        QString nested_display = display_source;
        if (nested_display.isEmpty()) {
            nested_display = archive_path;
        }
        nested_display += QLatin1Char('/');
        nested_display += QDir::toNativeSeparators(normalized_entry);
        auto out_session_result = std::make_shared<std::optional<z7::app::OpenArchiveFromParentResult>>();

        start_task_with_runner(
            QStringLiteral("%1: %2").arg(z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(541)),
                                         nested_display),
            z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(541)),
            [parent_token = panel.archive.current_token,
             normalized_entry,
             effective_hint,
             archive_index,
             nested_display,
             out_session_result,
             allow_external_fallback](ArchiveProcessRunner* runner) {
                if (runner == nullptr) {
                    return false;
                }
                if (archive_index.has_value()) {
                    return runner->start_open_nested(
                        parent_token,
                        *archive_index,
                        effective_hint,
                        0,
                        nested_display,
                        out_session_result,
                        std::nullopt,
                        allow_external_fallback
                            ? z7::app::UnsupportedNestedOpenMode::kMaterializeForExternalOpen
                            : z7::app::UnsupportedNestedOpenMode::kFail);
                }
                return runner->start_open_nested_by_path(
                    parent_token,
                    normalized_entry,
                    effective_hint,
                    0,
                    nested_display,
                    out_session_result,
                    std::nullopt,
                    allow_external_fallback
                        ? z7::app::UnsupportedNestedOpenMode::kMaterializeForExternalOpen
                        : z7::app::UnsupportedNestedOpenMode::kFail);
            },
            [this,
             panel_index,
             normalized_entry,
             effective_hint,
             nested_display,
             origin_dir,
             archive_path,
             out_session_result](bool ok, int, int, QString const&, z7::app::OperationOutcome const&) {
                if (!ok) {
                    return;
                }
                if (out_session_result == nullptr
                    || !out_session_result->has_value()) {
                    return;
                }
                z7::app::OpenArchiveFromParentResult const& open_result = out_session_result->value();
                if (open_result.disposition == z7::app::OpenArchiveFromParentResult::Disposition::kExternalFile) {
                    if (!open_result.external_file.valid()) {
                        return;
                    }
                    QString const file_path = QString::fromStdString(open_result.external_file.file_path());
                    QSharedPointer<ArchiveTempSession> session(new ArchiveTempSession);
                    session->purpose = ArchiveTempSessionPurpose::kOpenOutside;
                    session->external_file_lease = open_result.external_file;
                    session->command_caption =
                        z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(540));
                    launch_archive_temp_session_outside(
                        session, QStringList{file_path}, QFileInfo(file_path).absolutePath());
                    return;
                }
                if (!open_result.token.is_valid()) {
                    return;
                }

                PanelController& current_panel = panel_controller(panel_index);
                current_panel.push_current_archive_to_parent_stack();

                z7::app::ArchiveSessionToken const child_token = open_result.token;
                std::optional<uint32_t> const parent_entry_index =
                    open_result.parent_entry_index;
                auto nested_open_finished = std::make_shared<bool>(false);
                auto const rollback_nested_open = [this, panel_index, child_token, nested_open_finished]() {
                    if (*nested_open_finished) {
                        return;
                    }
                    *nested_open_finished = true;
                    PanelController& panel = panel_controller(panel_index);
                    panel.discard_last_parent_archive_frame();
                    close_archive_sessions_async(QVector<z7::app::ArchiveSessionToken>{child_token});
                };
                auto const commit_nested_open =
                    [this, panel_index, normalized_entry, parent_entry_index, nested_open_finished]() {
                    if (*nested_open_finished) {
                        return;
                    }
                    *nested_open_finished = true;
                    PanelController& panel = panel_controller(panel_index);
                    panel.archive.archive_entry_from_parent = normalized_entry;
                    panel.archive.parent_entry_index = parent_entry_index;
                    panel.archive.temp_session.clear();
                    panel.archive.filename_code_page.reset();
                    set_active_panel(panel_index);
                };

                bool const started = load_archive_virtual_directory_for_panel(
                    panel_index,
                    archive_path,
                    QString(),
                    origin_dir,
                    effective_hint,
                    true,
                    [rollback_nested_open, commit_nested_open](bool loaded) {
                        if (!loaded) {
                            rollback_nested_open();
                            return;
                        }
                        commit_nested_open();
                    },
                    false,
                    {},
                    child_token,
                    nested_display);
                if (!started) {
                    // load_archive_virtual_directory_for_panel() delivers finished(false)
                    // when it rejects startup, so rollback is centralized in that callback.
                    return;
                }
            },
            RunnerTaskUiMode::kDelayed);
    }

    void MainWindow::open_archive_inside(QString const& archive_path, QString const& archive_type_hint) {
        OpenArchiveInsideOptions options;
        options.archive_type_hint = archive_type_hint;
        open_archive_inside(archive_path, std::move(options));
    }

    void MainWindow::open_archive_inside_for_panel(int panel_index,
                                                   QString const& archive_path,
                                                   QString const& archive_type_hint) {
        OpenArchiveInsideOptions options;
        options.archive_type_hint = archive_type_hint;
        open_archive_inside_for_panel(panel_index, archive_path, std::move(options));
    }

    void MainWindow::open_archive_inside(QString const& archive_path, OpenArchiveInsideOptions options) {
        open_archive_inside_for_panel(active_panel_index_, archive_path, std::move(options));
    }

    void MainWindow::open_archive_inside_for_panel(int panel_index,
                                                   QString const& archive_path,
                                                   OpenArchiveInsideOptions options) {
        auto const failure_matches_fallback_policy =
            [policy = options.open_failure_fallback_policy](z7::app::ArchiveErrorDomain error_domain) {
                switch (policy) {
                case OpenFailureFallbackPolicy::kAnyFailure:
                    return true;
                case OpenFailureFallbackPolicy::kUnsupportedFormatOnly:
                    return error_domain == z7::app::ArchiveErrorDomain::kUnsupportedFormat;
                }
                return false;
            };
        auto const finish_failed = [options, failure_matches_fallback_policy](
                                       z7::app::ArchiveErrorDomain error_domain) {
            if (options.open_failure_fallback && failure_matches_fallback_policy(error_domain)) {
                options.open_failure_fallback();
            }
            if (options.finished_cb) {
                options.finished_cb(false);
            }
        };

        QFileInfo const archive_info(archive_path);
        if (!archive_info.exists() || !archive_info.isFile()) {
            QMessageBox::warning(
                this, z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(541)), archive_path);
            finish_failed(z7::app::ArchiveErrorDomain::kIo);
            return;
        }

        QString const source_archive = archive_info.absoluteFilePath();
        QString const origin_dir = archive_info.absolutePath();
        QString const type_hint = options.archive_type_hint.trimmed();
        if (in_archive_view_for_panel(panel_index)) {
            close_archive_view_for_panel(
                panel_index,
                [this, panel_index, source_archive, options, finish_failed](bool ok) mutable {
                    if (!ok) {
                        finish_failed(z7::app::ArchiveErrorDomain::kUnknown);
                        return;
                    }
                    open_archive_inside_for_panel(panel_index, source_archive, std::move(options));
                });
            return;
        }
        auto out_session_result = std::make_shared<std::optional<z7::app::OpenArchiveSessionResult>>();

        bool const started = start_task_with_runner(
            QStringLiteral("%1: %2").arg(z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(541)),
                                         source_archive),
            z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(541)),
            [source_archive, type_hint, out_session_result](ArchiveProcessRunner* runner) {
                return runner != nullptr && runner->start_open_from_path(source_archive, type_hint, out_session_result);
            },
            [this, panel_index, source_archive, origin_dir, type_hint, out_session_result, options, finish_failed](
                bool ok, int, int error_domain, QString const&, z7::app::OperationOutcome const&) {
                if (!ok) {
                    finish_failed(static_cast<z7::app::ArchiveErrorDomain>(error_domain));
                    return;
                }
                if (out_session_result == nullptr
                    || !out_session_result->has_value()
                    || !out_session_result->value().token.is_valid()) {
                    finish_failed(z7::app::ArchiveErrorDomain::kUnknown);
                    return;
                }

                z7::app::ArchiveSessionToken const session_token = out_session_result->value().token;
                auto const list_failure_domain = std::make_shared<z7::app::ArchiveErrorDomain>(
                    z7::app::ArchiveErrorDomain::kUnknown);
                bool const started = load_archive_virtual_directory_for_panel(
                    panel_index,
                    source_archive,
                    QString(),
                    origin_dir,
                    type_hint,
                    true,
                    [this, panel_index, session_token, options, finish_failed, list_failure_domain](bool loaded) {
                        if (!loaded) {
                            close_archive_sessions_async(QVector<z7::app::ArchiveSessionToken>{session_token});
                            finish_failed(*list_failure_domain);
                            return;
                        }
                        panel_controller(panel_index).archive.archive_entry_from_parent.clear();
                        panel_controller(panel_index).archive.parent_entry_index.reset();
                        set_active_panel(panel_index);
                        if (options.finished_cb) {
                            options.finished_cb(true);
                        }
                    },
                    false,
                    [list_failure_domain](int error_domain, QString const&) {
                        *list_failure_domain = static_cast<z7::app::ArchiveErrorDomain>(error_domain);
                    },
                    session_token,
                    source_archive,
                    options.task_ui_mode);
                if (!started) {
                    close_archive_sessions_async(QVector<z7::app::ArchiveSessionToken>{session_token});
                }
            },
            options.task_ui_mode,
            options.open_failure_fallback
                ? std::function<bool(int, QString const&)>(
                      [failure_matches_fallback_policy](int error_domain, QString const&) {
                          return !failure_matches_fallback_policy(
                              static_cast<z7::app::ArchiveErrorDomain>(error_domain));
                      })
                : std::function<bool(int, QString const&)>());
        if (!started) {
            finish_failed(z7::app::ArchiveErrorDomain::kBackendUnavailable);
        }
    }

} // namespace z7::ui::filemanager
