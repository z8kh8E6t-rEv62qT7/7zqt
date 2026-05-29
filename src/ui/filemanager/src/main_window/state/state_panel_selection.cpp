// src/ui/filemanager/src/main_window/state/state_panel_selection.cpp
// Role: PanelController selection, focus, and proxy/source row bridging.

#include "main_window/deps.h"
#include "main_window/internal.h"
#include "main_window/drag_drop/drop_logic.h"

namespace z7::ui::filemanager
{

    namespace
    {

        template <typename PanelLike>
        QItemSelectionModel* selection_model_for_panel(PanelLike const& panel)
        {
            if (panel.ui.details_view == nullptr)
            {
                return nullptr;
            }
            return panel.ui.details_view->selectionModel();
        }

        // Selection helpers used throughout MainWindow. All selection reads are done on
        // the details view's proxy, filtered to the primary column, then mapped back
        // to the DirectoryListModel source rows before being handed to business logic.
        template <typename PanelLike>
        int panel_primary_column(PanelLike const& panel)
        {
            if (panel.ui.details_view == nullptr)
            {
                return DirectoryListModel::kNameColumn;
            }
            return panel.ui.details_view->primary_column();
        }

        template <typename PanelLike>
        QModelIndex map_to_source_index(PanelLike const& panel, QModelIndex const& proxy_index)
        {
            if (!proxy_index.isValid())
                return {};
            if (panel.proxy == nullptr)
                return proxy_index;
            return panel.proxy->mapToSource(proxy_index);
        }

        QPoint viewport_pos_for_watched(QAbstractItemView* view,
                                        QObject const* watched,
                                        QPoint const& watched_pos)
        {
            if (view == nullptr)
            {
                return watched_pos;
            }

            QWidget const* watched_widget = qobject_cast<QWidget const*>(watched);
            if (watched_widget == view)
            {
                return view->viewport()->mapFrom(view, watched_pos);
            }
            if (watched_widget != view->viewport() && watched_widget != nullptr)
            {
                return view->viewport()->mapFromGlobal(watched_widget->mapToGlobal(watched_pos));
            }
            return watched_pos;
        }

        // Returns source-model rows (column == kNameColumn) for every row that has
        // at least one selected cell in the proxy.
        template <typename PanelLike>
        QModelIndexList selected_rows_for_panel(PanelLike const& panel)
        {
            QItemSelectionModel* selection_model = selection_model_for_panel(panel);
            if (selection_model == nullptr || panel.model == nullptr)
            {
                return {};
            }

            int const primary = panel_primary_column(panel);
            QModelIndexList const primary_indexes = selection_model->selectedRows(primary);

            QModelIndexList rows;
            rows.reserve(primary_indexes.size());
            QSet<int> seen_rows;
            for (QModelIndex const& proxy_index : primary_indexes)
            {
                QModelIndex const src = map_to_source_index(panel, proxy_index);
                if (!src.isValid() || seen_rows.contains(src.row()))
                    continue;
                seen_rows.insert(src.row());
                rows << panel.model->index(src.row(), DirectoryListModel::kNameColumn);
            }
            // Fallback: some callers may have selected non-primary cells (e.g. via
            // Ctrl+A with SelectItems), while select-by-type intentionally marks only
            // the primary column. Merge those cells with any full-row selections above.
            QModelIndexList const indexes = selection_model->selectedIndexes();
            for (QModelIndex const& proxy_index : indexes)
            {
                QModelIndex const src = map_to_source_index(panel, proxy_index);
                if (!src.isValid() || seen_rows.contains(src.row()))
                    continue;
                seen_rows.insert(src.row());
                rows << panel.model->index(src.row(), DirectoryListModel::kNameColumn);
            }
            return rows;
        }

        // Returns a source-model index (column == kNameColumn) for the currently
        // focused row, resolving through the proxy if necessary.
        template <typename PanelLike>
        QModelIndex focused_index_for_panel(PanelLike const& panel)
        {
            if (panel.model == nullptr)
            {
                return {};
            }

            auto to_source = [&panel](QModelIndex const& idx)
            {
                return map_to_source_index(panel, idx);
            };

            if (QAbstractItemView* view = panel.current_item_view())
            {
                QModelIndex const focused_index = view->currentIndex();
                if (focused_index.isValid())
                {
                    QModelIndex const src = to_source(focused_index);
                    if (src.isValid())
                    {
                        return panel.model->index(src.row(), DirectoryListModel::kNameColumn);
                    }
                }
            }

            QItemSelectionModel* selection_model = selection_model_for_panel(panel);
            if (selection_model == nullptr)
            {
                return {};
            }

            QModelIndex const current_index = selection_model->currentIndex();
            if (current_index.isValid())
            {
                QModelIndex const src = to_source(current_index);
                if (src.isValid())
                {
                    return panel.model->index(src.row(), DirectoryListModel::kNameColumn);
                }
            }

            int const primary = panel_primary_column(panel);
            QModelIndexList const selected_rows = selection_model->selectedRows(primary);
            if (!selected_rows.isEmpty())
            {
                QModelIndex const src = to_source(selected_rows.front());
                if (src.isValid())
                {
                    return panel.model->index(src.row(), DirectoryListModel::kNameColumn);
                }
            }

            return {};
        }

    } // namespace

    QString MainWindow::PanelController::focused_path() const
    {
        if (model == nullptr)
        {
            return {};
        }

        QModelIndex const focused_index = focused_index_for_panel(*this);
        if (!focused_index.isValid())
        {
            return {};
        }
        return model->path_for_row(focused_index.row());
    }

    bool MainWindow::PanelController::focused_item_is_dir() const
    {
        if (model == nullptr)
        {
            return false;
        }

        QModelIndex const focused_index = focused_index_for_panel(*this);
        return focused_index.isValid() && model->is_dir_for_row(focused_index.row());
    }

    bool MainWindow::PanelController::focused_item_is_parent_link() const
    {
        if (model == nullptr)
        {
            return false;
        }

        QModelIndex const focused_index = focused_index_for_panel(*this);
        return focused_index.isValid() && model->is_parent_link_for_row(focused_index.row());
    }

    QStringList MainWindow::PanelController::selected_filesystem_paths_including_parent_link() const
    {
        if (model == nullptr)
        {
            return {};
        }

        QModelIndexList const rows = selected_rows_for_panel(*this);
        QStringList out;
        out.reserve(rows.size());
        for (QModelIndex const& row : rows)
        {
            out << model->path_for_row(row.row());
        }
        return out;
    }

    QStringList MainWindow::PanelController::selected_real_archive_file_paths() const
    {
        QStringList out;
        QStringList const paths = selected_real_item_paths();
        for (QString const& path : paths)
        {
            QFileInfo const info(path);
            if (!info.isFile())
            {
                continue;
            }
            if (is_archive_file(info.fileName()))
            {
                out << info.absoluteFilePath();
            }
        }
        return out;
    }

    bool MainWindow::PanelController::selected_rows_include_parent_link() const
    {
        if (model == nullptr)
        {
            return false;
        }

        QModelIndexList const rows = selected_rows_including_parent_link();
        for (QModelIndex const& row : rows)
        {
            if (model->is_parent_link_for_row(row.row()))
            {
                return true;
            }
        }
        return false;
    }

    QModelIndexList MainWindow::PanelController::selected_real_item_rows() const
    {
        if (model == nullptr)
        {
            return {};
        }

        const QModelIndexList rows = selected_rows_for_panel(*this);
        if (!rows.isEmpty())
        {
            QModelIndexList out;
            out.reserve(rows.size());
            for (const QModelIndex& row : rows)
            {
                if (!row.isValid() || model->is_parent_link_for_row(row.row()))
                {
                    continue;
                }
                out << model->index(row.row(), DirectoryListModel::kNameColumn);
            }
            return out;
        }

        return {};
    }

    QModelIndexList MainWindow::PanelController::oper_smart_real_item_rows() const
    {
        if (model == nullptr)
        {
            return {};
        }

        QModelIndexList rows = selected_real_item_rows();
        if (!rows.isEmpty())
        {
            return rows;
        }

        const int row_count = model->rowCount();
        rows.reserve(row_count);
        for (int row = 0; row < row_count; ++row)
        {
            if (model->is_parent_link_for_row(row))
            {
                continue;
            }
            rows << model->index(row, DirectoryListModel::kNameColumn);
        }
        return rows;
    }

    bool MainWindow::PanelController::source_rows_contain_dir(
        const QModelIndexList& rows) const
    {
        if (model == nullptr)
        {
            return false;
        }

        for (const QModelIndex& row : rows)
        {
            if (row.isValid() && model->is_dir_for_row(row.row()))
            {
                return true;
            }
        }
        return false;
    }

    QStringList MainWindow::PanelController::real_item_paths_for_rows(
        const QModelIndexList& rows) const
    {
        if (model == nullptr)
        {
            return {};
        }

        QStringList out;
        out.reserve(rows.size());
        for (const QModelIndex& row : rows)
        {
            if (!row.isValid() || model->is_parent_link_for_row(row.row()))
            {
                continue;
            }
            const QString path = model->path_for_row(row.row());
            if (!path.trimmed().isEmpty())
            {
                out << path;
            }
        }
        return out;
    }

    QStringList MainWindow::PanelController::selected_real_item_paths() const
    {
        return real_item_paths_for_rows(selected_real_item_rows());
    }

    QStringList MainWindow::PanelController::oper_smart_real_item_paths() const
    {
        return real_item_paths_for_rows(oper_smart_real_item_rows());
    }

    QStringList MainWindow::PanelController::selected_archive_entries() const
    {
        if (!in_archive_view() || model == nullptr)
        {
            return {};
        }

        return archive_entries_for_source_rows(selected_rows_including_parent_link());
    }

    QStringList MainWindow::PanelController::archive_entries_for_source_rows(
        const QModelIndexList& rows) const
    {
        if (!in_archive_view() || model == nullptr)
        {
            return {};
        }

        QStringList entries;
        entries.reserve(rows.size());
        for (QModelIndex const& row : rows)
        {
            if (!row.isValid() || model->is_parent_link_for_row(row.row()))
            {
                continue;
            }
            QString const rel = z7::ui::archive_support::normalize_virtual_dir(model->path_for_row(row.row()));
            if (rel.isEmpty())
            {
                continue;
            }
            entries << rel;
        }
        entries.removeDuplicates();
        return entries;
    }

    QStringList MainWindow::PanelController::operated_archive_entries() const
    {
        return archive_entries_for_source_rows(selected_real_item_rows());
    }

    QStringList MainWindow::PanelController::oper_smart_archive_entries() const
    {
        return archive_entries_for_source_rows(oper_smart_real_item_rows());
    }

    MainWindow::PanelController::SelectionSnapshot
    MainWindow::PanelController::capture_selection_snapshot() const
    {
        SelectionSnapshot snapshot;
        if (model == nullptr)
        {
            return snapshot;
        }

        snapshot.selected_filesystem_paths_including_parent_link =
            selected_filesystem_paths_including_parent_link();
        snapshot.current_path = focused_path();
        if (snapshot.current_path.isEmpty() &&
            !snapshot.selected_filesystem_paths_including_parent_link.isEmpty())
        {
            snapshot.current_path =
                snapshot.selected_filesystem_paths_including_parent_link.front();
        }
        return snapshot;
    }

    void MainWindow::PanelController::restore_selection_snapshot(
        SelectionSnapshot const& snapshot) const
    {
        if (model == nullptr || ui.details_view == nullptr ||
            ui.details_view->selectionModel() == nullptr)
        {
            return;
        }

        auto find_row_by_path = [this](QString const& path) -> int
        {
            if (path.isEmpty())
            {
                return -1;
            }
            int const row_count = model->rowCount();
            for (int row = 0; row < row_count; ++row)
            {
                if (model->path_for_row(row) == path)
                {
                    return row;
                }
            }
            return -1;
        };

        QItemSelectionModel* selection_model = this->selection_model();
        if (selection_model == nullptr)
        {
            return;
        }

        selection_model->clearSelection();
        // Selection stays strictly on the primary cell so that the view renders
        // a primary-column-only chip. The Rows flag would expand the selection
        // to every column and paint a row-wide highlight.
        QSet<int> restored_rows;
        bool first_selected = true;
        for (QString const& selected_path :
             snapshot.selected_filesystem_paths_including_parent_link)
        {
            int const row = find_row_by_path(selected_path);
            if (row < 0 || restored_rows.contains(row))
            {
                if (row < 0)
                {
                }
                continue;
            }
            restored_rows.insert(row);
            QModelIndex const proxy_index = map_source_to_proxy(
                model->index(row, DirectoryListModel::kNameColumn));
            if (proxy_index.isValid())
            {
                if (first_selected)
                {
                    selection_model->setCurrentIndex(
                        proxy_index,
                        QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Current);
                    first_selected = false;
                }
                else
                {
                    selection_model->select(proxy_index, QItemSelectionModel::Select);
                }
            }
        }

        int current_row = find_row_by_path(snapshot.current_path);
        if (current_row < 0 && !restored_rows.isEmpty())
        {
            current_row = *restored_rows.begin();
        }
        if (current_row >= 0)
        {
            QModelIndex const proxy_index = map_source_to_proxy(
                model->index(current_row, DirectoryListModel::kNameColumn));
            if (proxy_index.isValid())
            {
                selection_model->setCurrentIndex(proxy_index,
                                                 QItemSelectionModel::NoUpdate);
            }
        }
    }

    QItemSelectionModel* MainWindow::PanelController::selection_model() const
    {
        return selection_model_for_panel(*this);
    }

    QModelIndex MainWindow::PanelController::map_proxy_to_source(QModelIndex const& proxy_index) const
    {
        return map_to_source_index(*this, proxy_index);
    }

    QModelIndex MainWindow::PanelController::map_source_to_proxy(QModelIndex const& source_index) const
    {
        if (!source_index.isValid())
            return {};
        if (proxy == nullptr)
            return source_index;
        return proxy->mapFromSource(source_index);
    }

    QModelIndex MainWindow::PanelController::focused_source_index() const
    {
        return focused_index_for_panel(*this);
    }

    QModelIndexList MainWindow::PanelController::selected_rows_including_parent_link() const
    {
        return selected_rows_for_panel(*this);
    }

    QModelIndex MainWindow::hovered_source_index_for_panel(
        int panel_index,
        QAbstractItemView* view,
        QObject const* watched,
        QPoint const& watched_pos) const
    {
        if (panel_index < 0 || view == nullptr)
        {
            return {};
        }

        PanelController const& panel = panel_controller(panel_index);
        if (panel.model == nullptr)
        {
            return {};
        }

        QPoint const viewport_pos = viewport_pos_for_watched(view, watched, watched_pos);
        QModelIndex const hovered_index = view->indexAt(viewport_pos);
        if (!hovered_index.isValid())
        {
            return {};
        }

        QModelIndex const source_index = map_to_source_index(panel, hovered_index);
        if (!source_index.isValid())
        {
            return {};
        }
        return panel.model->index(source_index.row(), DirectoryListModel::kNameColumn);
    }

    DropTargetInfo MainWindow::resolve_drop_target_info_for_panel(
        int panel_index,
        QAbstractItemView* view,
        QObject const* watched,
        QDropEvent const* event,
        QString const& fallback_directory) const
    {
        DropTargetInfo info;
        PanelController const& panel = panel_controller(panel_index);
        DirectoryListModel const* model = panel.model;
        info.directory = fallback_directory;
        if (model != nullptr && model->is_virtual_mode())
        {
            info.archive_virtual_dir =
                z7::ui::archive_support::normalize_virtual_dir(panel.archive.virtual_dir);
            info.allow_copy_move = true;
        }
        else
        {
            info.allow_copy_move = QFileInfo(fallback_directory).isDir();
        }
        if (model == nullptr || view == nullptr || event == nullptr)
        {
            return info;
        }

        QModelIndex const hovered = hovered_source_index_for_panel(
            panel_index, view, watched, event->position().toPoint());
        if (!hovered.isValid())
        {
            return info;
        }

        int const row = hovered.row();
        if (model->is_parent_link_for_row(row))
        {
            info.allow_copy_move = model->is_virtual_mode() ? true : false;
            return info;
        }
        if (!model->is_dir_for_row(row))
        {
            info.allow_copy_move = model->is_virtual_mode() ? true : false;
            return info;
        }

        QString const hovered_path = model->path_for_row(row).trimmed();
        if (model->is_virtual_mode())
        {
            info.archive_virtual_dir =
                z7::ui::archive_support::normalize_virtual_dir(hovered_path);
            info.allow_copy_move = true;
            info.archive_directory_row_target = true;
            return info;
        }

        QFileInfo const hovered_info(hovered_path);
        if (!hovered_info.isDir())
        {
            return info;
        }
        info.directory = hovered_info.absoluteFilePath();
        info.allow_copy_move = true;
        return info;
    }

} // namespace z7::ui::filemanager
