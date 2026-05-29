// src/ui/filemanager/src/temp_files_dialog_service.cpp
// Role: Temp-files dialog filesystem service and property text assembly.

#include "temp_files_dialog_service.h"
#include "official_lang_catalog.h"
#include "filemanager_instance_launcher.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <filesystem>
#include <system_error>

namespace z7::ui::filemanager {

namespace {

const QDir::Filters kDirEntryFilters =
    QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System;

std::filesystem::path to_filesystem_path(const QString& absolute_path) {
#ifdef Q_OS_WIN
  return std::filesystem::path(absolute_path.toStdWString());
#else
  const QByteArray native = QFile::encodeName(absolute_path);
  return std::filesystem::path(
      std::string(native.constData(), static_cast<size_t>(native.size())));
#endif
}

bool is_real_directory(const QFileInfo& info) {
  return info.isDir() && !info.isSymLink();
}

TempFilesDirectoryStats collect_temp_directory_stats_impl(const QString& root_path,
                                                          bool root_level) {
  TempFilesDirectoryStats stats;
  const QDir dir(root_path);
  if (!dir.exists()) {
    return stats;
  }

  const QFileInfoList children = dir.entryInfoList(kDirEntryFilters, QDir::NoSort);
  int root_item_count = 0;
  for (const QFileInfo& child : children) {
    if (root_level) {
      ++root_item_count;
      if (root_item_count == 1) {
        stats.sub_file_name = child.fileName();
      }
    }

    // Keep recursive stats scoped to the real directory tree; never follow directory symlinks.
    if (is_real_directory(child)) {
      ++stats.num_dirs;
      const TempFilesDirectoryStats nested =
          collect_temp_directory_stats_impl(child.absoluteFilePath(), false);
      stats.size += nested.size;
      stats.num_files += nested.num_files;
      stats.num_dirs += nested.num_dirs;
      continue;
    }

    if (!child.isDir()) {
      ++stats.num_files;
      stats.size += static_cast<quint64>(child.size());
    }
  }

  if (root_level && root_item_count != 1) {
    stats.sub_file_name.clear();
  }
  return stats;
}

QString localized_text(const uint32_t id) {
  return z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(id)).trimmed();
}

}  // namespace

TempFilesDeletePathResult delete_temp_path_with_system_error(
    const QString& absolute_path,
    bool is_dir) {
  TempFilesDeletePathResult result;
  result.failed_path = absolute_path;

  std::error_code ec;
  const std::filesystem::path native_path = to_filesystem_path(absolute_path);
  if (is_dir) {
    const uintmax_t removed_count = std::filesystem::remove_all(native_path, ec);
    if (!ec && removed_count == 0) {
      ec = std::make_error_code(std::errc::no_such_file_or_directory);
    }
  } else {
    const bool removed = std::filesystem::remove(native_path, ec);
    if (!ec && !removed) {
      ec = std::make_error_code(std::errc::no_such_file_or_directory);
    }
  }

  if (!ec) {
    return result;
  }

  result.ok = false;
  result.system_error_text = QString::fromLocal8Bit(ec.message()).trimmed();
  return result;
}

TempFilesDirectoryStats collect_temp_directory_stats(const QString& root_path) {
  return collect_temp_directory_stats_impl(root_path, true);
}

QString format_grouped_uint64(const quint64 value) {
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

QString format_datetime(const QDateTime& value) {
  if (!value.isValid()) {
    return QStringLiteral("-");
  }
  return value.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
}

QString build_temp_entry_properties_text(const TempFilesListEntry& entry) {
  const QFileInfo info(entry.absolute_path);
  if (!info.exists()) {
    return QStringLiteral("Path does not exist:\n%1")
        .arg(QDir::toNativeSeparators(entry.absolute_path));
  }

  const QString label_name = localized_text(1004);
  const QString label_type = localized_text(1020);
  const QString label_size = localized_text(1007);
  const QString label_modified = localized_text(1012);
  const QString label_created = localized_text(1010);
  const QString label_path = localized_text(1003);
  const QString label_folders = localized_text(1031);
  const QString label_files = localized_text(1032);

  QStringList lines;
  lines.push_back(
      QStringLiteral("%1: %2")
          .arg(label_name,
               info.fileName().isEmpty() ? info.absoluteFilePath() : info.fileName()));
  if (info.isDir() && !info.isSymLink()) {
    lines.push_back(QStringLiteral("%1: %2")
                        .arg(label_type,
                             localized_text(1006)));
    const TempFilesDirectoryStats aggregate =
        collect_temp_directory_stats(info.absoluteFilePath());
    lines.push_back(
        QStringLiteral("%1: %2")
            .arg(label_folders, QString::number(aggregate.num_dirs)));
    lines.push_back(
        QStringLiteral("%1: %2")
            .arg(label_files, QString::number(aggregate.num_files)));
    lines.push_back(
        QStringLiteral("%1: %2")
            .arg(label_size, format_grouped_uint64(aggregate.size)));
  } else {
    lines.push_back(
        QStringLiteral("%1: %2")
            .arg(label_type,
                 z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(500))));
    lines.push_back(
        QStringLiteral("%1: %2")
            .arg(label_size, format_grouped_uint64(static_cast<quint64>(info.size()))));
  }
  lines.push_back(QStringLiteral("%1: %2").arg(label_modified, format_datetime(info.lastModified())));
  const QDateTime created = info.birthTime().isValid() ? info.birthTime() : info.lastModified();
  lines.push_back(QStringLiteral("%1: %2").arg(label_created, format_datetime(created)));
  lines.push_back(QStringLiteral("%1: %2")
                      .arg(label_path, QDir::toNativeSeparators(info.absoluteFilePath())));
  return lines.join(QLatin1Char('\n'));
}

bool launch_path_in_new_7zfm(const QString& path,
                             const QString& fallback_working_dir) {
  const QString trimmed = path.trimmed();
  if (trimmed.isEmpty()) {
    return false;
  }

  const QFileInfo info(trimmed);
  QString working_dir = info.isDir() ? info.absoluteFilePath() : info.absolutePath();
  if (working_dir.trimmed().isEmpty()) {
    working_dir = fallback_working_dir;
  }
  return z7::platform::qt::filemanager_instance_launcher::
      launch_open_request_for_current_app(
      trimmed,
      QString(),
      working_dir,
      nullptr);
}

}  // namespace z7::ui::filemanager
