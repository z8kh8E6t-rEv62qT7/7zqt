// src/ui/filemanager/src/main_window/model/directory_list_model_state.cpp
// Role: State transitions and public behavior for directory list model.

#include "model.h"

#include <algorithm>

#include "common/archive_type_normalization.h"
#include "archive_suffix_catalog.h"
#include "structured_list_proxy.h"

namespace z7::ui::filemanager {
namespace {

QString archive_icon_name_for_suffix(const QString& suffix) {
  const QString normalized = suffix.trimmed().toLower();
  if (normalized == QStringLiteral("7z")) {
    return QStringLiteral("7z");
  }
  if (normalized == QStringLiteral("zip")) {
    return QStringLiteral("zip");
  }
  if (normalized == QStringLiteral("rar")) {
    return QStringLiteral("rar");
  }
  if (normalized == QStringLiteral("tar")) {
    return QStringLiteral("tar");
  }
  if (z7::common::is_gzip_family_archive_suffix(normalized.toStdString())) {
    return QString::fromStdString(
        z7::common::preferred_archive_output_suffix_copy("gzip"));
  }
  if (z7::common::is_bzip2_family_archive_suffix(normalized.toStdString())) {
    return QString::fromStdString(
        z7::common::preferred_archive_output_suffix_copy("bzip2"));
  }
  if (normalized == QStringLiteral("xz") ||
      normalized == QStringLiteral("txz")) {
    return QStringLiteral("xz");
  }
  if (normalized == QStringLiteral("zst") ||
      normalized == QStringLiteral("tzst")) {
    return QStringLiteral("zst");
  }
  if (normalized == QStringLiteral("cab")) {
    return QStringLiteral("cab");
  }
  if (normalized == QStringLiteral("iso")) {
    return QStringLiteral("iso");
  }
  if (normalized == QStringLiteral("wim") ||
      normalized == QStringLiteral("swm") ||
      normalized == QStringLiteral("esd")) {
    return QStringLiteral("wim");
  }
  if (normalized == QStringLiteral("arj")) {
    return QStringLiteral("arj");
  }
  if (normalized == QStringLiteral("cpio")) {
    return QStringLiteral("cpio");
  }
  if (normalized == QStringLiteral("lzma")) {
    return QStringLiteral("lzma");
  }
  if (normalized == QStringLiteral("z") ||
      normalized == QStringLiteral("taz")) {
    return QStringLiteral("z");
  }
  if (normalized == QStringLiteral("lzh") ||
      normalized == QStringLiteral("lha")) {
    return QStringLiteral("lzh");
  }
  if (normalized == QStringLiteral("deb")) {
    return QStringLiteral("deb");
  }
  if (normalized == QStringLiteral("rpm")) {
    return QStringLiteral("rpm");
  }
  if (normalized == QStringLiteral("dmg")) {
    return QStringLiteral("dmg");
  }
  if (normalized == QStringLiteral("hfs")) {
    return QStringLiteral("hfs");
  }
  if (normalized == QStringLiteral("apfs")) {
    return QStringLiteral("apfs");
  }
  if (normalized == QStringLiteral("fat")) {
    return QStringLiteral("fat");
  }
  if (normalized == QStringLiteral("ntfs")) {
    return QStringLiteral("ntfs");
  }
  if (normalized == QStringLiteral("squashfs")) {
    return QStringLiteral("squashfs");
  }
  if (normalized == QStringLiteral("xar") ||
      normalized == QStringLiteral("pkg")) {
    return QStringLiteral("xar");
  }
  if (normalized == QStringLiteral("vhd") ||
      normalized == QStringLiteral("vhdx")) {
    return QStringLiteral("vhd");
  }
  return QString();
}

}  // namespace

void DirectoryListModel::set_directory(const QString& directory) {
  mode_ = DataMode::kFilesystem;
  clear_archive_drag_source();
  directory_ = QDir(directory).absolutePath();
  reload();
}

void DirectoryListModel::set_virtual_entries(
    const QString& virtual_dir,
    const QVector<VirtualEntry>& entries) {
  beginResetModel();
  mode_ = DataMode::kVirtualArchive;
  directory_ = virtual_dir;
  entries_.clear();
  next_load_index_ = 0;
  entries_.reserve(entries.size());
  for (const VirtualEntry& virtual_entry : entries) {
    entries_.push_back(make_virtual_entry(virtual_entry));
  }
  endResetModel();
}

void DirectoryListModel::set_archive_drag_source(const QString& archive_path,
                                                 const QString& archive_type_hint,
                                                 quint64 session_token_value) {
  archive_drag_source_archive_ = QDir::fromNativeSeparators(archive_path.trimmed());
  archive_drag_type_hint_ = archive_type_hint.trimmed();
  archive_drag_source_session_token_ = session_token_value;
}

void DirectoryListModel::clear_archive_drag_source() {
  archive_drag_source_archive_.clear();
  archive_drag_type_hint_.clear();
  archive_drag_source_session_token_ = 0;
}

void DirectoryListModel::set_archive_drag_materializer(
    ArchiveDragMaterializer materializer) {
  archive_drag_materializer_ = std::move(materializer);
}

void DirectoryListModel::set_rename_handler(RenameHandler handler) {
  rename_handler_ = std::move(handler);
}

void DirectoryListModel::set_rename_enabled(bool enabled) {
  if (rename_enabled_ == enabled) {
    return;
  }
  rename_enabled_ = enabled;
  if (!entries_.isEmpty()) {
    emit dataChanged(index(0, kNameColumn),
                     index(entries_.size() - 1, kNameColumn),
                     {Qt::EditRole});
  }
}

QString DirectoryListModel::directory() const {
  return directory_;
}

DirectoryListModel::DataMode DirectoryListModel::data_mode() const {
  return mode_;
}

bool DirectoryListModel::is_virtual_mode() const {
  return mode_ == DataMode::kVirtualArchive;
}

QString DirectoryListModel::path_for_row(int row) const {
  if (row < 0 || row >= entries_.size()) {
    return QString();
  }
  return entries_[row].path;
}

bool DirectoryListModel::is_dir_for_row(int row) const {
  if (row < 0 || row >= entries_.size()) {
    return false;
  }
  return entries_[row].is_dir;
}

bool DirectoryListModel::is_parent_link_for_row(int row) const {
  if (row < 0 || row >= entries_.size()) {
    return false;
  }
  return entries_[row].is_parent_link;
}

bool DirectoryListModel::set_filesystem_directory_stats(
    const QString& path,
    uint64_t size,
    uint64_t num_sub_dirs,
    uint64_t num_sub_files) {
  if (mode_ != DataMode::kFilesystem) {
    return false;
  }

  const QString normalized = QFileInfo(path).absoluteFilePath();
  if (normalized.isEmpty()) {
    return false;
  }

  for (int row = 0; row < entries_.size(); ++row) {
    Entry& entry = entries_[row];
    if (entry.is_parent_link || !entry.is_dir ||
        QFileInfo(entry.path).absoluteFilePath() != normalized) {
      continue;
    }

    entry.size = size;
    entry.num_sub_dirs = num_sub_dirs;
    entry.num_sub_files = num_sub_files;
    entry.directory_size_defined = true;
    emit dataChanged(index(row, kSizeColumn),
                     index(row, kFilesColumn),
                     {Qt::DisplayRole,
                      z7::ui::widgets::StructuredListSortFilterProxy::kSortKeyRole});
    return true;
  }
  return false;
}

void DirectoryListModel::set_show_dots(bool enabled) {
  if (show_dots_ == enabled) {
    return;
  }
  show_dots_ = enabled;
  reload();
}

bool DirectoryListModel::show_dots() const {
  return show_dots_;
}

void DirectoryListModel::set_show_real_file_icons(bool enabled) {
  if (show_real_file_icons_ == enabled) {
    return;
  }
  show_real_file_icons_ = enabled;
  if (!entries_.isEmpty()) {
    emit dataChanged(index(0, kNameColumn),
                     index(entries_.size() - 1, kNameColumn),
                     {Qt::DecorationRole});
  }
}

bool DirectoryListModel::show_real_file_icons() const {
  return show_real_file_icons_;
}

bool DirectoryListModel::flat_view() const {
  return flat_view_;
}

void DirectoryListModel::set_flat_view(bool enabled) {
  if (flat_view_ == enabled) {
    return;
  }
  flat_view_ = enabled;
  reload();
}

void DirectoryListModel::set_timestamp_display(int timestamp_level,
                                               bool show_utc) {
  const int normalized_level = normalize_timestamp_level(timestamp_level);
  if (timestamp_level_ == normalized_level && timestamp_show_utc_ == show_utc) {
    return;
  }
  timestamp_level_ = normalized_level;
  timestamp_show_utc_ = show_utc;
  if (!entries_.isEmpty()) {
    emit dataChanged(index(0, kModifiedColumn),
                     index(entries_.size() - 1, kAccessedColumn));
  }
}

int DirectoryListModel::timestamp_level() const {
  return timestamp_level_;
}

bool DirectoryListModel::timestamp_show_utc() const {
  return timestamp_show_utc_;
}

void DirectoryListModel::reload() {
  if (mode_ != DataMode::kFilesystem) {
    return;
  }
  beginResetModel();
  entries_.clear();
  next_load_index_ = 0;

  QDir dir(directory_);
  const std::map<std::string, std::string> comments_by_key =
      load_filesystem_comments_for_directory(dir);
  if (flat_view_) {
    append_entries_recursive(dir, dir);
  } else {
    if (show_dots_) {
      entries_.push_back(make_parent_entry(dir));
    }
    const QFileInfoList list = dir.entryInfoList(
        QDir::NoDotAndDotDot | QDir::AllEntries | QDir::Hidden | QDir::System,
        QDir::NoSort);

    entries_.reserve(list.size());
    for (const QFileInfo& info : list) {
      Entry entry = make_filesystem_entry(info, info.fileName());
      if (const std::optional<QString> comment =
              filesystem_comment_for_entry(comments_by_key,
                                           dir,
                                           info,
                                           info.fileName());
          comment.has_value()) {
        entry.comment = *comment;
      }
      entries_.push_back(std::move(entry));
    }
  }

  if (flat_view_) {
    for (Entry& entry : entries_) {
      if (const std::optional<QString> comment =
              filesystem_comment_for_entry(comments_by_key,
                                           dir,
                                           entry.info,
                                           entry.display_name);
          comment.has_value()) {
        entry.comment = *comment;
      }
    }
  }

  endResetModel();
}

void DirectoryListModel::notify_language_changed() {
  emit headerDataChanged(Qt::Horizontal, 0, kColumnCount - 1);
}

int DirectoryListModel::normalize_timestamp_level(int timestamp_level) {
  switch (timestamp_level) {
    case kTimestampPrintLevelDay:
    case kTimestampPrintLevelMin:
    case kTimestampPrintLevelSec:
    case kTimestampPrintLevelNtfs:
    case kTimestampPrintLevelNs:
      return timestamp_level;
    default:
      return kTimestampPrintLevelMin;
  }
}

bool DirectoryListModel::icon_has_renderable_pixels(const QIcon& icon) {
  if (icon.isNull()) {
    return false;
  }
  return !icon.pixmap(16, 16).isNull();
}

bool DirectoryListModel::is_numeric_split_extension(const QString& extension) {
  if (extension.isEmpty()) {
    return false;
  }
  for (const QChar ch : extension) {
    if (!ch.isDigit()) {
      return false;
    }
  }
  return true;
}

const QHash<QString, QString>& DirectoryListModel::archive_icon_name_by_extension() {
  static const QHash<QString, QString> kIconNames = [] {
    QHash<QString, QString> out;
    out.insert(QStringLiteral("split"), QStringLiteral("split"));
    for (const QString& suffix : z7::ui::common::ordered_archive_suffixes()) {
      const QString icon_name = archive_icon_name_for_suffix(suffix);
      if (!icon_name.isEmpty()) {
        out.insert(suffix, icon_name);
      }
    }
    return out;
  }();
  return kIconNames;
}

QString DirectoryListModel::extension_for_icon_lookup(const QString& display_name) {
  if (display_name.isEmpty()) {
    return QString();
  }

  QString file_name = display_name;
  const int separator_pos = std::max(file_name.lastIndexOf(QLatin1Char('/')),
                                     file_name.lastIndexOf(QLatin1Char('\\')));
  if (separator_pos >= 0 && separator_pos + 1 < file_name.size()) {
    file_name = file_name.mid(separator_pos + 1);
  }

  const int dot_pos = file_name.lastIndexOf(QLatin1Char('.'));
  if (dot_pos <= 0 || dot_pos + 1 >= file_name.size()) {
    return QString();
  }

  return file_name.mid(dot_pos + 1).toLower();
}

}  // namespace z7::ui::filemanager
