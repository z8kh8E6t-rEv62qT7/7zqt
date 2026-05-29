// src/ui/filemanager/src/temp_files_dialog_model.cpp
// Role: Temp-files dialog state transitions and sorted entry refresh.

#include "temp_files_dialog_model.h"

#include <QDir>
#include <QFileInfo>

namespace z7::ui::filemanager {

TempFilesDialogModel::TempFilesDialogModel(const QString& temp_root_path)
    : temp_root_path_(normalized_absolute_path(temp_root_path)),
      current_path_(temp_root_path_) {}

void TempFilesDialogModel::reload() {
  entries_ = collect_temp_files_entries(current_path_, is_at_root());
  sort_entries();
}

void TempFilesDialogModel::open_parent() {
  if (is_at_root()) {
    return;
  }
  const QString parent_path =
      normalized_absolute_path(QFileInfo(current_path_).dir().path());
  set_current_path(parent_path);
}

void TempFilesDialogModel::apply_sort_column(int column_index, int column_count) {
  if (column_index < 0 || column_index >= column_count) {
    return;
  }
  if (column_index == sort_column_) {
    ascending_ = !ascending_;
  } else {
    sort_column_ = column_index;
    ascending_ = (column_index == 0 || column_index == 5);
  }
  sort_entries();
}

bool TempFilesDialogModel::set_current_path(const QString& path) {
  const QString normalized = normalized_absolute_path(path);
  const QString old_path = current_path_;
  if (is_path_inside_temp_root(normalized)) {
    current_path_ = normalized;
  } else {
    current_path_ = temp_root_path_;
  }
  return current_path_ != old_path;
}

bool TempFilesDialogModel::is_at_root() const {
  return normalized_absolute_path(current_path_) == temp_root_path_;
}

QString TempFilesDialogModel::normalized_absolute_path(const QString& path) {
  return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

bool TempFilesDialogModel::is_path_inside_temp_root(const QString& path) const {
  const QString normalized = normalized_absolute_path(path);
  if (normalized == temp_root_path_) {
    return true;
  }
  QString root_prefix = temp_root_path_;
  if (!root_prefix.endsWith(QDir::separator())) {
    root_prefix += QDir::separator();
  }
#ifdef Q_OS_WIN
  return normalized.startsWith(root_prefix, Qt::CaseInsensitive);
#else
  return normalized.startsWith(root_prefix, Qt::CaseSensitive);
#endif
}

void TempFilesDialogModel::sort_entries() {
  sort_temp_files_entries(&entries_, sort_column_, ascending_);
}

}  // namespace z7::ui::filemanager
