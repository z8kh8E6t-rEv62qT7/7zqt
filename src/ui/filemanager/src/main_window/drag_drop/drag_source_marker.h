// src/ui/filemanager/src/main_window/drag_drop/drag_source_marker.h
// Role: Internal drag-source marker encode/decode and trust checks.

#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>

class QMimeData;

namespace z7::ui::filemanager {

    inline constexpr char const* kMimeTypeZ7FmFsSource = "application/x-z7-filemanager-fs-source";
    inline constexpr char const* kMimeTypeZ7FmArchiveSource = "application/x-z7-filemanager-archive-source";
    inline constexpr char const* kMimeTypeZ7FmArchiveTransferRequested =
        "application/x-z7-filemanager-archive-transfer-requested";
    inline constexpr char const* kMimeTypeZ7FmArchiveMaterializationError =
        "application/x-z7-filemanager-archive-materialization-error";
    inline constexpr char const* kMimeTypeZ7FmArchiveInternalDropHandled =
        "application/x-z7-filemanager-archive-internal-drop-handled";
    inline constexpr char const* kDragSourceInstanceIdProperty = "z7.fm.drag.source.instance_id";

    struct InternalArchiveSourcePayload {
        QString archive_path;
        QString archive_type_hint;
        QStringList entries;
        quint64 session_token_value = 0;
    };

    QString drag_source_instance_id();
    QByteArray make_internal_fs_source_marker();
    QByteArray make_internal_archive_source_marker(QString const& archive_path,
                                                   QString const& archive_type_hint,
                                                   QStringList const& entries,
                                                   quint64 session_token_value = 0);
    bool has_internal_fs_source_marker(QMimeData const* mime_data);
    bool is_trusted_internal_fs_source_marker(QMimeData const* mime_data);
    bool has_internal_archive_source_marker(QMimeData const* mime_data);
    bool read_internal_archive_source_marker(QMimeData const* mime_data,
                                             InternalArchiveSourcePayload* payload,
                                             bool* trusted);

} // namespace z7::ui::filemanager
