// src/ui/filemanager/src/main_window/model/directory_list_model_data.cpp
// Role: Core table data and header rendering for directory list model.

#include "model.h"
#include "main_window/drag_drop/drag_source_marker.h"

#include <QDateTime>
#include <QEventLoop>
#include <QFileInfo>
#include <QSet>
#include <QTimeZone>
#include <QUrl>
#include <QVariant>

#include "drag_drop_policy_qt.h"
#include "official_lang_catalog.h"
#include "structured_list_proxy.h"

namespace z7::ui::filemanager {

namespace {

class ArchiveLazyMimeData final : public QMimeData {
 public:
  ArchiveLazyMimeData(const QString& source_archive,
                      const QString& archive_type_hint,
                      const QStringList& entries,
                      quint64 session_token_value,
                      DirectoryListModel::ArchiveDragMaterializer materializer)
      : source_archive_(source_archive.trimmed()),
        archive_type_hint_(archive_type_hint.trimmed()),
        entries_(entries),
        session_token_value_(session_token_value),
        materializer_(std::move(materializer)) {
    const QByteArray marker = make_internal_archive_source_marker(
        source_archive_,
        archive_type_hint_,
        entries_,
        session_token_value_);
    if (!marker.isEmpty()) {
      setData(QString::fromLatin1(kMimeTypeZ7FmArchiveSource), marker);
    }
    if (z7::platform::qt::mac_archive_native_drag_enabled()) {
      const QByteArray promise_payload =
          z7::platform::qt::encode_mac_archive_promise_payload(
              source_archive_,
              archive_type_hint_,
              entries_);
      if (!promise_payload.isEmpty()) {
        setData(z7::platform::qt::mac_archive_promise_mime_type(),
                promise_payload);
      }
    }
  }

 protected:
  QStringList formats() const override {
    QStringList out = QMimeData::formats();
    if (z7::platform::qt::mac_archive_native_drag_enabled()) {
      const QString promise_format =
          z7::platform::qt::mac_archive_promise_mime_type();
      if (!promise_format.isEmpty() && !out.contains(promise_format)) {
        out.push_back(promise_format);
      }
    }
    const QString uri_format = QStringLiteral("text/uri-list");
    if (!out.contains(uri_format)) {
      out.push_back(uri_format);
    }
    return out;
  }

  bool hasFormat(const QString& mime_type) const override {
    if (mime_type == z7::platform::qt::mac_archive_promise_mime_type()) {
      return z7::platform::qt::mac_archive_native_drag_enabled() &&
             !source_archive_.isEmpty() &&
             !entries_.isEmpty();
    }
    if (mime_type == QStringLiteral("text/uri-list")) {
      return !source_archive_.isEmpty() && !entries_.isEmpty();
    }
    return QMimeData::hasFormat(mime_type);
  }

  QVariant retrieveData(const QString& mime_type,
                        QMetaType type) const override {
    if (mime_type == z7::platform::qt::mac_archive_promise_mime_type()) {
      mark_transfer_requested();
      return QMimeData::retrieveData(mime_type, type);
    }
    if (mime_type == QStringLiteral("text/uri-list")) {
      mark_transfer_requested();
      ensure_urls_materialized();
      return QMimeData::retrieveData(mime_type, type);
    }
    return QMimeData::retrieveData(mime_type, type);
  }

 private:
  void ensure_urls_materialized() const {
    if (urls_materialized_) {
      return;
    }
    urls_materialized_ = true;
    if (!materializer_) {
      return;
    }

    QEventLoop wait_loop;
    bool finished = false;
    materializer_(entries_,
                  [this, &wait_loop, &finished](const QStringList& paths,
                                                const QString& error_message) {
                    QList<QUrl> urls;
                    urls.reserve(paths.size());
                    for (const QString& path : paths) {
                      const QString trimmed_path = path.trimmed();
                      if (trimmed_path.isEmpty()) {
                        continue;
                      }
                      urls.push_back(
                          QUrl::fromLocalFile(QFileInfo(trimmed_path).absoluteFilePath()));
                    }
                    const_cast<ArchiveLazyMimeData*>(this)->setUrls(urls);
                    const QString trimmed_error = error_message.trimmed();
                    if (!trimmed_error.isEmpty()) {
                      const_cast<ArchiveLazyMimeData*>(this)->setData(
                          QString::fromLatin1(kMimeTypeZ7FmArchiveMaterializationError),
                          trimmed_error.toUtf8());
                    }
                    finished = true;
                    if (wait_loop.isRunning()) {
                      wait_loop.quit();
                    }
                  });
    if (!finished) {
      wait_loop.exec();
    }
  }

  void mark_transfer_requested() const {
    const_cast<ArchiveLazyMimeData*>(this)->setData(
        QString::fromLatin1(kMimeTypeZ7FmArchiveTransferRequested),
        QByteArrayLiteral("1"));
  }

  QString source_archive_;
  QString archive_type_hint_;
  QStringList entries_;
  quint64 session_token_value_ = 0;
  DirectoryListModel::ArchiveDragMaterializer materializer_;
  mutable bool urls_materialized_ = false;
};

}  // namespace

DirectoryListModel::DirectoryListModel(QObject* parent)
    : QAbstractTableModel(parent) {}

void DirectoryListModel::set_icon_style_context(QWidget* context) {
  icon_style_context_ = context;
}

int DirectoryListModel::rowCount(const QModelIndex& parent) const {
  if (parent.isValid()) {
    return 0;
  }
  return entries_.size();
}

int DirectoryListModel::columnCount(const QModelIndex& parent) const {
  if (parent.isValid()) {
    return 0;
  }
  return kColumnCount;
}

Qt::ItemFlags DirectoryListModel::flags(const QModelIndex& index) const {
  Qt::ItemFlags item_flags = QAbstractTableModel::flags(index);
  if (!index.isValid() || index.row() < 0 || index.row() >= entries_.size()) {
    return item_flags;
  }

  const Entry& entry = entries_[index.row()];
  const bool can_drag_fs = mode_ == DataMode::kFilesystem;
  const bool can_drag_archive = mode_ == DataMode::kVirtualArchive &&
                                !archive_drag_source_archive_.isEmpty() &&
                                static_cast<bool>(archive_drag_materializer_);
  if ((can_drag_fs || can_drag_archive) && !entry.is_parent_link &&
      !entry.path.trimmed().isEmpty()) {
    item_flags |= Qt::ItemIsDragEnabled;
  }
  if (rename_enabled_ && index.column() == kNameColumn &&
      !entry.is_parent_link && !entry.path.trimmed().isEmpty() &&
      static_cast<bool>(rename_handler_)) {
    item_flags |= Qt::ItemIsEditable;
  }
  return item_flags;
}

QStringList DirectoryListModel::mimeTypes() const {
  return {
      QStringLiteral("text/uri-list"),
      QString::fromLatin1(kMimeTypeZ7FmFsSource),
      QString::fromLatin1(kMimeTypeZ7FmArchiveSource),
  };
}

QMimeData* DirectoryListModel::mimeData(const QModelIndexList& indexes) const {
  if (indexes.isEmpty()) {
    return new QMimeData();
  }

  if (mode_ == DataMode::kVirtualArchive) {
    if (archive_drag_source_archive_.isEmpty() || !archive_drag_materializer_) {
      return new QMimeData();
    }

    QSet<int> seen_rows;
    QStringList entries;
    entries.reserve(indexes.size());
    for (const QModelIndex& index : indexes) {
      if (!index.isValid() || seen_rows.contains(index.row()) ||
          index.row() < 0 || index.row() >= entries_.size()) {
        continue;
      }
      seen_rows.insert(index.row());

      const Entry& entry = entries_[index.row()];
      if (entry.is_parent_link || entry.path.trimmed().isEmpty()) {
        continue;
      }
      entries << entry.path.trimmed();
    }
    entries.removeDuplicates();
    if (entries.isEmpty()) {
      return new QMimeData();
    }

    return new ArchiveLazyMimeData(archive_drag_source_archive_,
                                   archive_drag_type_hint_,
                                   entries,
                                   archive_drag_source_session_token_,
                                   archive_drag_materializer_);
  }

  auto* mime_data = new QMimeData();
  if (mode_ != DataMode::kFilesystem) {
    return mime_data;
  }

  QSet<int> seen_rows;
  QList<QUrl> urls;
  for (const QModelIndex& index : indexes) {
    if (!index.isValid() || seen_rows.contains(index.row()) ||
        index.row() < 0 || index.row() >= entries_.size()) {
      continue;
    }
    seen_rows.insert(index.row());

    const Entry& entry = entries_[index.row()];
    if (entry.is_parent_link || entry.path.trimmed().isEmpty()) {
      continue;
    }

    const QFileInfo info(entry.path);
    if (!info.exists()) {
      continue;
    }
    urls << QUrl::fromLocalFile(info.absoluteFilePath());
  }

  mime_data->setUrls(urls);
  if (!urls.isEmpty()) {
    mime_data->setData(QString::fromLatin1(kMimeTypeZ7FmFsSource),
                       make_internal_fs_source_marker());
  }
  return mime_data;
}

Qt::DropActions DirectoryListModel::supportedDragActions() const {
  return Qt::CopyAction | Qt::MoveAction;
}

QVariant DirectoryListModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid() || index.row() < 0 || index.row() >= entries_.size()) {
    return QVariant();
  }

  const Entry& entry = entries_[index.row()];

  if (role == Qt::UserRole) {
    return entry.path;
  }

  if (role == Qt::EditRole && index.column() == kNameColumn) {
    return entry.display_name;
  }

  if (role == Qt::DecorationRole && index.column() == kNameColumn) {
    return resolve_entry_icon(entry);
  }

  if (role == Qt::TextAlignmentRole) {
    switch (index.column()) {
      case kSizeColumn:
      case kPackedSizeColumn:
      case kCrcColumn:
      case kVolumeIndexColumn:
      case kOffsetColumn:
      case kFoldersColumn:
      case kFilesColumn:
        return QVariant::fromValue(
            static_cast<int>(Qt::AlignRight | Qt::AlignVCenter));
      case kEncryptedColumn:
        return QVariant::fromValue(
            static_cast<int>(Qt::AlignCenter));
      default:
        break;
    }
  }

  // Group key keeps parent link at the top, then directories, then files, all
  // regardless of sort order. The proxy consults this role before the primary
  // sort key.
  if (role == z7::ui::widgets::StructuredListSortFilterProxy::kSortGroupRole) {
    if (entry.is_parent_link) return 0;
    if (entry.is_dir) return 1;
    return 2;
  }

  if (role == z7::ui::widgets::StructuredListSortFilterProxy::kSortTieBreakRole) {
    return entry.load_index;
  }

  // Typed sort key per column; falls back to display value for unhandled
  // columns.
  if (role == z7::ui::widgets::StructuredListSortFilterProxy::kSortKeyRole) {
    switch (index.column()) {
      case kNameColumn:
        return entry.display_name;
      case kTypeSortColumn: {
        const QString type_key =
            entry.is_dir ? QString() : extension_for_icon_lookup(entry.display_name);
        return type_key.toCaseFolded();
      }
      case kSizeColumn:
        if (entry.is_dir && !entry.directory_size_defined) {
          return QVariant(static_cast<qulonglong>(0));
        }
        return QVariant(static_cast<qulonglong>(entry.size));
      case kPackedSizeColumn:
        if (entry.is_dir || !entry.packed_size.has_value()) {
          return QVariant(static_cast<qulonglong>(0));
        }
        return QVariant(static_cast<qulonglong>(*entry.packed_size));
      case kModifiedColumn: {
        if (entry.mtime_msecs_utc.has_value()) {
          return QDateTime::fromMSecsSinceEpoch(*entry.mtime_msecs_utc,
                                                QTimeZone::UTC);
        }
        const QDateTime fs = entry.info.lastModified();
        return fs.isValid() ? fs : QDateTime();
      }
      case kCreatedColumn: {
        if (entry.ctime_msecs_utc.has_value()) {
          return QDateTime::fromMSecsSinceEpoch(*entry.ctime_msecs_utc,
                                                QTimeZone::UTC);
        }
        if (!entry.is_virtual) {
          const QDateTime fs = entry.info.birthTime().isValid()
                                   ? entry.info.birthTime()
                                   : entry.info.lastModified();
          return fs;
        }
        return QDateTime();
      }
      case kAccessedColumn: {
        if (entry.atime_msecs_utc.has_value()) {
          return QDateTime::fromMSecsSinceEpoch(*entry.atime_msecs_utc,
                                                QTimeZone::UTC);
        }
        const QDateTime fs = entry.info.lastRead();
        return fs.isValid() ? fs : QDateTime();
      }
      case kCrcColumn:
        if (!entry.crc.has_value()) return QVariant(0u);
        return QVariant(static_cast<uint>(*entry.crc));
      case kVolumeIndexColumn:
        if (!entry.volume_index.has_value()) {
          return QVariant(static_cast<qulonglong>(0));
        }
        return QVariant(static_cast<qulonglong>(*entry.volume_index));
      case kOffsetColumn:
        if (!entry.offset.has_value()) {
          return QVariant(static_cast<qulonglong>(0));
        }
        return QVariant(static_cast<qulonglong>(*entry.offset));
      case kFoldersColumn:
        if (!entry.num_sub_dirs.has_value()) {
          return QVariant(static_cast<qulonglong>(0));
        }
        return QVariant(static_cast<qulonglong>(*entry.num_sub_dirs));
      case kFilesColumn:
        if (!entry.num_sub_files.has_value()) {
          return QVariant(static_cast<qulonglong>(0));
        }
        return QVariant(static_cast<qulonglong>(*entry.num_sub_files));
      default:
        break;
    }
    // Fall through to display role for remaining columns (attributes, encrypted,
    // comment, method, characts, host_os, version).
  }

  if (role != Qt::DisplayRole &&
      role != z7::ui::widgets::StructuredListSortFilterProxy::kSortKeyRole) {
    return QVariant();
  }

  switch (index.column()) {
    case kNameColumn:
      return entry.display_name;
    case kSizeColumn:
      if (entry.is_dir && !entry.directory_size_defined) {
        return QString();
      }
      return QString::number(entry.size);
    case kPackedSizeColumn:
      if (entry.is_dir || !entry.packed_size.has_value()) {
        return QString();
      }
      return QString::number(*entry.packed_size);
    case kModifiedColumn:
      return format_timestamp_for_entry(entry.mtime_msecs_utc,
                                        entry.info.lastModified());
    case kCreatedColumn: {
      QDateTime created;
      if (!entry.is_virtual) {
        created = entry.info.birthTime().isValid()
                      ? entry.info.birthTime()
                      : entry.info.lastModified();
      }
      return format_timestamp_for_entry(entry.ctime_msecs_utc, created);
    }
    case kAccessedColumn:
      return format_timestamp_for_entry(entry.atime_msecs_utc,
                                        entry.info.lastRead());
    case kAttributesColumn:
      return entry.attributes;
    case kEncryptedColumn:
      if (!entry.encrypted.has_value()) {
        return QString();
      }
      return *entry.encrypted ? QStringLiteral("+") : QStringLiteral("-");
    case kCommentColumn:
      return entry.comment;
    case kCrcColumn:
      if (!entry.crc.has_value()) {
        return QString();
      }
      return QStringLiteral("%1")
          .arg(static_cast<qulonglong>(*entry.crc), 8, 16, QLatin1Char('0'))
          .toUpper();
    case kMethodColumn:
      return entry.method;
    case kCharactsColumn:
      return entry.characts;
    case kHostOsColumn:
      return entry.host_os;
    case kVersionColumn:
      return entry.version;
    case kVolumeIndexColumn:
      if (!entry.volume_index.has_value()) {
        return QString();
      }
      return QString::number(*entry.volume_index);
    case kOffsetColumn:
      if (!entry.offset.has_value()) {
        return QString();
      }
      return QString::number(*entry.offset);
    case kFoldersColumn:
      if (!entry.is_dir || !entry.num_sub_dirs.has_value()) {
        return QString();
      }
      return QString::number(*entry.num_sub_dirs);
    case kFilesColumn:
      if (!entry.is_dir || !entry.num_sub_files.has_value()) {
        return QString();
      }
      return QString::number(*entry.num_sub_files);
    default:
      return QVariant();
  }
}

bool DirectoryListModel::setData(const QModelIndex& index,
                                 const QVariant& value,
                                 int role) {
  if (role != Qt::EditRole || !index.isValid() ||
      index.column() != kNameColumn || index.row() < 0 ||
      index.row() >= entries_.size() || !rename_enabled_ ||
      !rename_handler_) {
    return false;
  }

  const Entry& entry = entries_[index.row()];
  if (entry.is_parent_link || entry.path.trimmed().isEmpty()) {
    return false;
  }

  return rename_handler_(entry.path, value.toString(), entry.is_dir);
}

QVariant DirectoryListModel::headerData(int section,
                                        Qt::Orientation orientation,
                                        int role) const {
  if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
    return QAbstractTableModel::headerData(section, orientation, role);
  }

  switch (section) {
    case kNameColumn:
      return z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(1004));
    case kSizeColumn:
      return z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(1007));
    case kPackedSizeColumn:
      return z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(1008));
    case kModifiedColumn:
      return z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(1012));
    case kCreatedColumn:
      return z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(1010));
    case kAccessedColumn:
      return z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(1011));
    case kAttributesColumn:
      return z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(1009));
    case kEncryptedColumn:
      return z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(1015));
    case kCommentColumn:
      return z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(1028));
    case kCrcColumn:
      return z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(1019));
    case kMethodColumn:
      return z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(1022));
    case kCharactsColumn:
      return z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(1047));
    case kHostOsColumn:
      return z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(1023));
    case kVersionColumn:
      return z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(1033));
    case kVolumeIndexColumn:
      return z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(1090));
    case kOffsetColumn:
      return z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(1036));
    case kFoldersColumn:
      return z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(1031));
    case kFilesColumn:
      return z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(1032));
    default:
      return QVariant();
  }
}

}  // namespace z7::ui::filemanager
