// src/ui/filemanager/src/main_window/archive/archive_open_actions_inside.cpp
// Role: Open-inside archive navigation actions.

#include "main_window/deps.h"
#include "main_window/internal.h"

namespace z7::ui::filemanager {

    void MainWindow::open_archive_file_inside_for_panel(int panel_index,
                                                        QString const& entry_path,
                                                        QString const& archive_type_hint,
                                                        std::optional<uint32_t> archive_index) {
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

        QString const override_hint = archive_type_hint.trimmed();
        QString const effective_hint = override_hint.isEmpty() ? panel.archive.type_hint : override_hint;
        QString const archive_path = panel.archive.source_archive;
        QString const origin_dir = panel.archive.origin_dir;
        QString const display_source = panel.archive_display_source();
        QString nested_display = display_source;
        if (nested_display.isEmpty()) {
            nested_display = archive_path;
        }
        nested_display += QLatin1Char('/');
        nested_display += QDir::toNativeSeparators(normalized_entry);
        auto out_session_result = std::make_shared<std::optional<z7::app::OpenArchiveSessionResult>>();

        start_task_with_runner(
            QStringLiteral("%1: %2").arg(z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(541)),
                                         nested_display),
            z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(541)),
            [parent_token = panel.archive.current_token,
             normalized_entry,
             effective_hint,
             archive_index,
             nested_display,
             out_session_result](ArchiveProcessRunner* runner) {
                if (runner == nullptr) {
                    return false;
                }
                if (archive_index.has_value()) {
                    return runner->start_open_nested(
                        parent_token, *archive_index, effective_hint, 0, nested_display, out_session_result);
                }
                return runner->start_open_nested_by_path(
                    parent_token, normalized_entry, effective_hint, 0, nested_display, out_session_result);
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
                    || !out_session_result->has_value()
                    || !out_session_result->value().token.is_valid()) {
                    return;
                }

                PanelController& current_panel = panel_controller(panel_index);
                current_panel.push_current_archive_to_parent_stack();

                z7::app::ArchiveSessionToken const child_token = out_session_result->value().token;
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
                auto const commit_nested_open = [this, panel_index, normalized_entry, nested_open_finished]() {
                    if (*nested_open_finished) {
                        return;
                    }
                    *nested_open_finished = true;
                    PanelController& panel = panel_controller(panel_index);
                    panel.archive.archive_entry_from_parent = normalized_entry;
                    panel.archive.temp_session.clear();
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
            });
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
        auto finish_failed = [options]() {
            if (options.open_failure_fallback) {
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
            finish_failed();
            return;
        }

        QString const source_archive = archive_info.absoluteFilePath();
        QString const origin_dir = archive_info.absolutePath();
        QString const type_hint = options.archive_type_hint.trimmed();
        if (in_archive_view_for_panel(panel_index)) {
            close_archive_view_for_panel(panel_index, [this, panel_index, source_archive, options](bool ok) mutable {
                if (!ok) {
                    if (options.open_failure_fallback) {
                        options.open_failure_fallback();
                    }
                    if (options.finished_cb) {
                        options.finished_cb(false);
                    }
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
            [this, panel_index, source_archive, origin_dir, type_hint, out_session_result, options](
                bool ok, int, int, QString const&, z7::app::OperationOutcome const&) {
                if (!ok) {
                    if (options.open_failure_fallback) {
                        options.open_failure_fallback();
                    }
                    if (options.finished_cb) {
                        options.finished_cb(false);
                    }
                    return;
                }
                if (out_session_result == nullptr
                    || !out_session_result->has_value()
                    || !out_session_result->value().token.is_valid()) {
                    if (options.open_failure_fallback) {
                        options.open_failure_fallback();
                    }
                    if (options.finished_cb) {
                        options.finished_cb(false);
                    }
                    return;
                }

                z7::app::ArchiveSessionToken const session_token = out_session_result->value().token;
                bool const started = load_archive_virtual_directory_for_panel(
                    panel_index,
                    source_archive,
                    QString(),
                    origin_dir,
                    type_hint,
                    true,
                    [this, panel_index, session_token, options](bool loaded) {
                        if (!loaded) {
                            close_archive_sessions_async(QVector<z7::app::ArchiveSessionToken>{session_token});
                            if (options.open_failure_fallback) {
                                options.open_failure_fallback();
                            }
                            if (options.finished_cb) {
                                options.finished_cb(false);
                            }
                            return;
                        }
                        panel_controller(panel_index).archive.archive_entry_from_parent.clear();
                        set_active_panel(panel_index);
                        if (options.finished_cb) {
                            options.finished_cb(true);
                        }
                    },
                    false,
                    {},
                    session_token,
                    source_archive,
                    options.task_ui_mode);
                if (!started) {
                    close_archive_sessions_async(QVector<z7::app::ArchiveSessionToken>{session_token});
                }
            },
            options.task_ui_mode,
            options.open_failure_fallback
                ? std::function<bool(int, QString const&)>([](int, QString const&) { return false; })
                : std::function<bool(int, QString const&)>());
        if (!started) {
            finish_failed();
        }
    }

} // namespace z7::ui::filemanager
