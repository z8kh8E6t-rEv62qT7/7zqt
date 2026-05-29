// src/ui/filemanager/src/main_window/actions/task.cpp
// Role: Task progress and hash workflow helpers.
// This partition is intentionally kept under 1000 lines.

#include "main_window/deps.h"
#include "main_window/internal.h"

#include "archive_format.h"

namespace z7::ui::filemanager {

namespace {

QString hash_caption_text() {
  return z7::ui::runtime_support::strip_mnemonic(
      z7::ui::runtime_support::L(7501));
}

}  // namespace

void MainWindow::start_combine_task(const QString& source_part_path,
                                    const QString& output_dir) {
  start_task_with_runner(
      QStringLiteral("%1: %2").arg(z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(550)), source_part_path),
      z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(550)),
      [source_part_path, output_dir](ArchiveProcessRunner* runner) {
        return runner != nullptr &&
               runner->start_combine(source_part_path, output_dir);
      },
      [this](bool ok, int, int, const QString&, const z7::app::OperationOutcome&) {
        if (ok) {
          refresh_directory();
        }
      });
}

QVector<QPair<QString, QString>> MainWindow::build_hash_result_rows(
    const z7::app::HashSummary& summary) const {
  QVector<QPair<QString, QString>> rows;

  const bool single_file =
      summary.num_files == 1 && summary.num_dirs == 0 && !summary.first_file_name.empty();
  if (summary.num_errors != 0) {
    rows.append({z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(1070)), QString::number(summary.num_errors)});
  }

  if (single_file) {
    rows.append({z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(1004)), z7::ui::archive_support::from_utf8_string(summary.first_file_name)});
  } else {
    if (!summary.main_name.empty()) {
      rows.append({z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(1004)), z7::ui::archive_support::from_utf8_string(summary.main_name)});
    }
    if (summary.num_dirs != 0) {
      rows.append({z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(1031)), QString::number(summary.num_dirs)});
    }
    rows.append({z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(1032)), QString::number(summary.num_files)});
  }

  rows.append({z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(1007)),
               z7::ui::archive_support::format_dual_size(summary.files_size)});
  if (summary.num_alt_streams != 0) {
    rows.append(
        {z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(1075)), QString::number(summary.num_alt_streams)});
    rows.append({z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(1076)),
                 z7::ui::archive_support::format_dual_size(
                     summary.alt_streams_size)});
  }

  const auto hash_label = [](uint32_t lang_id, const QString& method_name) {
    QString label = z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(lang_id));
    label.replace(QStringLiteral("CRC"), method_name);
    label.remove(QLatin1Char(':'));
    return label.trimmed();
  };

  for (const z7::app::HashMethodDigest& digest : summary.methods) {
    const QString method_name = z7::ui::archive_support::from_utf8_string(digest.method_name);
    if (single_file) {
      if (digest.has_data_sum) {
        rows.append({method_name, z7::ui::archive_support::from_utf8_string(digest.data_sum)});
      }
      continue;
    }

    if (digest.has_data_sum) {
      rows.append({hash_label(7502, method_name), z7::ui::archive_support::from_utf8_string(digest.data_sum)});
    }
    if (digest.has_names_sum) {
      rows.append({hash_label(7503, method_name), z7::ui::archive_support::from_utf8_string(digest.names_sum)});
    }
    if (summary.num_alt_streams != 0 && digest.has_streams_sum) {
      rows.append({hash_label(7504, method_name), z7::ui::archive_support::from_utf8_string(digest.streams_sum)});
    }
  }

  return rows;
}

void MainWindow::start_hash_task(const QStringList& inputs,
                                 const QString& hash_method,
                                 bool recursive_dirs) {
  Q_UNUSED(recursive_dirs);

  z7::task_ipc_runtime::TaskIpcPayload payload;
  payload.command = z7::task_ipc_runtime::TaskIpcCommandKind::kHash;
  payload.refresh_after_finish = false;
  payload.hash = z7::task_ipc_runtime::TaskIpcHashPayload{};
  payload.hash->hash_method = hash_method.trimmed();
  payload.hash->input_paths = inputs;
  launch_gui_subprocess_task(hash_caption_text(), payload);
}

}  // namespace z7::ui::filemanager

// End of task.cpp
