// src/ui/filemanager/src/main_window/state/state_panel_view.cpp
// Role: PanelController view-mode state and active item view selection.

#include "main_window/deps.h"
#include "main_window/internal.h"

namespace z7::ui::filemanager
{

    namespace
    {

        int bounded_scroll_value(QScrollBar const* scroll_bar, int value)
        {
            if (scroll_bar == nullptr)
            {
                return value;
            }
            if (value < scroll_bar->minimum())
            {
                return scroll_bar->minimum();
            }
            if (value > scroll_bar->maximum())
            {
                return scroll_bar->maximum();
            }
            return value;
        }

    } // namespace

    void MainWindow::PanelController::set_view_mode(ViewMode mode)
    {
        int normalized_mode = static_cast<int>(mode);
        if (normalized_mode < kViewModeLargeIcons || normalized_mode > kViewModeDetails)
        {
            normalized_mode = kViewModeDetails;
        }
        view_mode = static_cast<ViewMode>(normalized_mode);

        if (ui.view_stack == nullptr || ui.details_view == nullptr || ui.icon_list_view == nullptr)
        {
            return;
        }

        QWidget* metrics_reference = ui.view_stack != nullptr ? static_cast<QWidget*>(ui.view_stack) : ui.details_view;
        int const small_list_icon_extent = z7::platform::qt::file_list_icon_extent(false, metrics_reference);
        int const large_list_icon_extent = z7::platform::qt::file_list_icon_extent(true, metrics_reference);
        QSize const small_icon_grid = z7::platform::qt::file_list_grid_size(false, metrics_reference);
        QSize const large_icon_grid = z7::platform::qt::file_list_grid_size(true, metrics_reference);

        ui.details_view->setIconSize(QSize(small_list_icon_extent, small_list_icon_extent));

        auto* icon_view = ui.icon_list_view;
        icon_view->setViewMode(QListView::IconMode);
        icon_view->setFlow(QListView::LeftToRight);
        icon_view->setWrapping(true);
        icon_view->setResizeMode(QListView::Adjust);
        icon_view->setWordWrap(false);
        icon_view->setUniformItemSizes(false);
        icon_view->setGridSize(QSize());
        icon_view->setSpacing(0);

        switch (view_mode)
        {
            case kViewModeLargeIcons:
                icon_view->setViewMode(QListView::IconMode);
                icon_view->setIconSize(QSize(large_list_icon_extent, large_list_icon_extent));
                icon_view->setGridSize(large_icon_grid);
                icon_view->setSpacing(4);
                ui.view_stack->setCurrentWidget(icon_view);
                break;
            case kViewModeSmallIcons:
                icon_view->setViewMode(QListView::IconMode);
                icon_view->setIconSize(QSize(small_list_icon_extent, small_list_icon_extent));
                icon_view->setGridSize(small_icon_grid);
                icon_view->setSpacing(2);
                ui.view_stack->setCurrentWidget(icon_view);
                break;
            case kViewModeList:
                icon_view->setViewMode(QListView::ListMode);
                icon_view->setFlow(QListView::TopToBottom);
                icon_view->setWrapping(false);
                icon_view->setIconSize(QSize(small_list_icon_extent, small_list_icon_extent));
                icon_view->setUniformItemSizes(true);
                ui.view_stack->setCurrentWidget(icon_view);
                break;
            case kViewModeDetails:
            default:
                ui.view_stack->setCurrentWidget(ui.details_view);
                break;
        }
    }

    QAbstractItemView* MainWindow::PanelController::current_item_view() const
    {
        if (view_mode == kViewModeDetails || ui.icon_list_view == nullptr)
        {
            return ui.details_view;
        }
        return ui.icon_list_view;
    }

    MainWindow::PanelController::ScrollPositionSnapshot
    MainWindow::PanelController::capture_scroll_position() const
    {
        auto capture_view_scroll_position = [](QAbstractItemView const* view)
        {
            ViewScrollPosition position;
            if (view == nullptr)
            {
                return position;
            }

            if (QScrollBar const* horizontal = view->horizontalScrollBar())
            {
                position.horizontal_value = horizontal->value();
            }
            if (QScrollBar const* vertical = view->verticalScrollBar())
            {
                position.vertical_value = vertical->value();
            }
            position.valid = true;
            return position;
        };

        ScrollPositionSnapshot snapshot;
        snapshot.details = capture_view_scroll_position(ui.details_view);
        snapshot.icon = capture_view_scroll_position(ui.icon_list_view);
        return snapshot;
    }

    void MainWindow::PanelController::restore_scroll_position(
        ScrollPositionSnapshot const& snapshot) const
    {
        auto restore_view_scroll_position =
            [](QAbstractItemView* view, ViewScrollPosition const& position)
        {
            if (!position.valid || view == nullptr)
            {
                return;
            }

            if (QScrollBar* horizontal = view->horizontalScrollBar())
            {
                horizontal->setValue(
                    bounded_scroll_value(horizontal, position.horizontal_value));
            }
            if (QScrollBar* vertical = view->verticalScrollBar())
            {
                vertical->setValue(
                    bounded_scroll_value(vertical, position.vertical_value));
            }
        };

        restore_view_scroll_position(ui.details_view, snapshot.details);
        restore_view_scroll_position(ui.icon_list_view, snapshot.icon);
    }

} // namespace z7::ui::filemanager
