// src/ui/filemanager/src/main_window/dialogs/archive_add_sources_dialog.h
// Role: Archive-view dialog for selecting filesystem sources to add.

#pragma once

#include <QString>
#include <QStringList>

class QWidget;

namespace z7::ui::filemanager {

    struct ArchiveAddSourcesDialogResult {
        bool accepted = false;
        QStringList selected_paths;
    };

    ArchiveAddSourcesDialogResult show_archive_add_sources_dialog(QWidget* parent,
                                                                  QString const& title,
                                                                  QString const& initial_directory,
                                                                  QString const& target_virtual_dir_display);

} // namespace z7::ui::filemanager
