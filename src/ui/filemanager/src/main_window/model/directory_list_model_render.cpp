// src/ui/filemanager/src/main_window/model/directory_list_model_render.cpp
// Role: Icon resolution, entry construction, sorting, and time formatting.

#include "model.h"

#include <QApplication>
#include <QStyle>
#include <QTimeZone>

#include "archive_string_codec_qt.h"
#include "descript_ion_store.h"

namespace z7::ui::filemanager {

QIcon DirectoryListModel::mapped_archive_icon(const QString& extension) const {
  QString key = extension.trimmed().toLower();
  if (key.isEmpty()) {
    return QIcon();
  }
  if (is_numeric_split_extension(key)) {
    key = QStringLiteral("split");
  }

  const auto cached = archive_icon_cache_.constFind(key);
  if (cached != archive_icon_cache_.cend()) {
    return *cached;
  }

  const auto& icon_names = archive_icon_name_by_extension();
  const auto icon_name_it = icon_names.constFind(key);
  if (icon_name_it == icon_names.cend()) {
    archive_icon_cache_.insert(key, QIcon());
    return QIcon();
  }

  const QIcon icon(QStringLiteral(":/z7/archive-icons/%1.ico").arg(*icon_name_it));
  archive_icon_cache_.insert(key, icon);
  return icon;
}

QIcon DirectoryListModel::standard_style_icon(bool is_dir) const {
  const QStyle::StandardPixmap pixmap =
      is_dir ? QStyle::SP_DirIcon : QStyle::SP_FileIcon;
  if (icon_style_context_ != nullptr && icon_style_context_->style() != nullptr) {
    return icon_style_context_->style()->standardIcon(
        pixmap, nullptr, icon_style_context_);
  }
  if (QApplication::style() != nullptr) {
    return QApplication::style()->standardIcon(pixmap);
  }
  return QIcon();
}

QIcon DirectoryListModel::resolve_entry_icon(const Entry& entry) const {
  if (show_real_file_icons_ && !entry.is_virtual) {
    const QIcon real_icon = icon_provider_.icon(entry.info);
    if (icon_has_renderable_pixels(real_icon)) {
      return real_icon;
    }
  }

  if (!entry.is_dir) {
    QString icon_extension;
    if (entry.is_virtual) {
      icon_extension = extension_for_icon_lookup(entry.display_name);
    } else {
      icon_extension = extension_for_icon_lookup(entry.info.fileName());
    }
    const QIcon mapped_icon = mapped_archive_icon(icon_extension);
    if (icon_has_renderable_pixels(mapped_icon)) {
      return mapped_icon;
    }
  }

  const QIcon generic_icon = icon_provider_.icon(
      entry.is_dir ? QFileIconProvider::Folder : QFileIconProvider::File);
  if (icon_has_renderable_pixels(generic_icon)) {
    return generic_icon;
  }

  const QIcon style_icon = standard_style_icon(entry.is_dir);
  if (icon_has_renderable_pixels(style_icon)) {
    return style_icon;
  }

  return generic_icon;
}

DirectoryListModel::Entry DirectoryListModel::make_filesystem_entry(
    const QFileInfo& info,
    const QString& display_name) {
  Entry entry;
  entry.info = info;
  entry.path = info.absoluteFilePath();
  entry.display_name = display_name;
  entry.is_virtual = false;
  entry.is_dir = info.isDir();
  entry.is_parent_link = false;
  entry.size = static_cast<uint64_t>(info.size());
  entry.load_index = next_load_index_++;
  return entry;
}

std::map<std::string, std::string> DirectoryListModel::load_filesystem_comments_for_directory(
    const QDir& root) const {
  z7::app::DescriptIonDocument document;
  std::string error_message;
  if (!z7::app::load_descript_ion_document(
          z7::ui::archive_support::to_native_string(root.absolutePath()),
          &document,
          &error_message)) {
    return {};
  }

  std::map<std::string, std::string> out;
  for (const auto& [id, value] : document.entries) {
    const std::optional<std::string> display_value =
        z7::app::read_descript_ion_comment_for_display(document, id);
    if (!display_value.has_value()) {
      continue;
    }
    out.emplace(id, *display_value);
  }
  return out;
}

std::optional<QString> DirectoryListModel::filesystem_comment_for_entry(
    const std::map<std::string, std::string>& comments_by_key,
    const QDir&,
    const QFileInfo& info,
    const QString& display_name) const {
  QString key = display_name;
  if (!flat_view_) {
    key = info.fileName();
  }
  key = QDir::fromNativeSeparators(key.trimmed());
  if (key.isEmpty()) {
    return std::nullopt;
  }

  const auto it =
      comments_by_key.find(z7::ui::archive_support::to_native_string(key));
  if (it == comments_by_key.end()) {
    return std::nullopt;
  }
  return z7::ui::archive_support::from_native_string(it->second);
}

DirectoryListModel::Entry DirectoryListModel::make_parent_entry(const QDir& dir) {
  Entry entry;
  QDir parent_dir(dir);
  if (!parent_dir.cdUp()) {
    parent_dir = dir;
  }
  entry.path = parent_dir.absolutePath();
  entry.display_name = QStringLiteral("..");
  entry.is_virtual = true;
  entry.is_dir = true;
  entry.is_parent_link = true;
  entry.size = 0;
  entry.load_index = next_load_index_++;
  return entry;
}

DirectoryListModel::Entry DirectoryListModel::make_virtual_entry(
    const VirtualEntry& virtual_entry) {
  Entry entry;
  entry.path = virtual_entry.path;
  entry.display_name = virtual_entry.display_name;
  entry.is_virtual = true;
  entry.is_dir = virtual_entry.is_dir;
  entry.is_parent_link = virtual_entry.is_parent_link;
  entry.size = virtual_entry.size;
  entry.packed_size = virtual_entry.packed_size;
  entry.mtime_msecs_utc = virtual_entry.mtime_msecs_utc;
  entry.ctime_msecs_utc = virtual_entry.ctime_msecs_utc;
  entry.atime_msecs_utc = virtual_entry.atime_msecs_utc;
  entry.attributes = virtual_entry.attributes;
  entry.encrypted = virtual_entry.encrypted;
  entry.comment = virtual_entry.comment;
  entry.crc = virtual_entry.crc;
  entry.method = virtual_entry.method;
  entry.characts = virtual_entry.characts;
  entry.host_os = virtual_entry.host_os;
  entry.version = virtual_entry.version;
  entry.volume_index = virtual_entry.volume_index;
  entry.offset = virtual_entry.offset;
  entry.num_sub_dirs = virtual_entry.num_sub_dirs;
  entry.num_sub_files = virtual_entry.num_sub_files;
  entry.load_index = next_load_index_++;
  return entry;
}

void DirectoryListModel::append_entries_recursive(const QDir& root, const QDir& dir) {
  const QFileInfoList list = dir.entryInfoList(
      QDir::NoDotAndDotDot | QDir::AllEntries | QDir::Hidden | QDir::System,
      QDir::NoSort);

  for (const QFileInfo& info : list) {
    QString display_name = root.relativeFilePath(info.absoluteFilePath());
    display_name = QDir::toNativeSeparators(display_name);
    entries_.push_back(make_filesystem_entry(info, display_name));
    if (info.isDir() && !info.isSymLink()) {
      append_entries_recursive(root, QDir(info.absoluteFilePath()));
    }
  }
}

QString DirectoryListModel::format_timestamp_for_entry(
    const std::optional<int64_t>& msecs_utc,
    const QDateTime& source_time) const {
  if (msecs_utc.has_value()) {
    const QDateTime utc =
        QDateTime::fromMSecsSinceEpoch(*msecs_utc, QTimeZone::UTC);
    if (utc.isValid()) {
      return format_timestamp(utc);
    }
  }
  return format_timestamp(source_time);
}

QString DirectoryListModel::format_timestamp(const QDateTime& input) const {
  if (!input.isValid()) {
    return QString();
  }

  QDateTime value = timestamp_show_utc_ ? input.toUTC() : input.toLocalTime();
  if (!value.isValid()) {
    value = input;
  }

  const QString suffix = timestamp_show_utc_ ? QStringLiteral("Z") : QString();
  switch (timestamp_level_) {
    case kTimestampPrintLevelDay:
      return value.toString(QStringLiteral("yyyy-MM-dd")) + suffix;
    case kTimestampPrintLevelMin:
      return value.toString(QStringLiteral("yyyy-MM-dd HH:mm")) + suffix;
    case kTimestampPrintLevelSec:
      return value.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")) + suffix;
    case kTimestampPrintLevelNtfs:
    case kTimestampPrintLevelNs: {
      const int digits = (timestamp_level_ == kTimestampPrintLevelNtfs) ? 7 : 9;
      QString frac = QString::number(value.time().msec()).rightJustified(
          3, QLatin1Char('0'));
      while (frac.size() < digits) {
        frac += QLatin1Char('0');
      }
      if (frac.size() > digits) {
        frac = frac.left(digits);
      }
      return value.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")) +
             QStringLiteral(".") + frac + suffix;
    }
    default:
      return value.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")) + suffix;
  }
}

}  // namespace z7::ui::filemanager
