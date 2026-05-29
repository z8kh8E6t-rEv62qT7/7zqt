// src/ui/filemanager/src/temp_files_dialog_actions.cpp
// Role: Action handlers for delete/open/properties/context in temp-files dialog.

#include "temp_files_dialog.h"

#include "temp_files_dialog_service.h"
#include "official_lang_catalog.h"

#include <QAbstractButton>
#include <QDesktopServices>
#include <QDir>
#include <QItemSelectionModel>
#include <QMessageBox>
#include <QModelIndex>
#include <QMenu>
#include <QTableWidget>
#include <QUrl>

#include <algorithm>

namespace z7::ui::filemanager {

namespace {

constexpr int kPathRole = Qt::UserRole + 1;
constexpr int kIsDirRole = Qt::UserRole + 2;
constexpr int kIsSymlinkRole = Qt::UserRole + 3;

QString localized_text(const uint32_t id) {
  return z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(id)).trimmed();
}

}  // namespace

void TempFilesDialog::on_delete_requested() {
  const QVector<TempFilesListEntry> entries = selected_entries();
  if (entries.isEmpty()) {
    return;
  }

  uint32_t title_id = 6102;
  uint32_t message_id = 6105;
  QString message_arg = QString::number(entries.size());
  if (entries.size() == 1) {
    if (entries.front().is_dir) {
      title_id = 6101;
      message_id = 6104;
    } else {
      title_id = 6100;
      message_id = 6103;
    }
    message_arg = entries.front().name;
  }

  QString message = z7::ui::runtime_support::LF(message_id, {message_arg});
  if (entries.size() > 1) {
    QStringList details;
    details.reserve(entries.size());
    for (int i = 0; i < entries.size(); ++i) {
      if (i >= 10) {
        details.push_back(QStringLiteral("..."));
        break;
      }
      details.push_back(entries.at(i).name);
    }
    if (!details.isEmpty()) {
      message += QStringLiteral("\n\n%1").arg(details.join(QLatin1Char('\n')));
    }
  }

  QMessageBox box(this);
  box.setIcon(QMessageBox::Question);
  box.setWindowTitle(z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(title_id)));
  box.setText(message);
  box.setStandardButtons(QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);
  box.setDefaultButton(QMessageBox::Yes);

  if (QAbstractButton* yes = box.button(QMessageBox::Yes)) {
    yes->setText(localized_text(406));
  }
  if (QAbstractButton* no = box.button(QMessageBox::No)) {
    no->setText(localized_text(407));
  }
  if (QAbstractButton* cancel = box.button(QMessageBox::Cancel)) {
    cancel->setText(localized_text(402));
  }

  if (box.exec() != QMessageBox::Yes) {
    return;
  }

  bool removed_any = false;
  for (const TempFilesListEntry& entry : entries) {
    const bool remove_as_dir = entry.is_dir && !entry.is_symlink;
    const TempFilesDeletePathResult remove_result =
        delete_temp_path_with_system_error(entry.absolute_path, remove_as_dir);
    if (!remove_result.ok) {
      if (removed_any) {
        reload();
      }
      QStringList lines;
      if (!remove_result.system_error_text.isEmpty()) {
        lines.push_back(remove_result.system_error_text);
      }
      lines.push_back(QDir::toNativeSeparators(remove_result.failed_path));
      QMessageBox::warning(
          this,
          z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(6107)),
          lines.join(QLatin1Char('\n')));
      return;
    }
    removed_any = true;
  }

  reload();
}

void TempFilesDialog::on_context_menu(const QPoint& pos) {
  if (context_menu_ == nullptr || context_delete_action_ == nullptr ||
      context_open_outside_action_ == nullptr ||
      context_open_outside_7zip_action_ == nullptr ||
      context_properties_action_ == nullptr) {
    return;
  }

  const QModelIndex index = table_->indexAt(pos);
  if (index.isValid() && !table_->selectionModel()->isSelected(index)) {
    table_->selectRow(index.row());
  }
  if (index.isValid()) {
    table_->setCurrentIndex(index);
  }
  update_context_menu_actions();
  context_menu_->exec(table_->viewport()->mapToGlobal(pos));
}

void TempFilesDialog::update_context_menu_actions() {
  if (context_delete_action_ == nullptr || context_open_outside_action_ == nullptr ||
      context_open_outside_7zip_action_ == nullptr ||
      context_properties_action_ == nullptr || context_separator_after_delete_ == nullptr ||
      context_separator_before_properties_ == nullptr) {
    return;
  }

  const int selected_count = selected_entries().size();
  const bool has_selection = selected_count > 0;
  const bool has_single_selection = selected_count == 1;
  const bool allow_open_variants = selected_count <= 1;

  context_delete_action_->setVisible(has_selection);
  context_delete_action_->setEnabled(has_selection);

  context_separator_after_delete_->setVisible(has_single_selection);

  context_open_outside_action_->setVisible(allow_open_variants);
  context_open_outside_action_->setEnabled(allow_open_variants);
  context_open_outside_7zip_action_->setVisible(allow_open_variants);
  context_open_outside_7zip_action_->setEnabled(allow_open_variants);

  context_separator_before_properties_->setVisible(allow_open_variants);
  context_properties_action_->setVisible(allow_open_variants);
  context_properties_action_->setEnabled(allow_open_variants);
}

void TempFilesDialog::on_open_outside_requested() {
  const QVector<TempFilesListEntry> entries = selected_entries();
  if (entries.size() > 1) {
    return;
  }

  QString target_path = model_.current_path();
  if (entries.size() == 1) {
    target_path = entries.front().absolute_path;
  }
  if (target_path.trimmed().isEmpty()) {
    return;
  }

  open_path_externally_with_warning(target_path);
}

void TempFilesDialog::on_open_outside_7zip_requested() {
  const QVector<TempFilesListEntry> entries = selected_entries();
  if (entries.size() > 1) {
    return;
  }

  QString target_path = model_.current_path();
  if (entries.size() == 1) {
    target_path = entries.front().absolute_path;
  }
  if (target_path.trimmed().isEmpty()) {
    return;
  }

  if (!launch_path_in_new_7zfm(target_path, model_.current_path())) {
    QMessageBox::warning(
        this,
        localized_text(542),
        QStringLiteral("Failed to launch 7zFM:\n%1")
            .arg(QDir::toNativeSeparators(target_path)));
  }
}

void TempFilesDialog::on_properties_requested() {
  show_selected_entry_properties();
}

void TempFilesDialog::show_selected_entry_properties() {
  const QVector<TempFilesListEntry> entries = selected_entries();
  if (entries.size() != 1) {
    return;
  }
  const QString text = build_temp_entry_properties_text(entries.front());
  if (text.isEmpty()) {
    return;
  }
  QMessageBox::information(
      this,
      localized_text(551),
      text);
}

void TempFilesDialog::show_blocked_operation_warning() {
  QMessageBox::warning(
      this,
      QStringLiteral("7-Zip"),
      z7::ui::runtime_support::L(3014));
}

bool TempFilesDialog::open_path_externally_with_warning(const QString& path) {
  if (path.trimmed().isEmpty()) {
    return false;
  }
  if (QDesktopServices::openUrl(QUrl::fromLocalFile(path))) {
    return true;
  }
  QMessageBox::warning(
      this,
      localized_text(542),
      QStringLiteral("Failed to open:\n%1")
          .arg(QDir::toNativeSeparators(path)));
  return false;
}

QVector<TempFilesListEntry> TempFilesDialog::selected_entries() const {
  QVector<TempFilesListEntry> rows;
  if (table_ == nullptr || table_->selectionModel() == nullptr) {
    return rows;
  }

  QModelIndexList selected = table_->selectionModel()->selectedRows(0);
  std::sort(selected.begin(), selected.end(), [](const QModelIndex& a, const QModelIndex& b) {
    return a.row() < b.row();
  });

  rows.reserve(selected.size());
  for (const QModelIndex& index : selected) {
    QTableWidgetItem* item = table_->item(index.row(), 0);
    if (item == nullptr) {
      continue;
    }
    TempFilesListEntry row;
    row.name = item->text();
    row.absolute_path = item->data(kPathRole).toString();
    row.is_dir = item->data(kIsDirRole).toBool();
    row.is_symlink = item->data(kIsSymlinkRole).toBool();
    rows.push_back(std::move(row));
  }
  return rows;
}

}  // namespace z7::ui::filemanager
