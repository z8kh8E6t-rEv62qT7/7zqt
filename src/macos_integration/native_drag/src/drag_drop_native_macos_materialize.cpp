#include "internal.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QThread>

namespace z7::macos_integration::native_drag::detail {

QString native_drag_log_prefix(MacOSIntegrationNativeDragKind kind) {
  switch (kind) {
    case MacOSIntegrationNativeDragKind::kFilesystem:
      return QStringLiteral("filesystem-native-drag");
    case MacOSIntegrationNativeDragKind::kArchive:
    default:
      return QStringLiteral("archive-native-drag");
  }
}

namespace {

bool copy_directory_recursive(const QString& source_path,
                              const QString& destination_path,
                              QString* error_message) {
  const QDir source_dir(source_path);
  if (!source_dir.exists()) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("Filesystem drag source directory does not exist: %1")
                           .arg(QDir::toNativeSeparators(source_path));
    }
    return false;
  }

  if (!QDir().mkpath(destination_path)) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("Cannot create destination directory: %1")
                           .arg(QDir::toNativeSeparators(destination_path));
    }
    return false;
  }

  const QFileInfoList entries = source_dir.entryInfoList(
      QDir::NoDotAndDotDot | QDir::AllEntries | QDir::Hidden | QDir::System);
  for (const QFileInfo& entry : entries) {
    const QString child_source = entry.absoluteFilePath();
    const QString child_destination =
        QDir(destination_path).filePath(entry.fileName());
    if (entry.isDir() && !entry.isSymLink()) {
      if (!copy_directory_recursive(child_source,
                                    child_destination,
                                    error_message)) {
        return false;
      }
      continue;
    }
    if (QFileInfo::exists(child_destination) &&
        !QFile::remove(child_destination)) {
      if (error_message != nullptr) {
        *error_message = QStringLiteral("Cannot replace destination file: %1")
                             .arg(QDir::toNativeSeparators(child_destination));
      }
      return false;
    }
    if (!QFile::copy(child_source, child_destination)) {
      if (error_message != nullptr) {
        *error_message = QStringLiteral("Cannot copy file to destination: %1")
                             .arg(QDir::toNativeSeparators(child_destination));
      }
      return false;
    }
  }
  return true;
}

bool remove_existing_path(const QString& path, QString* error_message) {
  const QFileInfo existing(path);
  if (!existing.exists()) {
    return true;
  }
  if (existing.isDir() && !existing.isSymLink()) {
    if (QDir(path).removeRecursively()) {
      return true;
    }
  } else if (QFile::remove(path)) {
    return true;
  }
  if (error_message != nullptr) {
    *error_message =
        QStringLiteral("Cannot replace existing destination: %1")
            .arg(QDir::toNativeSeparators(path));
  }
  return false;
}

}  // namespace

bool copy_source_path_to_destination(const QString& source_path,
                                     bool source_is_dir,
                                     const QString& target_directory,
                                     const QString& promised_name,
                                     QString* error) {
  const QString normalized_source = source_path.trimmed();
  const QString normalized_target_directory = target_directory.trimmed();
  const QString normalized_promised_name = promised_name.trimmed();
  if (normalized_source.isEmpty() ||
      normalized_target_directory.isEmpty() ||
      normalized_promised_name.isEmpty()) {
    if (error != nullptr) {
      *error = QStringLiteral("Filesystem drag destination state is invalid.");
    }
    return false;
  }

  const QFileInfo source_info(normalized_source);
  if (!source_info.exists()) {
    if (error != nullptr) {
      *error = QStringLiteral("Filesystem drag source path does not exist: %1")
                   .arg(QDir::toNativeSeparators(normalized_source));
    }
    return false;
  }

  if (!QDir().mkpath(normalized_target_directory)) {
    if (error != nullptr) {
      *error = QStringLiteral("Cannot create target directory: %1")
                   .arg(QDir::toNativeSeparators(normalized_target_directory));
    }
    return false;
  }

  const QString destination_path =
      QDir(normalized_target_directory).filePath(normalized_promised_name);
  if (!remove_existing_path(destination_path, error)) {
    return false;
  }

  if (source_is_dir && !source_info.isSymLink()) {
    return copy_directory_recursive(normalized_source, destination_path, error);
  }

  if (!QFile::copy(normalized_source, destination_path)) {
    if (error != nullptr) {
      *error = QStringLiteral("Cannot copy filesystem drag item to destination: %1")
                   .arg(QDir::toNativeSeparators(destination_path));
    }
    return false;
  }
  return true;
}

bool write_to_destination_if_needed(MacOSIntegrationNativeDragItem* item,
                                    const QString& destination_path,
                                    QString* error) {
  if (item == nullptr) {
    if (error != nullptr) {
      *error = QStringLiteral("Invalid drag item state.");
    }
    return false;
  }

  const QString normalized_destination = destination_path.trimmed();
  if (normalized_destination.isEmpty()) {
    if (error != nullptr) {
      *error = QStringLiteral("Promise destination path is empty.");
    }
    return false;
  }
  const QFileInfo destination_info(normalized_destination);
  const QString target_directory = destination_info.absolutePath().trimmed();
  const QString promised_name = destination_info.fileName().trimmed();
  if (target_directory.isEmpty() || promised_name.isEmpty()) {
    if (error != nullptr) {
      *error = QStringLiteral("Promise destination path is invalid: %1")
                   .arg(QDir::toNativeSeparators(normalized_destination));
    }
    return false;
  }

  if (!item->source_path.trimmed().isEmpty()) {
    return copy_source_path_to_destination(item->source_path,
                                           item->is_dir,
                                           target_directory,
                                           promised_name,
                                           error);
  }

  if (!item->write_to_destination) {
    if (error != nullptr) {
      *error = QStringLiteral("Missing direct-export writer for drag item '%1'.")
                   .arg(default_item_name(*item));
    }
    return false;
  }

  bool write_ok = false;
  QString writer_error;
  const auto run_writer = [&]() {
    write_ok = item->write_to_destination(normalized_destination, &writer_error);
  };

  QCoreApplication* app = QCoreApplication::instance();
  if (app != nullptr && QThread::currentThread() != app->thread()) {
    const bool invoked = QMetaObject::invokeMethod(
        app, run_writer, Qt::BlockingQueuedConnection);
    if (!invoked) {
      if (error != nullptr) {
        *error = QStringLiteral("Failed to invoke archive direct export on UI thread.");
      }
      return false;
    }
  } else {
    run_writer();
  }

  if (write_ok) {
    return true;
  }

  if (error != nullptr) {
    *error = writer_error.trimmed().isEmpty()
                 ? QStringLiteral("Failed to export archive entry '%1' to destination.")
                       .arg(item->archive_entry_path.trimmed())
                 : writer_error.trimmed();
  }
  return false;
}

}  // namespace z7::macos_integration::native_drag::detail
