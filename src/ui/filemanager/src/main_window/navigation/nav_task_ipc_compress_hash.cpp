// src/ui/filemanager/src/main_window/navigation/nav_task_ipc_compress_hash.cpp
// Role: SevenZip add/hash menu actions and CRC menu rebuild.

#include "common/archive_type_normalization.h"
#include "main_window/deps.h"
#include "main_window/internal.h"

namespace z7::ui::filemanager {
    namespace {

        QString generated_sha256_file_name(QStringList const& input_paths) {
            return z7::shell_integration::shell_integration_create_archive_name_from_paths(input_paths, true, nullptr)
                 + QStringLiteral(".sha256");
        }

        QString normalized_archive_type_token(QString const& value) {
            QString const normalized =
                QString::fromStdString(z7::common::normalize_archive_type_token_copy(value.toStdString()));
            if (normalized.isEmpty()) {
                return QStringLiteral("7z");
            }
            return normalized;
        }

        QString preferred_archive_output_suffix(QString const& value) {
            return QString::fromStdString(z7::common::preferred_archive_output_suffix_copy(value.toStdString()));
        }

    } // namespace

    void MainWindow::run_sevenzip_add_to_archive() {
        SevenZipMenuState const state = compute_seven_zip_menu_state(false);
        if (!state.show_compress_group || state.selected_real_item_paths.isEmpty()) {
            return;
        }

        QString const caption = z7::ui::runtime_support::strip_mnemonic(lang_or(2324));
        QString const archive_path = QDir(state.base_folder).filePath(state.archive_name);
        z7::task_ipc_runtime::TaskIpcPayload payload;
        payload.command = z7::task_ipc_runtime::TaskIpcCommandKind::kAdd;
        payload.show_dialog = true;
        payload.refresh_after_finish = true;
        payload.add = z7::task_ipc_runtime::TaskIpcAddPayload{};
        payload.add->archive_path = archive_path;
        payload.add->input_paths = state.selected_real_item_paths;
        launch_gui_subprocess_task(caption, payload);
    }

    void MainWindow::run_sevenzip_add_to_type(QString const& type) {
        SevenZipMenuState const state = compute_seven_zip_menu_state(false);
        if (!state.show_compress_group || state.selected_real_item_paths.isEmpty()) {
            return;
        }

        QString const canonical_type = normalized_archive_type_token(type);
        QString const output_suffix = preferred_archive_output_suffix(canonical_type);
        QString const caption = z7::ui::runtime_support::strip_mnemonic(lang_or(2328));
        QString const archive_path =
            QDir(state.base_folder).filePath(state.archive_name + QStringLiteral(".") + output_suffix);

        z7::task_ipc_runtime::TaskIpcPayload payload;
        payload.command = z7::task_ipc_runtime::TaskIpcCommandKind::kAdd;
        payload.refresh_after_finish = true;
        payload.add = z7::task_ipc_runtime::TaskIpcAddPayload{};
        payload.add->archive_path = archive_path;
        payload.add->archive_type = canonical_type;
        payload.add->input_paths = state.selected_real_item_paths;
        launch_gui_subprocess_task(caption, payload);
    }

    void MainWindow::run_sevenzip_hash(QString const& method) {
        SevenZipMenuState const state = compute_seven_zip_menu_state(false);
        if (!state.show_crc_group || state.selected_real_item_paths.isEmpty()) {
            return;
        }
        QString const caption = z7::ui::runtime_support::strip_mnemonic(lang_or(7500));
        QString const hash_method = method.trimmed();
        QStringList const input_paths = active_panel_controller().oper_smart_real_item_paths();
        if (input_paths.isEmpty()) {
            return;
        }
        z7::task_ipc_runtime::TaskIpcPayload payload;
        payload.command = z7::task_ipc_runtime::TaskIpcCommandKind::kHash;
        payload.refresh_after_finish = false;
        payload.hash = z7::task_ipc_runtime::TaskIpcHashPayload{};
        payload.hash->hash_method = hash_method;
        payload.hash->input_paths = input_paths;
        launch_gui_subprocess_task(caption, payload);
    }

    void MainWindow::run_sevenzip_generate_sha256() {
        SevenZipMenuState const state = compute_seven_zip_menu_state(false);
        if (!state.show_crc_group || state.selected_real_item_paths.isEmpty()) {
            return;
        }

        QStringList const input_paths = active_panel_controller().oper_smart_real_item_paths();
        if (input_paths.isEmpty()) {
            return;
        }

        QString const sha256_file_name = generated_sha256_file_name(input_paths);
        QString const archive_path = QDir(state.base_folder).filePath(sha256_file_name);
        QString const caption =
            z7::ui::runtime_support::JF(QStringLiteral("shell.actions.generate_sha256"), {sha256_file_name});

        z7::task_ipc_runtime::TaskIpcPayload payload;
        payload.command = z7::task_ipc_runtime::TaskIpcCommandKind::kAdd;
        payload.refresh_after_finish = true;
        payload.add = z7::task_ipc_runtime::TaskIpcAddPayload{};
        payload.add->archive_path = archive_path;
        payload.add->archive_type = QStringLiteral("hash");
        payload.add->input_paths = input_paths;
        launch_gui_subprocess_task(caption, payload);
    }

    void MainWindow::run_sevenzip_checksum_test() {
        SevenZipMenuState const state = compute_seven_zip_menu_state(false);
        if (!state.show_crc_group || state.selected_real_item_paths.isEmpty()) {
            return;
        }

        QStringList const input_paths = active_panel_controller().oper_smart_real_item_paths();
        if (input_paths.isEmpty()) {
            return;
        }

        z7::task_ipc_runtime::TaskIpcPayload payload;
        payload.command = z7::task_ipc_runtime::TaskIpcCommandKind::kCli;
        payload.refresh_after_finish = false;
        payload.cli = z7::task_ipc_runtime::TaskIpcCliPayload{};
        payload.cli->argv = {QStringLiteral("t"), QStringLiteral("-thash")};
        payload.cli->argv.append(input_paths);
        payload.cli->working_dir = state.base_folder;
        launch_gui_subprocess_task(z7::ui::runtime_support::J(QStringLiteral("shell.actions.checksum_test")), payload);
    }

    void MainWindow::populate_crc_hash_menu(QMenu* menu,
                                            bool enabled,
                                            std::function<void(QString const&)> const& on_trigger) {
        if (menu == nullptr) {
            return;
        }

        menu->clear();
        for (z7::shell_integration::ShellIntegrationHashMethodDef const& item :
             z7::shell_integration::shell_integration_hash_method_defs()) {
            QAction* action = menu->addAction(QString::fromLatin1(item.label));
            action->setEnabled(enabled);
            QString const method = QString::fromLatin1(item.method);
            connect(action, &QAction::triggered, this, [on_trigger, method]() { on_trigger(method); });
        }
        menu->menuAction()->setEnabled(enabled);
    }

    void MainWindow::rebuild_file_crc_menu() {
        bool const enabled = has_oper_smart_real_items();
        populate_crc_hash_menu(
            crc_menu_, enabled, [this](QString const& method) { on_hash_with_method_requested(method); });
    }

} // namespace z7::ui::filemanager
