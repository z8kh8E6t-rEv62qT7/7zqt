// src/ui/filemanager/src/main_window/actions/action_properties.cpp
// Role: Properties dialog logic and table rendering helpers.

#include "main_window/deps.h"
#include "main_window/internal.h"

#include <QDirIterator>

#include <memory>
#include <optional>

#include "archive_delegate_qt.h"
#include "large_pages_settings.h"

namespace z7::ui::filemanager {

namespace {

QString format_grouped_uint64(const uint64_t value) {
  const QString digits = QString::number(value);
  if (digits.size() <= 3) {
    return digits;
  }

  QString out;
  out.reserve(digits.size() + digits.size() / 3);
  const int first_group = (digits.size() % 3 == 0) ? 3 : (digits.size() % 3);
  out += digits.left(first_group);
  for (int i = first_group; i < digits.size(); i += 3) {
    out += QLatin1Char(' ');
    out += digits.mid(i, 3);
  }
  return out;
}

QString localized_property_name(const std::optional<uint32_t>& prop_id,
                                const QString& native_name) {
  if (prop_id.has_value()) {
    const uint32_t lang_id = *prop_id < 1000u ? (1000u + *prop_id) : *prop_id;
    return z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(lang_id));
  }

  if (!native_name.trimmed().isEmpty()) {
    return native_name;
  }
  return QString();
}

QString localized_text(const uint32_t lang_id) {
  return z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(lang_id));
}

QString localized_selected_items_text(const int count) {
  return z7::ui::runtime_support::LF(3002, {QString::number(count)});
}

bool is_archive_selection_summary_count_line(const z7::app::ArchivePropertyLine& line) {
  return line.kind == z7::app::PropertyLineKind::kPair &&
         line.display_group == z7::app::ArchivePropertyDisplayGroup::kSelectionSummary &&
         !line.prop_id.has_value() &&
         line.name.empty();
}

struct FilesystemPropertyStats {
  uint64_t folders = 0;
  uint64_t files = 0;
  uint64_t size = 0;
};

void accumulate_directory_stats(const QString& root_path,
                                FilesystemPropertyStats* stats) {
  if (stats == nullptr) {
    return;
  }

  ++stats->folders;
  QDirIterator it(root_path,
                  QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
                  QDirIterator::Subdirectories);
  while (it.hasNext()) {
    it.next();
    const QFileInfo child = it.fileInfo();
    if (child.isDir()) {
      ++stats->folders;
      continue;
    }
    if (child.isFile()) {
      ++stats->files;
      stats->size += static_cast<uint64_t>(child.size());
    }
  }
}

FilesystemPropertyStats collect_filesystem_stats(const QFileInfo& info) {
  FilesystemPropertyStats stats;
  if (info.isDir()) {
    accumulate_directory_stats(info.absoluteFilePath(), &stats);
    return stats;
  }

  stats.files = 1;
  stats.size = static_cast<uint64_t>(info.size());
  return stats;
}

QString format_filesystem_timestamp(const QDateTime& timestamp) {
  if (!timestamp.isValid()) {
    return QString();
  }
  return timestamp.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
}

}  // namespace

void MainWindow::show_properties_dialog() {
  const PanelController& panel = active_panel_controller();
  if (panel.ui.details_view == nullptr || panel.ui.details_view->selectionModel() == nullptr ||
      panel.model == nullptr) {
    return;
  }
  const bool archive_mode = in_archive_view();
  if (!archive_mode && active_selected_rows_include_parent_link()) {
    return;
  }

  const QModelIndexList selected_rows = panel.selected_rows_including_parent_link();

  QModelIndexList rows;
  rows.reserve(selected_rows.size());
  for (const QModelIndex& index : selected_rows) {
    if (!index.isValid() || panel.model->is_parent_link_for_row(index.row())) {
      continue;
    }
    rows << index;
  }
  if (!archive_mode && rows.isEmpty()) {
    return;
  }

  QVector<PropertiesDialogRow> dialog_rows;
  const auto append_pair_to =
      [](QVector<PropertiesDialogRow>* rows, const QString& key, const QString& value) {
        if (rows == nullptr || value.trimmed().isEmpty()) {
          return;
        }
        PropertiesDialogRow row;
        row.kind = PropertiesDialogRow::Kind::kPair;
        row.key = key;
        row.value = value;
        rows->push_back(std::move(row));
      };
  const auto append_separator_to =
      [](QVector<PropertiesDialogRow>* rows, const bool small = false) {
        if (rows == nullptr) {
          return;
        }
        PropertiesDialogRow row;
        row.kind = small ? PropertiesDialogRow::Kind::kSeparatorSmall
                         : PropertiesDialogRow::Kind::kSeparator;
        rows->push_back(std::move(row));
      };
  const auto append_selection_summary_count_to =
      [append_pair_to](QVector<PropertiesDialogRow>* rows, const int count) {
        append_pair_to(rows, QString(), localized_selected_items_text(count));
      };
  const auto append_archive_properties_rows =
      [append_pair_to, append_separator_to](
          const z7::app::ArchivePropertiesResult& result,
          QVector<PropertiesDialogRow>* rows) {
        for (const z7::app::ArchivePropertyLine& line : result.lines) {
          if (line.kind == z7::app::PropertyLineKind::kSeparator) {
            append_separator_to(rows, false);
            continue;
          }
          if (line.kind == z7::app::PropertyLineKind::kSeparatorSmall) {
            append_separator_to(rows, true);
            continue;
          }

          const QString native_name = z7::ui::archive_support::from_utf8_string(line.name);
          QString value = z7::ui::archive_support::from_utf8_string(line.value);
          if (is_archive_selection_summary_count_line(line)) {
            bool ok = false;
            const int count = value.toInt(&ok);
            value = ok ? localized_selected_items_text(count)
                       : z7::ui::runtime_support::LF(3002, {value});
          }
          append_pair_to(rows, localized_property_name(line.prop_id, native_name), value);
        }
      };
  const auto append_filesystem_single_selection_rows =
      [append_pair_to, append_separator_to](const QFileInfo& info,
                                            QVector<PropertiesDialogRow>* rows) {
        const bool is_dir = info.isDir();
        const FilesystemPropertyStats stats = collect_filesystem_stats(info);
        const QDateTime created = info.birthTime().isValid() ? info.birthTime()
                                                             : info.lastModified();

        append_pair_to(rows, localized_text(1004), info.fileName());
        append_pair_to(rows,
                       localized_text(1020),
                       is_dir ? localized_text(1006) : localized_text(500));
        append_pair_to(rows, localized_text(1032), format_grouped_uint64(stats.files));
        append_pair_to(rows, localized_text(1031), format_grouped_uint64(stats.folders));
        append_pair_to(rows, localized_text(1007), format_grouped_uint64(stats.size));
        append_separator_to(rows, false);
        append_pair_to(rows,
                       localized_text(1003),
                       QDir::toNativeSeparators(info.absoluteFilePath()));
        append_pair_to(rows, localized_text(1010), format_filesystem_timestamp(created));
        append_pair_to(rows,
                       localized_text(1012),
                       format_filesystem_timestamp(info.lastModified()));
      };
  const auto append_filesystem_multi_selection_rows =
      [append_pair_to, append_selection_summary_count_to](const QStringList& paths,
                                                          QVector<PropertiesDialogRow>* rows) {
        append_selection_summary_count_to(rows, paths.size());

        FilesystemPropertyStats aggregate;
        for (const QString& path : paths) {
          const QFileInfo info(path);
          if (info.isDir()) {
            accumulate_directory_stats(path, &aggregate);
            continue;
          }
          ++aggregate.files;
          aggregate.size += static_cast<uint64_t>(info.size());
        }

        append_pair_to(rows, localized_text(1031), format_grouped_uint64(aggregate.folders));
        append_pair_to(rows, localized_text(1032), format_grouped_uint64(aggregate.files));
        append_pair_to(rows, localized_text(1007), format_grouped_uint64(aggregate.size));
      };

  const QString title = localized_text(6600);
  if (archive_mode) {
    if (panel.archive.source_archive.trimmed().isEmpty() ||
        (!panel.archive.current_token.is_valid() &&
         !QFileInfo(panel.archive.source_archive).exists())) {
      return;
    }

    QStringList selected_entries;
    selected_entries.reserve(rows.size());
    for (const QModelIndex& index : rows) {
      const QString path = z7::ui::archive_support::normalize_virtual_dir(
          panel.model->path_for_row(index.row()));
      if (!path.isEmpty()) {
        selected_entries << path;
      }
    }
    selected_entries.removeDuplicates();

    z7::app::ArchivePropertiesRequest request;
    request.archive_path = z7::ui::archive_support::to_native_string(panel.archive.source_archive);
    request.entries.reserve(selected_entries.size());
    for (const QString& entry : selected_entries) {
      request.entries.push_back(z7::ui::archive_support::to_utf8_string(entry));
    }
    request.directory = z7::ui::archive_support::to_utf8_string(panel.archive.virtual_dir);
    request.flat_view = panel.model->flat_view();
    request.archive_type_hint = z7::ui::archive_support::to_utf8_string(panel.archive.type_hint.trimmed());
    if (panel.archive.current_token.is_valid()) {
      request.session_token = panel.archive.current_token;
    }

    QPointer<MainWindow> main_window(this);
    const QString warning_caption = localized_text(541);
    struct AsyncArchiveTaskState final {
      z7::app::ArchiveEngine engine;
      z7::app::ArchiveSession session;
      std::shared_ptr<z7::app::IArchiveDelegate> delegate;
    };
    auto async_task = std::make_shared<AsyncArchiveTaskState>();
    auto completion_delegate =
        std::make_shared<z7::ui::archive_support::OutcomeRelayDelegate>(
            this,
            [main_window,
             async_task,
             title,
             warning_caption,
             append_archive_properties_rows](const z7::app::OperationOutcome& outcome) {
          async_task->session = z7::app::ArchiveSession{};
          async_task->delegate.reset();
          if (main_window.isNull()) {
            return;
          }
          const auto properties_result =
              z7::app::outcome_payload_as<z7::app::ArchivePropertiesResult>(outcome);
          if (!properties_result.has_value()) {
            QMessageBox::warning(
                main_window,
                warning_caption,
                stable_error_message(static_cast<int>(outcome.error_domain)));
            return;
          }
          if (!properties_result->ok) {
            QMessageBox::warning(
                main_window,
                warning_caption,
                stable_error_message(static_cast<int>(properties_result->error.domain)));
            return;
          }

          QVector<PropertiesDialogRow> rows_for_dialog;
          append_archive_properties_rows(*properties_result, &rows_for_dialog);

          main_window->show_properties_table(title, rows_for_dialog);
            },
            nullptr,
            z7::ui::archive_support::MissingTargetPolicy::kInvokeDirect);
    async_task->delegate = completion_delegate;

    z7::ui::runtime_support::apply_configured_large_pages_mode();
    async_task->session = async_task->engine.start(
        z7::app::ArchiveRequest{std::move(request)},
        completion_delegate);
    if (!async_task->session.valid()) {
      const z7::app::OperationOutcome outcome = z7::app::make_backend_unavailable_outcome();
      QMessageBox::warning(
          this,
          warning_caption,
          stable_error_message(static_cast<int>(outcome.error_domain)));
      async_task->delegate.reset();
    }
    return;
  }

  QStringList paths;
  paths.reserve(rows.size());
  for (const QModelIndex& index : rows) {
    const QString path = panel.model->path_for_row(index.row());
    if (!path.isEmpty()) {
      paths << path;
    }
  }
  if (paths.isEmpty()) {
    return;
  }

  if (paths.size() == 1) {
    append_filesystem_single_selection_rows(QFileInfo(paths.front()), &dialog_rows);
  } else {
    append_filesystem_multi_selection_rows(paths, &dialog_rows);
  }

  show_properties_table(title, dialog_rows);
}

void MainWindow::show_properties_table(const QString& title,
                                       const QVector<PropertiesDialogRow>& rows) {
  if (rows.isEmpty()) {
    return;
  }

  QDialog dialog(this);
  dialog.setWindowTitle(title);
#ifdef Z7_TESTING
  dialog.setObjectName(QStringLiteral("propertiesDialog"));
#endif
  dialog.resize(820, 520);

  auto* layout = new QVBoxLayout(&dialog);
  auto* table = new QTableWidget(rows.size(), 2, &dialog);
#ifdef Z7_TESTING
  table->setObjectName(QStringLiteral("propertiesTable"));
#endif
  table->setHorizontalHeaderLabels({QString(), QString()});
  table->horizontalHeader()->setVisible(true);
  table->horizontalHeader()->setSectionsClickable(false);
  table->horizontalHeader()->setHighlightSections(false);
  table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Interactive);
  table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Interactive);
  table->horizontalHeader()->setStretchLastSection(true);
  table->verticalHeader()->setVisible(false);
  table->setEditTriggers(QAbstractItemView::NoEditTriggers);
  table->setSelectionBehavior(QAbstractItemView::SelectRows);
  table->setSelectionMode(QAbstractItemView::SingleSelection);
  table->setWordWrap(false);
  table->setShowGrid(false);

  for (int i = 0; i < rows.size(); ++i) {
    const auto& row = rows.at(i);
    if (row.kind == PropertiesDialogRow::Kind::kSeparator ||
        row.kind == PropertiesDialogRow::Kind::kSeparatorSmall) {
      const QString text = row.key.isEmpty()
                               ? (row.kind == PropertiesDialogRow::Kind::kSeparatorSmall
                                      ? QStringLiteral("----------------")
                                      : QStringLiteral("------------------------"))
                               : row.key;
      auto* sep_key_item = new QTableWidgetItem(text);
      auto* sep_value_item = new QTableWidgetItem(QString());
      sep_key_item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
      sep_value_item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
      table->setItem(i, 0, sep_key_item);
      table->setItem(i, 1, sep_value_item);
      continue;
    }

    auto* key_item = new QTableWidgetItem(row.key);
    auto* value_item = new QTableWidgetItem(row.value);
    key_item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    value_item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    table->setItem(i, 0, key_item);
    table->setItem(i, 1, value_item);
  }
  table->resizeColumnsToContents();
  table->horizontalHeader()->resizeSection(0, table->columnWidth(0) + 12);
  table->resizeRowsToContents();
  layout->addWidget(table);

  auto* buttons =
      new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                           Qt::Horizontal,
                           &dialog);
  z7::platform::qt::apply_dialog_button_baseline(buttons);
  if (auto* ok = buttons->button(QDialogButtonBox::Ok)) {
    ok->setText(z7::ui::runtime_support::L(401));
  }
  if (auto* cancel = buttons->button(QDialogButtonBox::Cancel)) {
    cancel->setText(z7::ui::runtime_support::L(402));
  }
  connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  layout->addWidget(buttons);

  dialog.exec();
}

}  // namespace z7::ui::filemanager
