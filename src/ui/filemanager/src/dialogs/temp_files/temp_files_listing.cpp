// src/ui/filemanager/src/temp_files_listing.cpp
// Role: Entry collection and sort comparator implementation for temp-files dialog.

#include "temp_files_listing.h"
#include "temp_files_dialog_service.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>

#include <algorithm>

namespace z7::ui::filemanager {

namespace {

bool matches_original_temp_name_pattern(const QString& name) {
  static const QRegularExpression kPattern(
      QStringLiteral("^7z[EOS][0-9A-Fa-f]{8}$"));
  return kPattern.match(name).hasMatch();
}

int compare_text_ci(const QString& left, const QString& right) {
  return QString::compare(left, right, Qt::CaseInsensitive);
}

int compare_optional_name(const QString& left, const QString& right) {
  const bool left_empty = left.isEmpty();
  const bool right_empty = right.isEmpty();
  if (left_empty != right_empty) {
    return left_empty ? -1 : 1;
  }
  return compare_text_ci(left, right);
}

int compare_entries(const TempFilesListEntry& left,
                    const TempFilesListEntry& right,
                    int sort_column,
                    bool ascending) {
  if (left.is_dir != right.is_dir) {
    return left.is_dir ? -1 : 1;
  }

  int result = 0;
  switch (sort_column) {
    case 0:
      result = compare_text_ci(left.name, right.name);
      break;
    case 1: {
      const qint64 left_time = left.modified.isValid()
                                   ? left.modified.toMSecsSinceEpoch()
                                   : 0;
      const qint64 right_time = right.modified.isValid()
                                    ? right.modified.toMSecsSinceEpoch()
                                    : 0;
      if (left_time < right_time) {
        result = -1;
      } else if (left_time > right_time) {
        result = 1;
      }
      break;
    }
    case 2:
      if (left.size < right.size) {
        result = -1;
      } else if (left.size > right.size) {
        result = 1;
      }
      break;
    case 3:
      if (left.num_files < right.num_files) {
        result = -1;
      } else if (left.num_files > right.num_files) {
        result = 1;
      }
      break;
    case 4:
      if (left.num_dirs < right.num_dirs) {
        result = -1;
      } else if (left.num_dirs > right.num_dirs) {
        result = 1;
      }
      break;
    case 5:
      result = compare_optional_name(left.sub_file_name, right.sub_file_name);
      break;
    default:
      result = compare_text_ci(left.name, right.name);
      break;
  }

  if (result == 0) {
    if (left.load_index < right.load_index) {
      result = -1;
    } else if (left.load_index > right.load_index) {
      result = 1;
    }
  }

  return ascending ? result : -result;
}

}  // namespace

QVector<TempFilesListEntry> collect_temp_files_entries(const QString& current_path,
                                                       bool filter_root_names) {
  QVector<TempFilesListEntry> rows;
  QDir dir(current_path);
  if (!dir.exists()) {
    return rows;
  }

  const QFileInfoList infos = dir.entryInfoList(QDir::AllEntries |
                                                    QDir::NoDotAndDotDot |
                                                    QDir::Hidden |
                                                    QDir::System,
                                                QDir::NoSort);
  rows.reserve(infos.size());
  int load_index = 0;
  for (const QFileInfo& info : infos) {
    const QString name = info.fileName();
    if (filter_root_names && !matches_original_temp_name_pattern(name)) {
      continue;
    }

    TempFilesListEntry row;
    row.name = name;
    row.absolute_path = QDir::cleanPath(info.absoluteFilePath());
    row.modified = info.lastModified();
    row.is_dir = info.isDir();
    row.is_symlink = info.isSymLink();
    row.load_index = load_index++;

    if (row.is_dir && !row.is_symlink) {
      const TempFilesDirectoryStats aggregate =
          collect_temp_directory_stats(row.absolute_path);
      row.size = aggregate.size;
      row.num_files = aggregate.num_files;
      row.num_dirs = aggregate.num_dirs;
      row.sub_file_name = aggregate.sub_file_name;
    } else {
      row.size = static_cast<quint64>(info.size());
    }

    rows.push_back(std::move(row));
  }

  return rows;
}

void sort_temp_files_entries(QVector<TempFilesListEntry>* entries,
                             int sort_column,
                             bool ascending) {
  if (entries == nullptr) {
    return;
  }
  std::sort(entries->begin(), entries->end(),
            [sort_column, ascending](const TempFilesListEntry& left,
                                     const TempFilesListEntry& right) {
              return compare_entries(left, right, sort_column, ascending) < 0;
            });
}

}  // namespace z7::ui::filemanager
