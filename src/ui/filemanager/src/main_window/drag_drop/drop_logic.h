// src/ui/filemanager/src/main_window/drag_drop/drop_logic.h
// Role: Shared drop decision helpers used by main-window handlers.

#pragma once

#include <QString>
#include <QStringList>
#include <QVariant>
#include <Qt>

class QAbstractItemView;
class QDropEvent;
class QMimeData;
class QObject;

namespace z7::ui::filemanager {

    class DirectoryListModel;
    class MainWindow;

    enum class DropCommand {
        kCancel = 0,
        kCopy,
        kMove,
        kCopyToArchive,
        kAddToArchive
    };

    enum class SourceTargetVolumeRelation {
        kUnknown = 0,
        kSame,
        kDifferent,
        kMixed
    };

#ifdef Z7_TESTING
    enum class DropCopyToArchiveConfirmOverride {
        kDialog = 0,
        kYes,
        kNo,
        kCancel
    };
#endif

    struct DropTargetInfo {
        QString directory;
        QString archive_virtual_dir;
        bool allow_copy_move = false;
        bool archive_directory_row_target = false;
    };

    QStringList local_existing_drop_paths(QMimeData const* mime_data);
    QString add_caption();

#ifdef Z7_TESTING
    DropCopyToArchiveConfirmOverride parse_copy_to_archive_confirm_override(QVariant const& value);
    bool parse_volume_relation_override(QVariant const& value, SourceTargetVolumeRelation* relation);
#endif
    SourceTargetVolumeRelation source_target_volume_relation(QStringList const& source_paths,
                                                             QString const& target_directory);
    bool source_target_all_on_same_volume(SourceTargetVolumeRelation relation);

    Qt::DropAction drop_action_for_command(DropCommand command);
    Qt::DropAction reported_drop_action_for_source(DropCommand command, bool trusted_internal_fs_source);

    DropCommand choose_drop_command(MainWindow* window,
                                    bool in_archive_view,
                                    bool allow_copy_move,
                                    bool app_target,
                                    bool internal_fs_source,
                                    bool internal_archive_source,
                                    bool source_target_same_volume,
                                    QObject const* watched,
                                    QDropEvent const* event);
    DropCommand choose_drop_preview_command(MainWindow* window,
                                            bool in_archive_view,
                                            bool allow_copy_move,
                                            bool app_target,
                                            bool internal_fs_source,
                                            bool internal_archive_source,
                                            bool source_target_same_volume,
                                            QDropEvent const* event);

} // namespace z7::ui::filemanager
