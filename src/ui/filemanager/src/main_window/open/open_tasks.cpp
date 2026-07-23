// src/ui/filemanager/src/main_window/open/open_tasks.cpp
// Role: Extract/test/hash/refresh and filesystem navigation actions.

#include "main_window/deps.h"
#include "main_window/image_preview/archive_image_preview_controller.h"
#include "main_window/internal.h"

namespace z7::ui::filemanager {

    namespace {

        z7::task_ipc_runtime::TaskIpcFilenameCodePage
        to_task_ipc_filename_code_page(z7::app::FilenameCodePage value) {
            z7::task_ipc_runtime::TaskIpcFilenameCodePage out;
            out.automatic = !value.has_value();
            out.code_page = value.value_or(0);
            return out;
        }

    } // namespace

    namespace {

        struct ArchiveExportDestinationPlan {
            QString output_dir;
            QString path_mode = QStringLiteral("full");
            QString history_path;
            QVector<z7::task_ipc_runtime::TaskIpcExtractPathRemap> path_remaps;
        };

        QString non_empty_label(uint32_t id) {
            return z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(id)).trimmed();
        }

        template <typename PanelControllerLike>
        QStringList oper_smart_archive_export_entries(PanelControllerLike const& panel) {
            if (!panel.in_archive_view() || panel.model == nullptr) {
                return {};
            }
            return panel.oper_smart_archive_entries();
        }

        ArchiveExportDestinationPlan build_archive_export_destination_plan(
            CopyTransferDestinationPlan const& transfer_plan, bool flat_view, QString const& current_virtual) {
            ArchiveExportDestinationPlan plan;
            if (!transfer_plan.valid || transfer_plan.destination_dir.isEmpty()) {
                return plan;
            }

            plan.history_path = transfer_plan.history_path;
            plan.output_dir = transfer_plan.destination_dir;
            if (flat_view) {
                plan.path_mode = QStringLiteral("no");
                return plan;
            }

            QString const normalized_current_virtual = z7::ui::archive_support::normalize_virtual_dir(current_virtual);
            if (!normalized_current_virtual.isEmpty()) {
                z7::task_ipc_runtime::TaskIpcExtractPathRemap remap;
                remap.match_kind = z7::task_ipc_runtime::TaskIpcExtractPathRemapMatchKind::kArchivePrefix;
                remap.source_path = normalized_current_virtual;
                remap.destination_path = transfer_plan.destination_dir;
                plan.path_remaps.push_back(std::move(remap));
            }
            return plan;
        }

    } // namespace

    bool extract_eliminate_root_duplication_from_settings() {
        z7::platform::qt::PortableSettings settings;
        return settings.value(QString::fromLatin1(kSettingsOptionsElimDupExtract), true).toBool();
    }

    QString extract_zone_id_mode_from_settings() {
        z7::platform::qt::PortableSettings settings;
        bool ok = false;
        int const mode = settings.value(QString::fromLatin1(kSettingsOptionsWriteZoneIdExtract), 0).toInt(&ok);
        if (!ok) {
            return QStringLiteral("none");
        }
        if (mode == 1) {
            return QStringLiteral("all");
        }
        if (mode == 2) {
            return QStringLiteral("office");
        }
        return QStringLiteral("none");
    }

    bool MainWindow::build_archive_scoped_open_payload_for_panel(
        int panel_index, z7::task_ipc_runtime::TaskIpcOpenPayload* out_payload) const {
        if (out_payload == nullptr) {
            return false;
        }

        *out_payload = z7::task_ipc_runtime::TaskIpcOpenPayload{};
        ArchiveWritebackPlan const writeback_plan = build_archive_writeback_plan_for_panel(panel_index);
        if (!writeback_plan.is_valid()) {
            return false;
        }

        QString const archive_path = QFileInfo(writeback_plan.source_archive).absoluteFilePath();
        if (archive_path.trimmed().isEmpty()) {
            return false;
        }

        out_payload->archive_path = archive_path;
        out_payload->archive_type = writeback_plan.root_type_hint().trimmed();
        out_payload->nested_archive_entries = writeback_plan.nested_archive_entries;
        out_payload->filename_code_pages.reserve(static_cast<qsizetype>(writeback_plan.filename_code_pages.size()));
        for (z7::app::FilenameCodePage code_page : writeback_plan.filename_code_pages) {
            out_payload->filename_code_pages.push_back(to_task_ipc_filename_code_page(code_page));
        }
        return true;
    }

    QStringList MainWindow::archive_test_entries_for_panel(int panel_index) const {
        if (!in_archive_view_for_panel(panel_index)) {
            return {};
        }

        return panel_controller(panel_index).oper_smart_archive_entries();
    }

    QString MainWindow::archive_export_info_text_for_panel(int panel_index, QStringList const& archive_entries) const {
        Q_UNUSED(archive_entries);
        PanelController const& panel = panel_controller(panel_index);
        return copy_move_info_text_for_source_rows(panel_index, panel.oper_smart_real_item_rows());
    }

    bool MainWindow::run_archive_export_from_active_panel() {
        if (!in_archive_view()) {
            return false;
        }

        int const panel_index = active_panel_index_;
        PanelController const& panel = active_panel_controller();
        QStringList const archive_entries = oper_smart_archive_export_entries(panel);
        if (panel.archive.source_archive.isEmpty() || archive_entries.isEmpty()) {
            QMessageBox::information(this,
                                     z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(6000)),
                                     z7::ui::runtime_support::L(3014));
            return false;
        }

        QString const dialog_caption = non_empty_label(6000);
        QString const dialog_prompt = non_empty_label(6002);
        CopyMoveDialogResult const dialog_result =
            show_copy_move_dialog(this,
                                  dialog_caption,
                                  dialog_prompt,
                                  archive_export_info_text_for_panel(panel_index, archive_entries),
                                  default_target_directory_for_transfer());
        if (!dialog_result.accepted) {
            return false;
        }

        CopyTransferDestinationPlan const transfer_plan = build_copy_transfer_destination_plan(
            dialog_result.destination_path, current_directory_for_panel(panel_index), archive_entries.size(), true);
        if (!transfer_plan.valid) {
            return false;
        }
        if (!ensure_copy_transfer_destination_directories(transfer_plan)) {
            QMessageBox::warning(this,
                                 dialog_caption,
                                 QStringLiteral("Cannot create destination directory:\n%1")
                                     .arg(QDir::toNativeSeparators(transfer_plan.display_path)));
            return false;
        }

        ArchiveExportDestinationPlan const destination_plan = build_archive_export_destination_plan(
            transfer_plan, panel.model != nullptr && panel.model->flat_view(), panel.archive.virtual_dir);
        if (destination_plan.output_dir.isEmpty()) {
            return false;
        }
        save_copy_history(normalize_copy_history(read_copy_history(), destination_plan.history_path));

        z7::task_ipc_runtime::TaskIpcOpenPayload open_payload;
        if (!build_archive_scoped_open_payload_for_panel(active_panel_index_, &open_payload)) {
            QMessageBox::warning(this,
                                 z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(7201)),
                                 z7::ui::runtime_support::L(3014));
            return false;
        }

        z7::task_ipc_runtime::TaskIpcArchiveExportPayload archive_export;
        archive_export.root_archive_path = open_payload.archive_path;
        archive_export.root_archive_type = open_payload.archive_type;
        archive_export.nested_archive_entries = open_payload.nested_archive_entries;
        archive_export.filename_code_pages = open_payload.filename_code_pages;
        archive_export.archive_entry_paths = archive_entries;
        archive_export.output_dir = destination_plan.output_dir;
        archive_export.overwrite_mode = QStringLiteral("ask");
        archive_export.path_mode = destination_plan.path_mode;
        archive_export.eliminate_root_duplication = false;
        archive_export.restore_file_security = false;
        archive_export.zone_id_mode = extract_zone_id_mode_from_settings();
        archive_export.path_remaps = destination_plan.path_remaps;

        z7::task_ipc_runtime::TaskIpcPayload payload;
        payload.command = z7::task_ipc_runtime::TaskIpcCommandKind::kArchiveExport;
        payload.refresh_after_finish = false;
        payload.archive_export = std::move(archive_export);
        return launch_gui_subprocess_task(dialog_caption, payload);
    }

    void MainWindow::on_extract_requested() {
        if (in_archive_view()) {
            (void)run_archive_export_from_active_panel();
            return;
        }

        PanelController const& panel = active_panel_controller();
        QModelIndexList const rows = panel.selected_real_item_rows();
        QStringList const archives = panel.real_item_paths_for_rows(rows);
        if (archives.isEmpty() || panel.source_rows_contain_dir(rows)) {
            QMessageBox::information(this,
                                     z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(7201)),
                                     z7::ui::runtime_support::L(3015));
            return;
        }

        QString const caption = z7::ui::runtime_support::strip_mnemonic(lang_or(2323));
        z7::task_ipc_runtime::TaskIpcPayload payload;
        payload.command = z7::task_ipc_runtime::TaskIpcCommandKind::kExtract;
        payload.show_dialog = true;
        payload.refresh_after_finish = true;
        payload.extract = z7::task_ipc_runtime::TaskIpcExtractPayload{};
        payload.extract->output_dir = QDir::fromNativeSeparators(current_directory());
        payload.extract->split_dest_enabled = true;
        payload.extract->split_dest_name = archives.size() == 1
                                             ? z7::shell_integration::shell_integration_extract_subfolder_name(
                                                   QFileInfo(archives.front()).fileName())
                                                   + QDir::separator()
                                             : QStringLiteral("*") + QDir::separator();
        payload.extract->zone_id_mode = extract_zone_id_mode_from_settings();
        payload.extract->archive_inputs = archives;
        launch_gui_subprocess_task(caption, payload);
    }

    void MainWindow::on_test_requested() {
        if (in_archive_view()) {
            QStringList const entries = archive_test_entries_for_panel(active_panel_index_);
            z7::task_ipc_runtime::TaskIpcOpenPayload open_payload;
            if (entries.isEmpty() || !build_archive_scoped_open_payload_for_panel(active_panel_index_, &open_payload)) {
                QMessageBox::information(this,
                                         z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(7202)),
                                         z7::ui::runtime_support::L(3014));
                return;
            }

            QString const caption = z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(7202));
            z7::task_ipc_runtime::TaskIpcPayload payload;
            payload.command = z7::task_ipc_runtime::TaskIpcCommandKind::kTest;
            payload.refresh_after_finish = false;
            payload.open = std::move(open_payload);
            payload.test = z7::task_ipc_runtime::TaskIpcTestPayload{};
            payload.test->archive_inputs = entries;
            (void)launch_gui_subprocess_task(caption, payload);
            return;
        }

        QStringList const archives = active_panel_controller().oper_smart_real_item_paths();
        if (archives.isEmpty()) {
            QMessageBox::information(this,
                                     z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(7202)),
                                     z7::ui::runtime_support::L(3015));
            return;
        }

        QString const caption = z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(7202));
        z7::task_ipc_runtime::TaskIpcPayload payload;
        payload.command = z7::task_ipc_runtime::TaskIpcCommandKind::kTest;
        payload.refresh_after_finish = false;
        payload.test = z7::task_ipc_runtime::TaskIpcTestPayload{};
        payload.test->archive_inputs = archives;
        (void)launch_gui_subprocess_task(caption, payload);
    }

    void MainWindow::on_hash_with_method_requested(QString const& method) {
        if (!in_archive_view()) {
            z7::task_ipc_runtime::TaskIpcPayload payload;
            payload.command = z7::task_ipc_runtime::TaskIpcCommandKind::kHash;
            payload.refresh_after_finish = false;
            payload.hash = z7::task_ipc_runtime::TaskIpcHashPayload{};
            payload.hash->hash_method = method.trimmed();
            payload.hash->input_paths = active_panel_controller().oper_smart_real_item_paths();
            if (payload.hash->input_paths.isEmpty()) {
                return;
            }
            (void)launch_gui_subprocess_task(z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(7500)),
                                             payload);
            return;
        }

        QStringList const entries = active_panel_controller().oper_smart_archive_entries();
        QString const hash_method = method.trimmed();
        z7::task_ipc_runtime::TaskIpcOpenPayload open_payload;
        if (entries.isEmpty()
            || hash_method.isEmpty()
            || !build_archive_scoped_open_payload_for_panel(active_panel_index_, &open_payload)) {
            QMessageBox::information(this,
                                     z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(7501)),
                                     z7::ui::runtime_support::L(3015));
            return;
        }

        QString const caption = z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(7500));
        z7::task_ipc_runtime::TaskIpcPayload payload;
        payload.command = z7::task_ipc_runtime::TaskIpcCommandKind::kHash;
        payload.refresh_after_finish = false;
        payload.open = std::move(open_payload);
        payload.hash = z7::task_ipc_runtime::TaskIpcHashPayload{};
        payload.hash->hash_method = hash_method;
        payload.hash->input_paths = entries;
        (void)launch_gui_subprocess_task(caption, payload);
    }

    void MainWindow::on_refresh_requested() {
        if (archive_image_preview_ != nullptr && in_archive_view_for_panel(active_panel_index_)) {
            archive_image_preview_->close_for_session(active_panel_controller().archive.current_token);
        }
        if (refresh_directory_for_panel(active_panel_index_)) {
            update_status();
        }
    }

    void MainWindow::on_open_parent_requested() {
        int const panel_index = active_panel_index_;
        PanelController& panel = panel_controller(panel_index);
        if (in_archive_view_for_panel(panel_index)) {
            QString const current_virtual = z7::ui::archive_support::normalize_virtual_dir(panel.archive.virtual_dir);
            if (current_virtual.isEmpty()) {
                if (auto const parent_return = panel.begin_return_to_parent_archive()) {
                    PanelController::ParentArchiveReturnTransition const transition = *parent_return;
                    auto const start_parent_load = [this, panel_index, transition]() {
                        bool const started = load_archive_virtual_directory_for_panel(
                            panel_index,
                            transition.parent.archive_path,
                            transition.parent.virtual_dir,
                            transition.parent.origin_dir,
                            transition.parent.type_hint,
                            true,
                            [this, panel_index, transition](bool loaded) {
                                if (loaded) {
                                    PanelController& current_panel = panel_controller(panel_index);
                                    current_panel.commit_return_to_parent_archive(transition);
                                    (void)select_model_path_for_panel(panel_index,
                                                                      transition.leaving_archive_entry_from_parent);
                                    if (transition.leaving_temp_session != nullptr
                                        && transition.leaving_temp_session != current_panel.archive.temp_session) {
                                        release_archive_temp_session_for_panel_close(panel_index,
                                                                                     transition.leaving_temp_session);
                                    }
                                    return;
                                }
                                PanelController& current_panel = panel_controller(panel_index);
                                current_panel.rollback_return_to_parent_archive(transition);
                            },
                            false,
                            {},
                            transition.parent.session_token,
                            transition.parent.virtual_display_source);
                        if (!started) {
                            panel_controller(panel_index).rollback_return_to_parent_archive(transition);
                        }
                    };

                    if (transition.leaving_token.is_valid()
                        && transition.leaving_token != transition.parent.session_token
                        && !archive_session_token_referenced_outside_panel(panel_index, transition.leaving_token)) {
                        bool const started = start_task_with_runner(
                            QStringLiteral("Close archive session"),
                            z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(541)),
                            [token = transition.leaving_token](ArchiveProcessRunner* runner) {
                                return runner != nullptr
                                    && runner->start_close_session(token, z7::app::NestedDirtyClosePolicy::kPrompt);
                            },
                            [this, start_parent_load, panel_index, transition](
                                bool ok, int, int, QString const&, z7::app::OperationOutcome const&) {
                                if (!ok) {
                                    panel_controller(panel_index).rollback_return_to_parent_archive(transition);
                                    return;
                                }
                                start_parent_load();
                            },
                            RunnerTaskUiMode::kSilent);
                        if (!started) {
                            panel.rollback_return_to_parent_archive(transition);
                        }
                    } else {
                        start_parent_load();
                    }
                    return;
                }

                QString const exit_dir = panel.archive.origin_dir;
                QString const source_archive = panel.archive.source_archive;
                close_archive_view_for_panel(panel_index, [this, panel_index, exit_dir, source_archive](bool ok) {
                    if (ok && !exit_dir.isEmpty()) {
                        set_current_directory_for_panel(panel_index, exit_dir);
                        (void)select_model_path_for_panel(panel_index, source_archive);
                    }
                });
                return;
            }

            QString parent = current_virtual;
            int const slash = parent.lastIndexOf(QLatin1Char('/'));
            if (slash < 0) {
                parent.clear();
            } else {
                parent = parent.left(slash);
            }
            QString const child_to_focus = current_virtual;

            load_archive_virtual_directory_for_panel(
                panel_index,
                panel.archive.source_archive,
                parent,
                panel.archive.origin_dir,
                panel.archive.type_hint,
                false,
                [this, panel_index, child_to_focus](bool loaded) {
                    if (loaded) {
                        (void)select_model_path_for_panel(panel_index, child_to_focus);
                    }
                },
                false,
                {},
                panel.archive.current_token,
                panel.archive_display_source());
            return;
        }

        QString const current = current_directory();
        QDir dir(current);
        QString const child_to_focus = QFileInfo(current).absoluteFilePath();
        if (dir.cdUp()) {
            set_current_directory_for_panel(panel_index, dir.absolutePath());
            (void)select_model_path_for_panel(panel_index, child_to_focus);
        }
    }

    void MainWindow::on_open_root_requested() {
        int const panel_index = active_panel_index_;
        close_archive_view_for_panel(panel_index, [this, panel_index](bool ok) {
            if (ok) {
                set_current_directory_for_panel(panel_index, QDir::rootPath());
            }
        });
    }

} // namespace z7::ui::filemanager
