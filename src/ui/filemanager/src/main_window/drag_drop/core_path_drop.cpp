// src/ui/filemanager/src/main_window/drag_drop/core_path_drop.cpp
// Role: Event-filter routing for panel activation, key handlers, and drag/drop dispatch.

#include "main_window/deps.h"
#include "main_window/internal.h"

namespace z7::ui::filemanager {

bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
  if (path_combo_ != nullptr && watched == path_combo_->lineEdit() &&
      event != nullptr && event->type() == QEvent::ShortcutOverride) {
    auto* key_event = static_cast<QKeyEvent*>(event);
    const bool no_modifiers = key_event->modifiers() == Qt::NoModifier;
    if (no_modifiers &&
        (key_event->key() == Qt::Key_Space ||
         key_event->key() == Qt::Key_Return ||
         key_event->key() == Qt::Key_Enter ||
         key_event->key() == Qt::Key_Backspace)) {
      event->accept();
      return true;
    }
  }

  if (event != nullptr) {
    int panel_index = panel_index_for_view(watched);
    const bool panel_view_target = panel_index >= 0;
    const bool window_drop_target =
        watched == this || watched == centralWidget();
    const bool is_drop_event =
        event->type() == QEvent::DragEnter ||
        event->type() == QEvent::DragMove ||
        event->type() == QEvent::Drop;
    if (panel_index < 0 && window_drop_target && is_drop_event) {
      panel_index = active_panel_index_;
    }
    if (panel_index >= 0) {
      if (panel_view_target &&
          (event->type() == QEvent::FocusIn ||
           event->type() == QEvent::MouseButtonPress)) {
        set_active_panel(panel_index);
      }
      if (event->type() == QEvent::DragEnter ||
          event->type() == QEvent::DragMove) {
        return handle_panel_drag_enter_or_move(
            watched,
            panel_index,
            window_drop_target,
            static_cast<QDropEvent*>(event));
      }
      if (event->type() == QEvent::Drop) {
        return handle_panel_drop(watched,
                                 panel_index,
                                 window_drop_target,
                                 static_cast<QDropEvent*>(event));
      }
      if (event->type() == QEvent::KeyPress) {
        set_active_panel(panel_index);
        auto* key_event = static_cast<QKeyEvent*>(event);
        const Qt::KeyboardModifiers modifiers =
            key_event->modifiers() & ~Qt::KeypadModifier;
        if (handle_favorite_slot_shortcut(*key_event, modifiers)) {
          return true;
        }
        if (modifiers == Qt::AltModifier) {
          switch (key_event->key()) {
            case Qt::Key_Up:
              bind_opposite_panel_to_same_folder();
              return true;
            case Qt::Key_Left:
            case Qt::Key_Right:
              bind_opposite_panel_to_focused_folder();
              return true;
            default:
              break;
          }
        }
        if (key_event->key() == Qt::Key_A &&
            modifiers == Qt::ControlModifier) {
          set_operable_rows_selected_for_panel(panel_index, true);
          return true;
        }
        if (key_event->key() == Qt::Key_N &&
            modifiers == Qt::ControlModifier) {
          on_create_file_requested();
          return true;
        }
        if (key_event->key() == Qt::Key_R &&
            modifiers == Qt::ControlModifier) {
          on_refresh_requested();
          return true;
        }
        if (key_event->key() == Qt::Key_W &&
            modifiers == Qt::ControlModifier) {
          close();
          return true;
        }
        if (modifiers == Qt::ControlModifier) {
          switch (key_event->key()) {
            case Qt::Key_1:
              on_view_mode_action_triggered(
                  PanelController::kViewModeLargeIcons);
              return true;
            case Qt::Key_2:
              on_view_mode_action_triggered(
                  PanelController::kViewModeSmallIcons);
              return true;
            case Qt::Key_3:
              on_view_mode_action_triggered(PanelController::kViewModeList);
              return true;
            case Qt::Key_4:
              on_view_mode_action_triggered(PanelController::kViewModeDetails);
              return true;
            default:
              break;
          }
        }
        if (modifiers == Qt::ControlModifier) {
          switch (key_event->key()) {
            case Qt::Key_F3:
              on_sort_mode_action_triggered(kSortActionName);
              return true;
            case Qt::Key_F4:
              on_sort_mode_action_triggered(kSortActionType);
              return true;
            case Qt::Key_F5:
              on_sort_mode_action_triggered(kSortActionDate);
              return true;
            case Qt::Key_F6:
              on_sort_mode_action_triggered(kSortActionSize);
              return true;
            case Qt::Key_F7:
              on_sort_mode_action_triggered(kSortActionUnsorted);
              return true;
            default:
              break;
          }
        }
        if (key_event->key() == Qt::Key_Plus &&
            modifiers == Qt::AltModifier) {
          on_select_by_type_requested();
          return true;
        }
        if (key_event->key() == Qt::Key_Plus &&
            modifiers == Qt::ShiftModifier) {
          set_operable_rows_selected_for_panel(panel_index, true);
          return true;
        }
        if (key_event->key() == Qt::Key_Plus &&
            modifiers == Qt::NoModifier) {
          on_select_requested();
          return true;
        }
        if (key_event->key() == Qt::Key_Minus &&
            modifiers == Qt::AltModifier) {
          on_deselect_by_type_requested();
          return true;
        }
        if (key_event->key() == Qt::Key_Minus &&
            modifiers == Qt::ShiftModifier) {
          set_operable_rows_selected_for_panel(panel_index, false);
          return true;
        }
        if (key_event->key() == Qt::Key_Minus &&
            modifiers == Qt::NoModifier) {
          on_deselect_requested();
          return true;
        }
        if (key_event->key() == Qt::Key_Asterisk &&
            modifiers == Qt::NoModifier) {
          invert_operable_selection_for_panel(panel_index);
          return true;
        }
        if (key_event->key() == Qt::Key_F4 &&
            modifiers == Qt::ShiftModifier) {
          on_create_file_requested();
          return true;
        }
        if (key_event->key() == Qt::Key_F5 &&
            (modifiers == Qt::NoModifier ||
             modifiers == Qt::ShiftModifier)) {
          run_copy_or_move(false, modifiers == Qt::ShiftModifier);
          return true;
        }
        if (key_event->key() == Qt::Key_F6 &&
            (modifiers == Qt::NoModifier ||
             modifiers == Qt::ShiftModifier)) {
          run_copy_or_move(true, modifiers == Qt::ShiftModifier);
          return true;
        }
        if (key_event->key() == Qt::Key_F7 &&
            modifiers == Qt::NoModifier) {
          on_create_folder_requested();
          return true;
        }
        if (key_event->key() == Qt::Key_F12 &&
            modifiers == Qt::AltModifier) {
          on_folders_history_requested();
          return true;
        }
        // Return/Enter/Backspace/Delete for the details view are routed via
        // StructuredListView signals; they are only handled here for views
        // that do not expose those signals (e.g. the icon list view).
        if (key_event->key() == Qt::Key_F9 &&
            modifiers == Qt::NoModifier) {
          on_two_panels_action_triggered();
          return true;
        }
        const QAbstractItemView* details_view =
            active_panel_controller().ui.details_view;
        const bool details_view_key_event =
            watched == details_view ||
            (details_view != nullptr && watched == details_view->viewport());
        if (!details_view_key_event) {
          if (key_event->key() == Qt::Key_Return ||
              key_event->key() == Qt::Key_Enter) {
            activate_panel_selection(key_event->modifiers());
            return true;
          }
          if (key_event->key() == Qt::Key_Backspace) {
            on_open_parent_requested();
            return true;
          }
        }
      }
    }
  }

  return QMainWindow::eventFilter(watched, event);
}

}  // namespace z7::ui::filemanager
