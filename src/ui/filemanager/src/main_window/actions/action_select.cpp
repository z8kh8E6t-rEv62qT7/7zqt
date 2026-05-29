// src/ui/filemanager/src/main_window/actions/action_select.cpp
// Role: Selection commands (Select/Deselect/SelectByType) aligned with original panel semantics.

#include "main_window/deps.h"
#include "main_window/internal.h"

namespace z7::ui::filemanager {
namespace {

QString display_name_for_row(const DirectoryListModel* model, int row) {
  if (model == nullptr || row < 0 || row >= model->rowCount()) {
    return QString();
  }
  return model->data(model->index(row, DirectoryListModel::kNameColumn), Qt::DisplayRole)
      .toString();
}

Qt::CaseSensitivity original_type_match_case_sensitivity() {
#if defined(Q_OS_WIN) || defined(Q_OS_MAC)
  return Qt::CaseInsensitive;
#else
  return Qt::CaseSensitive;
#endif
}

QString leaf_name_for_path(QString path) {
  path = path.trimmed();
  if (path.isEmpty()) {
    return {};
  }
  const int slash_pos = path.lastIndexOf(QLatin1Char('/'));
  const int backslash_pos = path.lastIndexOf(QLatin1Char('\\'));
  const int separator_pos = slash_pos > backslash_pos ? slash_pos : backslash_pos;
  if (separator_pos >= 0 && separator_pos + 1 < path.size()) {
    return path.mid(separator_pos + 1);
  }
  return path;
}

QString leaf_name_for_row(const DirectoryListModel* model, int row) {
  if (model == nullptr || row < 0 || row >= model->rowCount()) {
    return {};
  }
  return leaf_name_for_path(model->path_for_row(row));
}

template <typename PanelLike>
QModelIndex focused_source_index_for_panel(const PanelLike& panel) {
  if (panel.model == nullptr) {
    return {};
  }

  if (QAbstractItemView* view = panel.current_item_view()) {
    const QModelIndex focused_index = panel.map_proxy_to_source(view->currentIndex());
    if (focused_index.isValid()) {
      return focused_index;
    }
  }

  if (QItemSelectionModel* selection_model = panel.selection_model()) {
    const QModelIndex focused_index =
        panel.map_proxy_to_source(selection_model->currentIndex());
    if (focused_index.isValid()) {
      return focused_index;
    }
  }

  const QModelIndexList selected_rows = panel.selected_rows_including_parent_link();
  if (!selected_rows.isEmpty()) {
    return selected_rows.front();
  }
  return {};
}

bool matches_original_type_name(const QString& focused_leaf_name,
                                const QString& candidate_leaf_name) {
  const int focused_dot_pos = focused_leaf_name.lastIndexOf(QLatin1Char('.'));
  if (focused_dot_pos < 0) {
    return candidate_leaf_name.lastIndexOf(QLatin1Char('.')) < 0;
  }

  return candidate_leaf_name.endsWith(focused_leaf_name.mid(focused_dot_pos),
                                      original_type_match_case_sensitivity());
}

// Applies a select/deselect to a single source row through the panel's proxy
// so that the selection model (which operates on proxy indices) sees the
// right target. Template so we can take the private PanelController type from
// the caller's scope without violating access control.
template <typename PanelLike>
void apply_row_selection_source(const PanelLike& panel,
                                int source_row,
                                bool select_mode) {
  QItemSelectionModel* selection_model = panel.selection_model();
  if (selection_model == nullptr || panel.model == nullptr || source_row < 0) {
    return;
  }
  const QModelIndex source_index =
      panel.model->index(source_row, DirectoryListModel::kNameColumn);
  const QModelIndex proxy_index = panel.map_source_to_proxy(source_index);
  if (!proxy_index.isValid()) {
    return;
  }
  // No Rows flag: selection stays on the primary cell so the view renders a
  // primary-column chip instead of a row-wide highlight.
  const QItemSelectionModel::SelectionFlags flags =
      select_mode ? QItemSelectionModel::Select
                  : (QItemSelectionModel::Deselect | QItemSelectionModel::Rows);
  selection_model->select(proxy_index, flags);
}

template <typename PanelLike>
void set_operable_row_selection_source(const PanelLike& panel,
                                       int source_row,
                                       bool selected) {
  if (panel.model == nullptr ||
      source_row < 0 ||
      source_row >= panel.model->rowCount() ||
      panel.model->is_parent_link_for_row(source_row)) {
    return;
  }
  apply_row_selection_source(panel, source_row, selected);
}

template <typename PanelLike>
void clear_parent_link_selection_source(const PanelLike& panel) {
  if (panel.model == nullptr || panel.selection_model() == nullptr) {
    return;
  }

  for (int row = 0; row < panel.model->rowCount(); ++row) {
    if (!panel.model->is_parent_link_for_row(row)) {
      continue;
    }
    apply_row_selection_source(panel, row, false);
  }
}

template <typename PanelLike>
void apply_type_selection_by_original_name(const PanelLike& panel,
                                           bool select_mode) {
  if (panel.model == nullptr || panel.selection_model() == nullptr) {
    return;
  }

  const QModelIndex focused_index = focused_source_index_for_panel(panel);
  if (!focused_index.isValid()) {
    return;
  }

  const int focused_row = focused_index.row();
  if (focused_row < 0 || focused_row >= panel.model->rowCount() ||
      panel.model->is_parent_link_for_row(focused_row)) {
    return;
  }

  const bool focused_is_dir = panel.model->is_dir_for_row(focused_row);
  const QString focused_leaf_name = leaf_name_for_row(panel.model, focused_row);

  for (int row = 0; row < panel.model->rowCount(); ++row) {
    if (panel.model->is_parent_link_for_row(row) ||
        panel.model->is_dir_for_row(row) != focused_is_dir) {
      continue;
    }

    const bool type_match =
        focused_is_dir ||
        matches_original_type_name(focused_leaf_name, leaf_name_for_row(panel.model, row));
    if (!type_match) {
      continue;
    }

    apply_row_selection_source(panel, row, select_mode);
  }
}

}  // namespace

void MainWindow::set_operable_rows_selected_for_panel(int panel_index,
                                                      bool selected) {
  PanelController& panel = panel_controller(panel_index);
  if (panel.model == nullptr || panel.selection_model() == nullptr) {
    return;
  }

  clear_parent_link_selection_source(panel);
  for (int row = 0; row < panel.model->rowCount(); ++row) {
    set_operable_row_selection_source(panel, row, selected);
  }

  update_status_for_panel(panel_index);
  refresh_action_states();
}

void MainWindow::invert_operable_selection_for_panel(int panel_index) {
  PanelController& panel = panel_controller(panel_index);
  QItemSelectionModel* selection_model = panel.selection_model();
  if (panel.model == nullptr || selection_model == nullptr) {
    return;
  }

  clear_parent_link_selection_source(panel);
  for (int row = 0; row < panel.model->rowCount(); ++row) {
    if (panel.model->is_parent_link_for_row(row)) {
      continue;
    }
    const QModelIndex proxy_index = panel.map_source_to_proxy(
        panel.model->index(row, DirectoryListModel::kNameColumn));
    if (!proxy_index.isValid()) {
      continue;
    }
    selection_model->select(proxy_index, QItemSelectionModel::Toggle);
  }

  update_status_for_panel(panel_index);
  refresh_action_states();
}

void MainWindow::on_select_requested() {
  PanelController& panel = active_panel_controller();
  if (panel.model == nullptr ||
      panel.ui.details_view == nullptr ||
      panel.ui.details_view->selectionModel() == nullptr) {
    return;
  }

  bool ok = false;
  const QString mask = QInputDialog::getText(this,
                                             z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(603)),
                                             lang_or(6404),
                                             QLineEdit::Normal,
                                             QStringLiteral("*"),
                                             &ok)
                           .trimmed();
  if (!ok || mask.isEmpty()) {
    return;
  }

  const QRegularExpression regex(
      QRegularExpression::wildcardToRegularExpression(mask),
      QRegularExpression::CaseInsensitiveOption);
  if (!regex.isValid()) {
    return;
  }

  for (int row = 0; row < panel.model->rowCount(); ++row) {
    if (panel.model->is_parent_link_for_row(row)) {
      continue;
    }
    const QString name = display_name_for_row(panel.model, row);
    if (!regex.match(name).hasMatch()) {
      continue;
    }
    apply_row_selection_source(panel, row, true);
  }

  update_status();
  refresh_action_states();
}

void MainWindow::on_deselect_requested() {
  PanelController& panel = active_panel_controller();
  if (panel.model == nullptr ||
      panel.ui.details_view == nullptr ||
      panel.ui.details_view->selectionModel() == nullptr) {
    return;
  }

  bool ok = false;
  const QString mask = QInputDialog::getText(this,
                                             z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(604)),
                                             lang_or(6404),
                                             QLineEdit::Normal,
                                             QStringLiteral("*"),
                                             &ok)
                           .trimmed();
  if (!ok || mask.isEmpty()) {
    return;
  }

  const QRegularExpression regex(
      QRegularExpression::wildcardToRegularExpression(mask),
      QRegularExpression::CaseInsensitiveOption);
  if (!regex.isValid()) {
    return;
  }

  for (int row = 0; row < panel.model->rowCount(); ++row) {
    if (panel.model->is_parent_link_for_row(row)) {
      continue;
    }
    const QString name = display_name_for_row(panel.model, row);
    if (!regex.match(name).hasMatch()) {
      continue;
    }
    apply_row_selection_source(panel, row, false);
  }

  update_status();
  refresh_action_states();
}

void MainWindow::on_select_by_type_requested() {
  PanelController& panel = active_panel_controller();
  if (panel.model == nullptr ||
      panel.ui.details_view == nullptr ||
      panel.ui.details_view->selectionModel() == nullptr) {
    return;
  }

  apply_type_selection_by_original_name(panel, true);

  update_status();
  refresh_action_states();
}

void MainWindow::on_deselect_by_type_requested() {
  PanelController& panel = active_panel_controller();
  if (panel.model == nullptr ||
      panel.ui.details_view == nullptr ||
      panel.ui.details_view->selectionModel() == nullptr) {
    return;
  }

  apply_type_selection_by_original_name(panel, false);

  update_status();
  refresh_action_states();
}

}  // namespace z7::ui::filemanager
