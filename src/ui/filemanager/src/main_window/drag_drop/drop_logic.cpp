// src/ui/filemanager/src/main_window/drag_drop/drop_logic.cpp
// Role: Drop helper primitives shared across drag/drop handlers.

#include "drop_logic.h"

#include "main_window/deps.h"
#include "main_window/internal.h"

namespace z7::ui::filemanager {

    QStringList local_existing_drop_paths(QMimeData const* mime_data) {
        if (mime_data == nullptr || !mime_data->hasUrls()) {
            return {};
        }

        QStringList paths;
        QSet<QString> dedup;
        QList<QUrl> const urls = mime_data->urls();
        paths.reserve(urls.size());
        for (QUrl const& url : urls) {
            if (!url.isLocalFile()) {
                continue;
            }

            QFileInfo const info(url.toLocalFile());
            if (!info.exists()) {
                continue;
            }

            QString const absolute = info.absoluteFilePath();
            if (absolute.isEmpty() || dedup.contains(absolute)) {
                continue;
            }
            dedup.insert(absolute);
            paths << absolute;
        }
        return paths;
    }

    QString add_caption() {
        return z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(7200));
    }

#ifdef Z7_TESTING
    DropCopyToArchiveConfirmOverride parse_copy_to_archive_confirm_override(QVariant const& value) {
        if (!value.isValid()) {
            return DropCopyToArchiveConfirmOverride::kDialog;
        }

        QString const key = value.toString().trimmed().toLower();
        if (key == QStringLiteral("yes")) {
            return DropCopyToArchiveConfirmOverride::kYes;
        }
        if (key == QStringLiteral("no")) {
            return DropCopyToArchiveConfirmOverride::kNo;
        }
        if (key == QStringLiteral("cancel")) {
            return DropCopyToArchiveConfirmOverride::kCancel;
        }
        return DropCopyToArchiveConfirmOverride::kDialog;
    }

    bool parse_volume_relation_override(QVariant const& value, SourceTargetVolumeRelation* relation) {
        if (relation == nullptr || !value.isValid()) {
            return false;
        }

        QString const key = value.toString().trimmed().toLower();
        if (key == QStringLiteral("same")) {
            *relation = SourceTargetVolumeRelation::kSame;
            return true;
        }
        if (key == QStringLiteral("different")) {
            *relation = SourceTargetVolumeRelation::kDifferent;
            return true;
        }
        if (key == QStringLiteral("mixed")) {
            *relation = SourceTargetVolumeRelation::kMixed;
            return true;
        }
        if (key == QStringLiteral("unknown")) {
            *relation = SourceTargetVolumeRelation::kUnknown;
            return true;
        }
        return false;
    }
#endif

    SourceTargetVolumeRelation source_target_volume_relation(QStringList const& source_paths,
                                                             QString const& target_directory) {
        if (source_paths.isEmpty()) {
            return SourceTargetVolumeRelation::kUnknown;
        }

        QString const target_dir = target_directory.trimmed();
        if (target_dir.isEmpty()) {
            return SourceTargetVolumeRelation::kUnknown;
        }

        QFileInfo const target_info(target_dir);
        if (!target_info.exists() || !target_info.isDir()) {
            return SourceTargetVolumeRelation::kUnknown;
        }

        QStorageInfo const target_storage(target_info.absoluteFilePath());
        if (!target_storage.isValid() || !target_storage.isReady()) {
            return SourceTargetVolumeRelation::kUnknown;
        }

        QByteArray const target_device = target_storage.device();
        QString const target_root = target_storage.rootPath();
        bool saw_same = false;
        bool saw_different = false;
        for (QString const& source_path : source_paths) {
            QFileInfo const source_info(source_path);
            if (!source_info.exists()) {
                return SourceTargetVolumeRelation::kUnknown;
            }

            QStorageInfo const source_storage(source_info.absoluteFilePath());
            if (!source_storage.isValid() || !source_storage.isReady()) {
                return SourceTargetVolumeRelation::kUnknown;
            }

            bool same_volume = false;
            if (!target_device.isEmpty() && !source_storage.device().isEmpty()) {
                same_volume = source_storage.device() == target_device;
            } else {
                same_volume = source_storage.rootPath() == target_root;
            }

            if (same_volume) {
                saw_same = true;
            } else {
                saw_different = true;
            }
            if (saw_same && saw_different) {
                return SourceTargetVolumeRelation::kMixed;
            }
        }
        if (saw_same) {
            return SourceTargetVolumeRelation::kSame;
        }
        if (saw_different) {
            return SourceTargetVolumeRelation::kDifferent;
        }
        return SourceTargetVolumeRelation::kUnknown;
    }

    bool source_target_all_on_same_volume(SourceTargetVolumeRelation relation) {
        return relation == SourceTargetVolumeRelation::kSame;
    }

    Qt::DropAction drop_action_for_command(DropCommand command) {
        switch (command) {
            case DropCommand::kMove:
                return Qt::MoveAction;
            case DropCommand::kCopy:
            case DropCommand::kCopyToArchive:
            case DropCommand::kAddToArchive:
                return Qt::CopyAction;
            case DropCommand::kCancel:
            default:
                return Qt::IgnoreAction;
        }
    }

    Qt::DropAction reported_drop_action_for_source(DropCommand command, bool trusted_internal_fs_source) {
        // Original PanelDrag::Drop() keeps move semantics internal to 7-Zip, but avoids
        // returning move effect to external sources to prevent source-side duplicate delete.
        if (command == DropCommand::kMove && !trusted_internal_fs_source) {
            return Qt::CopyAction;
        }
        return drop_action_for_command(command);
    }

} // namespace z7::ui::filemanager
