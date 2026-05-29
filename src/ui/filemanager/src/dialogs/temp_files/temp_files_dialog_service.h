// src/ui/filemanager/src/temp_files_dialog_service.h
// Role: Filesystem operations and text formatting used by temp-files dialog.

#pragma once

#include <QDateTime>
#include <QString>

#include "temp_files_listing.h"

namespace z7::ui::filemanager {

struct TempFilesDeletePathResult {
  bool ok = true;
  QString system_error_text;
  QString failed_path;
};

struct TempFilesDirectoryStats {
  quint64 size = 0;
  quint64 num_files = 0;
  quint64 num_dirs = 0;
  QString sub_file_name;
};

TempFilesDeletePathResult delete_temp_path_with_system_error(
    const QString& absolute_path,
    bool is_dir);

TempFilesDirectoryStats collect_temp_directory_stats(const QString& root_path);

QString format_grouped_uint64(quint64 value);
QString format_datetime(const QDateTime& value);

QString build_temp_entry_properties_text(const TempFilesListEntry& entry);

bool launch_path_in_new_7zfm(const QString& path,
                             const QString& fallback_working_dir);

}  // namespace z7::ui::filemanager
