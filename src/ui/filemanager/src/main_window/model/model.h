// src/ui/filemanager/src/main_window/model/model.h
// Role: Declarations for main-window directory list model.

#pragma once

#include <QAbstractTableModel>
#include <QDateTime>
#include <QDir>
#include <QFileIconProvider>
#include <QFileInfo>
#include <QHash>
#include <QIcon>
#include <QMimeData>
#include <QVector>
#include <QWidget>

#include <cstdint>
#include <functional>
#include <map>
#include <optional>

namespace z7::ui::filemanager {

class DirectoryListModel final : public QAbstractTableModel {
 public:
  using ArchiveDragMaterializedCallback =
      std::function<void(const QStringList& paths, const QString& error_message)>;
  using ArchiveDragMaterializer =
      std::function<void(const QStringList& entries,
                         ArchiveDragMaterializedCallback finished_cb)>;
  using RenameHandler =
      std::function<bool(const QString& item_path,
                         const QString& new_name,
                         bool entry_is_dir)>;

  struct VirtualEntry {
    QString path;
    QString display_name;
    bool is_dir = false;
    bool is_parent_link = false;
    uint64_t size = 0;
    std::optional<uint64_t> packed_size;
    std::optional<int64_t> mtime_msecs_utc;
    std::optional<int64_t> ctime_msecs_utc;
    std::optional<int64_t> atime_msecs_utc;
    QString attributes;
    std::optional<bool> encrypted;
    QString comment;
    std::optional<uint32_t> crc;
    QString method;
    QString characts;
    QString host_os;
    QString version;
    std::optional<uint64_t> volume_index;
    std::optional<uint64_t> offset;
    std::optional<uint64_t> num_sub_dirs;
    std::optional<uint64_t> num_sub_files;
  };

  enum Column {
    kNameColumn = 0,
    kSizeColumn,
    kPackedSizeColumn,
    kModifiedColumn,
    kCreatedColumn,
    kAccessedColumn,
    kAttributesColumn,
    kEncryptedColumn,
    kCommentColumn,
    kCrcColumn,
    kMethodColumn,
    kCharactsColumn,
    kHostOsColumn,
    kVersionColumn,
    kVolumeIndexColumn,
    kOffsetColumn,
    kFoldersColumn,
    kFilesColumn,
    kTypeSortColumn,
    kColumnCount
  };

  static constexpr int kTimestampPrintLevelDay = -3;
  static constexpr int kTimestampPrintLevelMin = -1;
  static constexpr int kTimestampPrintLevelSec = 0;
  static constexpr int kTimestampPrintLevelNtfs = 7;
  static constexpr int kTimestampPrintLevelNs = 9;

  enum class DataMode {
    kFilesystem,
    kVirtualArchive
  };

  explicit DirectoryListModel(QObject* parent = nullptr);

  void set_icon_style_context(QWidget* context);

  int rowCount(const QModelIndex& parent = QModelIndex()) const override;
  int columnCount(const QModelIndex& parent = QModelIndex()) const override;
  Qt::ItemFlags flags(const QModelIndex& index) const override;
  QStringList mimeTypes() const override;
  QMimeData* mimeData(const QModelIndexList& indexes) const override;
  Qt::DropActions supportedDragActions() const override;
  QVariant data(const QModelIndex& index, int role) const override;
  bool setData(const QModelIndex& index,
               const QVariant& value,
               int role = Qt::EditRole) override;
  QVariant headerData(int section,
                      Qt::Orientation orientation,
                      int role) const override;

  void set_directory(const QString& directory);
  void set_virtual_entries(const QString& virtual_dir,
                           const QVector<VirtualEntry>& entries);
  void set_archive_drag_source(const QString& archive_path,
                               const QString& archive_type_hint,
                               quint64 session_token_value = 0);
  void clear_archive_drag_source();
  void set_archive_drag_materializer(ArchiveDragMaterializer materializer);
  void set_rename_handler(RenameHandler handler);
  void set_rename_enabled(bool enabled);

  QString directory() const;
  DataMode data_mode() const;
  bool is_virtual_mode() const;

  QString path_for_row(int row) const;
  bool is_dir_for_row(int row) const;
  bool is_parent_link_for_row(int row) const;
  bool set_filesystem_directory_stats(const QString& path,
                                      uint64_t size,
                                      uint64_t num_sub_dirs,
                                      uint64_t num_sub_files);

  void set_show_dots(bool enabled);
  bool show_dots() const;

  void set_show_real_file_icons(bool enabled);
  bool show_real_file_icons() const;

  bool flat_view() const;
  void set_flat_view(bool enabled);

  void set_timestamp_display(int timestamp_level, bool show_utc);
  int timestamp_level() const;
  bool timestamp_show_utc() const;

  void reload();

  void notify_language_changed();

 private:
  struct Entry {
    QFileInfo info;
    QString path;
    QString display_name;
    bool is_virtual = false;
    bool is_dir = false;
    bool is_parent_link = false;
    bool directory_size_defined = false;
    uint64_t size = 0;
    std::optional<uint64_t> packed_size;
    std::optional<int64_t> mtime_msecs_utc;
    std::optional<int64_t> ctime_msecs_utc;
    std::optional<int64_t> atime_msecs_utc;
    QString attributes;
    std::optional<bool> encrypted;
    QString comment;
    std::optional<uint32_t> crc;
    QString method;
    QString characts;
    QString host_os;
    QString version;
    std::optional<uint64_t> volume_index;
    std::optional<uint64_t> offset;
    std::optional<uint64_t> num_sub_dirs;
    std::optional<uint64_t> num_sub_files;
    int load_index = 0;
  };

  static int normalize_timestamp_level(int timestamp_level);

  static bool icon_has_renderable_pixels(const QIcon& icon);
  static bool is_numeric_split_extension(const QString& extension);
  static const QHash<QString, QString>& archive_icon_name_by_extension();
  static QString extension_for_icon_lookup(const QString& display_name);

  QIcon mapped_archive_icon(const QString& extension) const;
  QIcon standard_style_icon(bool is_dir) const;
  QIcon resolve_entry_icon(const Entry& entry) const;

  Entry make_filesystem_entry(const QFileInfo& info, const QString& display_name);
  Entry make_parent_entry(const QDir& dir);
  Entry make_virtual_entry(const VirtualEntry& virtual_entry);

  void append_entries_recursive(const QDir& root, const QDir& dir);
  std::map<std::string, std::string> load_filesystem_comments_for_directory(
      const QDir& root) const;
  std::optional<QString> filesystem_comment_for_entry(
      const std::map<std::string, std::string>& comments_by_key,
      const QDir& root,
      const QFileInfo& info,
      const QString& display_name) const;

  QString format_timestamp_for_entry(const std::optional<int64_t>& msecs_utc,
                                     const QDateTime& source_time) const;
  QString format_timestamp(const QDateTime& input) const;

  QVector<Entry> entries_;
  QString directory_;
  DataMode mode_ = DataMode::kFilesystem;
  QFileIconProvider icon_provider_;
  QWidget* icon_style_context_ = nullptr;
  mutable QHash<QString, QIcon> archive_icon_cache_;
  bool flat_view_ = false;
  bool show_dots_ = false;
  bool show_real_file_icons_ = false;
  int timestamp_level_ = kTimestampPrintLevelMin;
  bool timestamp_show_utc_ = false;
  int next_load_index_ = 0;
  QString archive_drag_source_archive_;
  QString archive_drag_type_hint_;
  quint64 archive_drag_source_session_token_ = 0;
  ArchiveDragMaterializer archive_drag_materializer_;
  RenameHandler rename_handler_;
  bool rename_enabled_ = false;
};

}  // namespace z7::ui::filemanager
