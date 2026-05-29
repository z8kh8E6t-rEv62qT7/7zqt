// src/ui/filemanager/src/main_window/navigation/nav_task_ipc_compress_hash.cpp
// Role: SevenZip add/hash menu actions and CRC menu rebuild.

#include "main_window/deps.h"
#include "main_window/internal.h"
#include "common/archive_type_normalization.h"

namespace z7::ui::filemanager {
namespace {

QString generated_sha256_file_name(const QStringList& input_paths) {
  return z7::shell_integration::shell_integration_create_archive_name_from_paths(
             input_paths,
             true,
             nullptr) +
         QStringLiteral(".sha256");
}

QString normalized_archive_type_token(const QString& value) {
  const QString normalized = QString::fromStdString(
      z7::common::normalize_archive_type_token_copy(value.toStdString()));
  if (normalized.isEmpty()) {
    return QStringLiteral("7z");
  }
  return normalized;
}

QString preferred_archive_output_suffix(const QString& value) {
  return QString::fromStdString(
      z7::common::preferred_archive_output_suffix_copy(value.toStdString()));
}

}  // namespace

void MainWindow::run_sevenzip_add_to_archive() {
  const SevenZipMenuState state = compute_seven_zip_menu_state(false);
  if (!state.show_compress_group || state.selected_real_item_paths.isEmpty()) {
    return;
  }

  const QString caption = z7::ui::runtime_support::strip_mnemonic(lang_or(2324));
  const QString archive_path = QDir(state.base_folder).filePath(state.archive_name);
  z7::task_ipc_runtime::TaskIpcPayload payload;
  payload.command = z7::task_ipc_runtime::TaskIpcCommandKind::kAdd;
  payload.show_dialog = true;
  payload.refresh_after_finish = true;
  payload.add = z7::task_ipc_runtime::TaskIpcAddPayload{};
  payload.add->archive_path = archive_path;
  payload.add->input_paths = state.selected_real_item_paths;
  launch_gui_subprocess_task(caption, payload);
}

void MainWindow::run_sevenzip_add_to_type(const QString& type) {
  const SevenZipMenuState state = compute_seven_zip_menu_state(false);
  if (!state.show_compress_group || state.selected_real_item_paths.isEmpty()) {
    return;
  }

  const QString canonical_type = normalized_archive_type_token(type);
  const QString output_suffix = preferred_archive_output_suffix(canonical_type);
  const QString caption = z7::ui::runtime_support::strip_mnemonic(lang_or(2328));
  const QString archive_path = QDir(state.base_folder).filePath(
      state.archive_name + QStringLiteral(".") + output_suffix);

  z7::task_ipc_runtime::TaskIpcPayload payload;
  payload.command = z7::task_ipc_runtime::TaskIpcCommandKind::kAdd;
  payload.refresh_after_finish = true;
  payload.add = z7::task_ipc_runtime::TaskIpcAddPayload{};
  payload.add->archive_path = archive_path;
  payload.add->archive_type = canonical_type;
  payload.add->input_paths = state.selected_real_item_paths;
  launch_gui_subprocess_task(caption, payload);
}

void MainWindow::run_sevenzip_hash(const QString& method) {
  const SevenZipMenuState state = compute_seven_zip_menu_state(false);
  if (!state.show_crc_group || state.selected_real_item_paths.isEmpty()) {
    return;
  }
  const QString caption = z7::ui::runtime_support::strip_mnemonic(lang_or(7500));
  const QString hash_method = method.trimmed();
  const QStringList input_paths = active_panel_controller().oper_smart_real_item_paths();
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
  const SevenZipMenuState state = compute_seven_zip_menu_state(false);
  if (!state.show_crc_group || state.selected_real_item_paths.isEmpty()) {
    return;
  }

  const QStringList input_paths = active_panel_controller().oper_smart_real_item_paths();
  if (input_paths.isEmpty()) {
    return;
  }

  const QString sha256_file_name = generated_sha256_file_name(input_paths);
  const QString archive_path = QDir(state.base_folder).filePath(sha256_file_name);
  const QString caption = z7::ui::runtime_support::JF(
      QStringLiteral("shell.actions.generate_sha256"),
      {sha256_file_name});

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
  const SevenZipMenuState state = compute_seven_zip_menu_state(false);
  if (!state.show_crc_group || state.selected_real_item_paths.isEmpty()) {
    return;
  }

  const QStringList input_paths = active_panel_controller().oper_smart_real_item_paths();
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
  launch_gui_subprocess_task(
      z7::ui::runtime_support::J(QStringLiteral("shell.actions.checksum_test")),
      payload);
}

void MainWindow::populate_crc_hash_menu(
    QMenu* menu,
    bool enabled,
    const std::function<void(const QString&)>& on_trigger) {
  if (menu == nullptr) {
    return;
  }

  menu->clear();
  for (const z7::shell_integration::ShellIntegrationHashMethodDef& item :
       z7::shell_integration::shell_integration_hash_method_defs()) {
    QAction* action = menu->addAction(QString::fromLatin1(item.label));
    action->setEnabled(enabled);
    const QString method = QString::fromLatin1(item.method);
    connect(action, &QAction::triggered, this, [on_trigger, method]() {
      on_trigger(method);
    });
  }
  menu->menuAction()->setEnabled(enabled);
}

void MainWindow::rebuild_file_crc_menu() {
  const bool enabled = has_oper_smart_real_items();
  populate_crc_hash_menu(
      crc_menu_,
      enabled,
      [this](const QString& method) { on_hash_with_method_requested(method); });
}

}  // namespace z7::ui::filemanager
