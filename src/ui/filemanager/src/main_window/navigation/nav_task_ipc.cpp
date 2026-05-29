// src/ui/filemanager/src/main_window/navigation/nav_task_ipc.cpp
// Role: SevenZip menu command routing and CRC helpers.

#include "main_window/deps.h"
#include "main_window/internal.h"

#include <QUuid>

#include "filemanager_instance_launcher.h"

namespace z7::ui::filemanager {
namespace {

#ifdef Q_OS_WIN
constexpr const char* k7zGProgramName = "7zG.exe";
#else
constexpr const char* k7zGProgramName = "7zG";
#endif

QStringList unique_non_empty_paths(const QStringList& paths) {
  QStringList out;
  QSet<QString> seen;
  for (const QString& path : paths) {
    const QString trimmed = path.trimmed();
    if (trimmed.isEmpty()) {
      continue;
    }
    const QString normalized =
        QDir::cleanPath(QDir::fromNativeSeparators(trimmed));
    if (normalized.isEmpty()) {
      continue;
    }
    const QString dedup_key = normalized.toLower();
    if (seen.contains(dedup_key)) {
      continue;
    }
    seen.insert(dedup_key);
    out << normalized;
  }
  return out;
}

QStringList unique_non_empty_archive_entries(const QStringList& paths) {
  QStringList out;
  QSet<QString> seen;
  for (const QString& path : paths) {
    const QString normalized =
        z7::ui::archive_support::normalize_virtual_dir(path);
    if (normalized.isEmpty() || seen.contains(normalized)) {
      continue;
    }
    seen.insert(normalized);
    out << normalized;
  }
  return out;
}

QString task_ipc_command_name(z7::task_ipc_runtime::TaskIpcCommandKind command) {
  switch (command) {
    case z7::task_ipc_runtime::TaskIpcCommandKind::kAdd:
      return QStringLiteral("add");
    case z7::task_ipc_runtime::TaskIpcCommandKind::kExtract:
      return QStringLiteral("extract");
    case z7::task_ipc_runtime::TaskIpcCommandKind::kTest:
      return QStringLiteral("test");
    case z7::task_ipc_runtime::TaskIpcCommandKind::kHash:
      return QStringLiteral("hash");
    case z7::task_ipc_runtime::TaskIpcCommandKind::kBenchmark:
      return QStringLiteral("benchmark");
    case z7::task_ipc_runtime::TaskIpcCommandKind::kOpen:
      return QStringLiteral("open");
    case z7::task_ipc_runtime::TaskIpcCommandKind::kArchiveExport:
      return QStringLiteral("archive_export");
    case z7::task_ipc_runtime::TaskIpcCommandKind::kCli:
      return QStringLiteral("cli");
    case z7::task_ipc_runtime::TaskIpcCommandKind::kNone:
    default:
      return QStringLiteral("none");
  }
}

QString create_task_ipc_window_owner_instance_id() {
  return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

bool command_supports_archive_scoped_refresh(
    z7::task_ipc_runtime::TaskIpcCommandKind command) {
  switch (command) {
    case z7::task_ipc_runtime::TaskIpcCommandKind::kExtract:
    case z7::task_ipc_runtime::TaskIpcCommandKind::kTest:
    case z7::task_ipc_runtime::TaskIpcCommandKind::kHash:
      return true;
    case z7::task_ipc_runtime::TaskIpcCommandKind::kNone:
    case z7::task_ipc_runtime::TaskIpcCommandKind::kAdd:
    case z7::task_ipc_runtime::TaskIpcCommandKind::kBenchmark:
    case z7::task_ipc_runtime::TaskIpcCommandKind::kOpen:
    case z7::task_ipc_runtime::TaskIpcCommandKind::kArchiveExport:
    case z7::task_ipc_runtime::TaskIpcCommandKind::kCli:
      return false;
  }
  return false;
}

}  // namespace

QString MainWindow::locate_7zg_program() const {
  const QString self_program = QCoreApplication::applicationFilePath().trimmed();
  if (self_program.isEmpty()) {
    return QString();
  }
  const QFileInfo self_info(self_program);
  if (!self_info.exists()) {
    return QString();
  }
  const QString worker_path = self_info.dir().filePath(QString::fromLatin1(k7zGProgramName));
  const QFileInfo worker_info(worker_path);
  if (!worker_info.exists() || !worker_info.isFile()) {
    return QString();
  }
  return worker_info.absoluteFilePath();
}

void MainWindow::ensure_task_ipc_completion_notifier() {
  if (task_ipc_completion_notifier_registered_ ||
      task_ipc_owner_instance_id_.trimmed().isEmpty()) {
    return;
  }

  QPointer<MainWindow> self(this);
  z7::task_ipc_runtime::set_task_ipc_event_notifier(
      task_ipc_owner_instance_id_,
      [self](const QString&) {
        MainWindow* window = self.data();
        if (window == nullptr) {
          return;
        }
        QMetaObject::invokeMethod(
            window,
            [self]() {
              MainWindow* window = self.data();
              if (window != nullptr) {
                window->poll_task_ipc_completions();
              }
            },
            Qt::QueuedConnection);
      },
      nullptr);
  task_ipc_completion_notifier_registered_ = true;
}

void MainWindow::poll_task_ipc_completions() {
  if (task_ipc_owner_instance_id_.trimmed().isEmpty()) {
    return;
  }

  QVector<z7::task_ipc_runtime::TaskIpcEvent> events;
  QString error_message;
  if (!z7::task_ipc_runtime::collect_task_ipc_events(
          task_ipc_owner_instance_id_, &events, &error_message)) {
    return;
  }
  if (events.isEmpty()) {
    return;
  }
  bool should_refresh = false;
  for (const z7::task_ipc_runtime::TaskIpcEvent& task_event : events) {
    z7::task_ipc_runtime::acknowledge_task_ipc_event(task_event, nullptr);
    if (task_event.event_kind !=
        z7::task_ipc_runtime::TaskIpcEventKind::kCompleted) {
      continue;
    }
    int completion_panel_index = active_panel_index_;
    auto session_panel = task_ipc_session_panels_.find(task_event.session_id);
    if (session_panel != task_ipc_session_panels_.end()) {
      completion_panel_index = session_panel.value();
      task_ipc_session_panels_.erase(session_panel);
    }
    const bool archive_refresh_handled =
        payload_represents_archive_scoped_refresh(task_event.payload) &&
        refresh_archive_panels_for_task_ipc_payload(task_event.payload);
    if (!archive_refresh_handled && task_event.refresh_after_finish) {
      should_refresh = true;
    }
    const QString event_summary = task_event.summary.trimmed();
    if (task_event.result_code != 0 || !event_summary.isEmpty()) {
      const bool canceled = task_event.result_code == 5;
      const QString summary =
          event_summary.isEmpty()
              ? (canceled ? z7::ui::runtime_support::L(448)
                          : z7::ui::runtime_support::JF(
                                QStringLiteral("ui.navigation.task_ipc.task_failed_exit_code"),
                                {QString::number(task_event.result_code)}))
              : event_summary;
      show_transient_status_message(summary,
                                    task_event.result_code == 0 ? 5000 : 8000,
                                    completion_panel_index);
    }
  }
  if (should_refresh) {
    refresh_directory();
  }
}

bool MainWindow::payload_represents_archive_scoped_refresh(
    const z7::task_ipc_runtime::TaskIpcPayload& payload) const {
  const QString archive_path = payload.open.has_value()
                                   ? payload.open->archive_path.trimmed()
                                   : QString();
  return !archive_path.isEmpty() &&
         command_supports_archive_scoped_refresh(payload.command);
}

bool MainWindow::archive_refresh_target_matches_panel(
    const z7::task_ipc_runtime::TaskIpcPayload& payload,
    int panel_index) const {
  if (!in_archive_view_for_panel(panel_index)) {
    return false;
  }
  const ArchiveWritebackPlan plan =
      build_archive_writeback_plan_for_panel(panel_index);
  if (!plan.is_valid()) {
    return false;
  }
  return QFileInfo(plan.source_archive).absoluteFilePath() ==
             QFileInfo(payload.open.has_value() ? payload.open->archive_path
                                                : QString())
                 .absoluteFilePath() &&
         plan.root_type_hint().trimmed() ==
             (payload.open.has_value() ? payload.open->archive_type.trimmed()
                                       : QString()) &&
         plan.nested_archive_entries ==
             (payload.open.has_value() ? payload.open->nested_archive_entries
                                       : QStringList{});
}

bool MainWindow::refresh_archive_panels_for_task_ipc_payload(
    const z7::task_ipc_runtime::TaskIpcPayload& payload) {
  if (!payload_represents_archive_scoped_refresh(payload)) {
    return false;
  }

  QVector<int> matching_panels;
  for (int i = 0; i < 2; ++i) {
    if (archive_refresh_target_matches_panel(payload, i)) {
      matching_panels.push_back(i);
    }
  }
  if (matching_panels.isEmpty()) {
    return false;
  }

  reload_archive_virtual_directories_serially(std::move(matching_panels));
  return true;
}

bool MainWindow::launch_gui_subprocess_task(
    const QString& caption,
    const z7::task_ipc_runtime::TaskIpcPayload& source_payload) {
  const int origin_panel_index = active_panel_index_;
  z7::task_ipc_runtime::TaskIpcPayload payload = source_payload;
  payload.caption = caption;
  if (payload.add.has_value()) {
    payload.add->input_paths = unique_non_empty_paths(payload.add->input_paths);
  }
  if (payload.extract.has_value() && !payload.open.has_value()) {
    payload.extract->archive_inputs =
        unique_non_empty_paths(payload.extract->archive_inputs);
  }
  if (payload.test.has_value() && !payload.open.has_value()) {
    payload.test->archive_inputs =
        unique_non_empty_paths(payload.test->archive_inputs);
  }
  if (payload.hash.has_value() && !payload.open.has_value()) {
    payload.hash->input_paths = unique_non_empty_paths(payload.hash->input_paths);
  }
  if (payload.archive_export.has_value()) {
    const QString root_archive_path =
        payload.archive_export->root_archive_path.trimmed();
    payload.archive_export->root_archive_path =
        root_archive_path.isEmpty()
            ? QString()
            : QFileInfo(root_archive_path).absoluteFilePath();
    payload.archive_export->nested_archive_entries =
        unique_non_empty_archive_entries(
            payload.archive_export->nested_archive_entries);
    payload.archive_export->archive_entry_paths =
        unique_non_empty_archive_entries(
            payload.archive_export->archive_entry_paths);
  }
  const bool archive_aware_hash_test_or_extract =
      (payload.command == z7::task_ipc_runtime::TaskIpcCommandKind::kHash ||
       payload.command == z7::task_ipc_runtime::TaskIpcCommandKind::kTest ||
       payload.command == z7::task_ipc_runtime::TaskIpcCommandKind::kExtract) &&
      payload.open.has_value() &&
      !payload.open->archive_path.trimmed().isEmpty();

  const QString worker_program = locate_7zg_program();
  if (worker_program.isEmpty()) {
    QMessageBox::warning(
        this,
        caption,
        z7::ui::runtime_support::J(
            QStringLiteral("ui.navigation.task_ipc.worker_program_missing")));
    return false;
  }

  if (payload.command == z7::task_ipc_runtime::TaskIpcCommandKind::kAdd &&
      (!payload.add.has_value() || payload.add->archive_path.trimmed().isEmpty() ||
       payload.add->input_paths.isEmpty())) {
    QMessageBox::warning(
        this,
        caption,
        z7::ui::runtime_support::J(
            QStringLiteral("ui.navigation.task_ipc.no_input_paths")));
    return false;
  }
  if (payload.command == z7::task_ipc_runtime::TaskIpcCommandKind::kExtract &&
      !archive_aware_hash_test_or_extract &&
      (!payload.extract.has_value() || payload.extract->archive_inputs.isEmpty())) {
    QMessageBox::warning(
        this,
        caption,
        z7::ui::runtime_support::J(
            QStringLiteral("ui.navigation.task_ipc.no_archive_path")));
    return false;
  }
  if (payload.command == z7::task_ipc_runtime::TaskIpcCommandKind::kTest &&
      !archive_aware_hash_test_or_extract &&
      (!payload.test.has_value() || payload.test->archive_inputs.isEmpty())) {
    QMessageBox::warning(
        this,
        caption,
        z7::ui::runtime_support::J(
            QStringLiteral("ui.navigation.task_ipc.no_archive_path")));
    return false;
  }
  if ((payload.command == z7::task_ipc_runtime::TaskIpcCommandKind::kHash ||
       payload.command == z7::task_ipc_runtime::TaskIpcCommandKind::kTest) &&
      archive_aware_hash_test_or_extract &&
      (!payload.hash.has_value() ? (payload.test.has_value()
                                        ? payload.test->archive_inputs.isEmpty()
                                        : true)
                                 : payload.hash->input_paths.isEmpty())) {
    QMessageBox::warning(
        this,
        caption,
        z7::ui::runtime_support::J(
            QStringLiteral("ui.navigation.task_ipc.no_archive_entries")));
    return false;
  }
  if (payload.command == z7::task_ipc_runtime::TaskIpcCommandKind::kHash &&
      !archive_aware_hash_test_or_extract &&
      (!payload.hash.has_value() || payload.hash->input_paths.isEmpty())) {
    QMessageBox::warning(
        this,
        caption,
        z7::ui::runtime_support::J(
            QStringLiteral("ui.navigation.task_ipc.no_input_paths")));
    return false;
  }
  if (payload.command ==
          z7::task_ipc_runtime::TaskIpcCommandKind::kArchiveExport &&
      (!payload.archive_export.has_value() ||
       payload.archive_export->root_archive_path.trimmed().isEmpty() ||
       payload.archive_export->output_dir.trimmed().isEmpty() ||
       payload.archive_export->archive_entry_paths.isEmpty())) {
    QMessageBox::warning(
        this,
        caption,
        z7::ui::runtime_support::J(
            QStringLiteral("ui.navigation.task_ipc.no_archive_entries")));
    return false;
  }

  if (task_ipc_owner_instance_id_.trimmed().isEmpty()) {
    task_ipc_owner_instance_id_ = create_task_ipc_window_owner_instance_id();
  }
  ensure_task_ipc_completion_notifier();

  QString working_dir = current_directory();
  if (archive_aware_hash_test_or_extract) {
    const QString archive_path = payload.open->archive_path.trimmed();
    const QString archive_parent = QFileInfo(archive_path).absolutePath();
    if (!archive_parent.trimmed().isEmpty()) {
      working_dir = archive_parent;
    }
  } else if (payload.command ==
                 z7::task_ipc_runtime::TaskIpcCommandKind::kArchiveExport &&
             payload.archive_export.has_value()) {
    const QString archive_parent =
        QFileInfo(payload.archive_export->root_archive_path.trimmed())
            .absolutePath();
    if (!archive_parent.trimmed().isEmpty()) {
      working_dir = archive_parent;
    }
  }

  z7::task_ipc_runtime::TaskIpcDispatchResult dispatch_result;
  QString dispatch_error;
  if (!z7::task_ipc_runtime::dispatch_task_ipc_task(worker_program,
                                         working_dir,
                                         task_ipc_owner_instance_id_,
                                         payload,
                                         &dispatch_result,
                                         &dispatch_error)) {
    QMessageBox::warning(this,
                         caption,
                         dispatch_error.trimmed().isEmpty()
                             ? z7::ui::runtime_support::J(
                                   QStringLiteral("ui.navigation.task_ipc.dispatch_failed"))
                             : dispatch_error);
    return false;
  }
  task_ipc_session_panels_.insert(dispatch_result.session_id, origin_panel_index);
  show_transient_status_message(
      QStringLiteral("%1: 7zG %2 (session %3)")
          .arg(caption,
               task_ipc_command_name(payload.command),
               QString::number(dispatch_result.session_id)),
      5000,
      origin_panel_index);
  return true;
}

MainWindow::SevenZipMenuState MainWindow::compute_seven_zip_menu_state(
    bool shift_pressed) const {
  SevenZipMenuState state;
  if (in_archive_view()) {
    return state;
  }

  z7::shell_integration::ShellIntegrationSelection selection;
  selection.selected_paths = active_panel_controller().selected_real_item_paths();
  selection.shift_pressed = shift_pressed;
  selection.working_directory = current_directory();

  z7::shell_integration::ShellIntegrationConfig config;
  config.enabled = true;

  const z7::shell_integration::ShellIntegrationMenuPlan plan =
      z7::shell_integration::build_shell_integration_menu_plan(selection, config);
  if (!plan.menu_visible) {
    return state;
  }

  state.visible = plan.menu_visible;
  state.show_open = plan.show_open;
  state.show_open_as = plan.show_open_as;
  state.show_extract_group = plan.show_extract_group;
  state.show_test = plan.show_test;
  state.show_compress_group = plan.show_compress_group;
  state.show_crc_group = plan.show_crc_group;
  state.selected_real_item_paths = plan.selected_paths;
  state.selected_files = plan.selected_files;
  if (state.show_extract_group || state.show_test) {
    state.selected_files = state.selected_real_item_paths;
  }
  state.base_folder = plan.base_folder;
  state.extract_subdir = plan.extract_subdir;
  state.archive_name = plan.archive_name;
  state.archive_name_7z = plan.archive_name_7z;
  state.archive_name_zip = plan.archive_name_zip;
  return state;
}

bool MainWindow::launch_7zfm_open_request(const QString& caption,
                                          const QString& target_path,
                                          const QString& archive_type_hint) {
  const int origin_panel_index = active_panel_index_;
  if (target_path.trimmed().isEmpty()) {
    QMessageBox::warning(this,
                         caption,
                         z7::ui::runtime_support::J(
                             QStringLiteral("ui.navigation.task_ipc.open_requires_one_target")));
    return false;
  }

  const QString working_dir =
      QFileInfo(QDir::fromNativeSeparators(target_path.trimmed())).absolutePath();
  QString launch_error;
  if (!z7::platform::qt::filemanager_instance_launcher::
          launch_open_request_for_current_app(
          target_path, archive_type_hint, working_dir, &launch_error)) {
    QMessageBox::warning(this,
                         caption,
                         launch_error.trimmed().isEmpty()
                             ? z7::ui::runtime_support::J(
                                   QStringLiteral("ui.navigation.task_ipc.launch_7zfm_failed"))
                             : launch_error);
    return false;
  }
  show_transient_status_message(
      QStringLiteral("%1: launched 7zFM open").arg(caption),
      5000,
      origin_panel_index);
  return true;
}

void MainWindow::run_sevenzip_open_archive() {
  const SevenZipMenuState state = compute_seven_zip_menu_state(false);
  if (!state.show_open || state.selected_real_item_paths.size() != 1) {
    return;
  }
  const QString target_path = state.selected_real_item_paths.front();
  const QString caption = z7::ui::runtime_support::strip_mnemonic(lang_or(2322));
  launch_7zfm_open_request(caption, target_path, QString());
}

void MainWindow::run_sevenzip_open_archive_as(const QString& type) {
  const SevenZipMenuState state = compute_seven_zip_menu_state(false);
  if (!state.show_open_as || state.selected_real_item_paths.size() != 1) {
    return;
  }
  const QString target_path = state.selected_real_item_paths.front();
  const QString caption = z7::ui::runtime_support::strip_mnemonic(lang_or(2322));
  const QString type_hint = type.trimmed();
  launch_7zfm_open_request(caption, target_path, type_hint);
}

void MainWindow::run_sevenzip_extract_files_dialog() {
  const SevenZipMenuState state =
      compute_seven_zip_menu_state(
          (QApplication::keyboardModifiers() & Qt::ShiftModifier) != 0);
  if (!state.show_extract_group || state.selected_files.isEmpty()) {
    return;
  }

  const QString caption = z7::ui::runtime_support::strip_mnemonic(lang_or(2323));
  z7::task_ipc_runtime::TaskIpcPayload payload;
  payload.command = z7::task_ipc_runtime::TaskIpcCommandKind::kExtract;
  payload.show_dialog = true;
  payload.refresh_after_finish = true;
  payload.extract = z7::task_ipc_runtime::TaskIpcExtractPayload{};
  payload.extract->output_dir = QDir::fromNativeSeparators(state.base_folder);
  payload.extract->split_dest_enabled = true;
  payload.extract->split_dest_name = state.extract_subdir;
  payload.extract->zone_id_mode = extract_zone_id_mode_from_settings();
  payload.extract->archive_inputs = state.selected_files;
  launch_gui_subprocess_task(caption, payload);
}

void MainWindow::run_sevenzip_extract_here() {
  const SevenZipMenuState state =
      compute_seven_zip_menu_state(
          (QApplication::keyboardModifiers() & Qt::ShiftModifier) != 0);
  if (!state.show_extract_group || state.selected_files.isEmpty()) {
    return;
  }

  const QString output_dir = state.base_folder;
  const QString caption = z7::ui::runtime_support::strip_mnemonic(lang_or(2326));
  z7::task_ipc_runtime::TaskIpcPayload payload;
  payload.command = z7::task_ipc_runtime::TaskIpcCommandKind::kExtract;
  payload.refresh_after_finish = true;
  payload.extract = z7::task_ipc_runtime::TaskIpcExtractPayload{};
  payload.extract->output_dir = QDir::fromNativeSeparators(output_dir);
  payload.extract->split_dest_enabled = false;
  payload.extract->zone_id_mode = extract_zone_id_mode_from_settings();
  payload.extract->archive_inputs = state.selected_files;
  launch_gui_subprocess_task(caption, payload);
}

void MainWindow::run_sevenzip_extract_to() {
  const SevenZipMenuState state =
      compute_seven_zip_menu_state(
          (QApplication::keyboardModifiers() & Qt::ShiftModifier) != 0);
  if (!state.show_extract_group || state.selected_files.isEmpty()) {
    return;
  }

  const QString output_dir = state.base_folder;
  const QString caption = z7::ui::runtime_support::strip_mnemonic(lang_or(2327));
  z7::task_ipc_runtime::TaskIpcPayload payload;
  payload.command = z7::task_ipc_runtime::TaskIpcCommandKind::kExtract;
  payload.refresh_after_finish = true;
  payload.extract = z7::task_ipc_runtime::TaskIpcExtractPayload{};
  payload.extract->output_dir = QDir::fromNativeSeparators(output_dir);
  payload.extract->split_dest_enabled = true;
  payload.extract->split_dest_name = state.extract_subdir;
  payload.extract->eliminate_root_duplication =
      extract_eliminate_root_duplication_from_settings();
  payload.extract->zone_id_mode = extract_zone_id_mode_from_settings();
  payload.extract->archive_inputs = state.selected_files;
  launch_gui_subprocess_task(caption, payload);
}

void MainWindow::run_sevenzip_test_archive() {
  const SevenZipMenuState state =
      compute_seven_zip_menu_state(
          (QApplication::keyboardModifiers() & Qt::ShiftModifier) != 0);
  if (!state.show_test || state.selected_files.isEmpty()) {
    return;
  }

  const QString caption = z7::ui::runtime_support::strip_mnemonic(lang_or(2325));
  z7::task_ipc_runtime::TaskIpcPayload payload;
  payload.command = z7::task_ipc_runtime::TaskIpcCommandKind::kTest;
  payload.refresh_after_finish = false;
  payload.test = z7::task_ipc_runtime::TaskIpcTestPayload{};
  payload.test->archive_inputs = state.selected_files;
  launch_gui_subprocess_task(caption, payload);
}

}  // namespace z7::ui::filemanager
