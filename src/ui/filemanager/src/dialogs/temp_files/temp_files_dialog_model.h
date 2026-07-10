// src/ui/filemanager/src/temp_files_dialog_model.h
// Role: State model for temp-files dialog path, sorting, and entry list.

#pragma once

#include <QString>
#include <QVector>

#include "temp_files_listing.h"

namespace z7::ui::filemanager {

    class TempFilesDialogModel final {
    public:
        explicit TempFilesDialogModel(QString const& temp_root_path);

        void reload();
        void open_parent();
        void apply_sort_column(int column_index, int column_count);

        bool set_current_path(QString const& path);
        bool is_at_root() const;

        QString const& temp_root_path() const { return temp_root_path_; }

        QString const& current_path() const { return current_path_; }

        int sort_column() const { return sort_column_; }

        bool ascending() const { return ascending_; }

        QVector<TempFilesListEntry> const& entries() const { return entries_; }

    private:
        static QString normalized_absolute_path(QString const& path);
        bool is_path_inside_temp_root(QString const& path) const;
        void sort_entries();

        QString temp_root_path_;
        QString current_path_;
        int sort_column_ = 1;
        bool ascending_ = true;
        QVector<TempFilesListEntry> entries_;
    };

} // namespace z7::ui::filemanager
