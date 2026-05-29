// src/ui/filemanager/src/temp_files_listing.h
// Role: Entry collection, aggregation, and sorting for temp-files dialog.

#pragma once

#include <QDateTime>
#include <QString>
#include <QVector>

namespace z7::ui::filemanager {

struct TempFilesListEntry {
  QString name;
  QString absolute_path;
  QDateTime modified;
  quint64 size = 0;
  quint64 num_files = 0;
  quint64 num_dirs = 0;
  QString sub_file_name;
  int load_index = 0;
  bool is_dir = false;
  bool is_symlink = false;
};

QVector<TempFilesListEntry> collect_temp_files_entries(const QString& current_path,
                                                       bool filter_root_names);

void sort_temp_files_entries(QVector<TempFilesListEntry>* entries,
                             int sort_column,
                             bool ascending);

}  // namespace z7::ui::filemanager

