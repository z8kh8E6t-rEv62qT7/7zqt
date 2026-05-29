// src/ui/widgets/src/structured_list_view.cpp
// Role: Primary-column-owned interaction semantics for StructuredListView.

#include "structured_list_view.h"

#include <QAbstractProxyModel>
#include <QApplication>
#include <QContextMenuEvent>
#include <QCursor>
#include <QHeaderView>
#include <QItemSelection>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QScrollBar>
#include <QStyleOptionViewItem>
#include <QTimer>

#include <algorithm>

#include "structured_list_delegate.h"

namespace z7::ui::widgets {

StructuredListView::StructuredListView(QWidget* parent) : QTableView(parent) {
  setSelectionBehavior(QAbstractItemView::SelectItems);
  setSelectionMode(QAbstractItemView::ExtendedSelection);
  setMouseTracking(true);
  setShowGrid(false);
  setAlternatingRowColors(false);
  setFocusPolicy(Qt::StrongFocus);
  setEditTriggers(QAbstractItemView::NoEditTriggers);
  setTabKeyNavigation(false);
  setWordWrap(false);
  setTextElideMode(Qt::ElideRight);
  setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
  setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
  setCornerButtonEnabled(false);
  // Default context menu policy dispatches through contextMenuEvent.
  setContextMenuPolicy(Qt::DefaultContextMenu);

  verticalHeader()->setVisible(false);
  verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);

  auto* hh = horizontalHeader();
  hh->setSectionsClickable(true);
  hh->setStretchLastSection(false);
  hh->setHighlightSections(false);
  hh->setSortIndicatorShown(true);
  hh->setSectionResizeMode(QHeaderView::Interactive);

  delegate_ = new StructuredListDelegate(this);
  setItemDelegate(delegate_);
}

StructuredListView::~StructuredListView() = default;

void StructuredListView::set_config(StructuredListConfig config) {
  config_ = std::move(config);
  apply_config_to_view();
}

void StructuredListView::apply_config_to_view() {
  horizontalHeader()->setVisible(config_.show_header);
  setSortingEnabled(config_.sorting_enabled);

  if (model() != nullptr) {
    const int cols = std::min<int>(static_cast<int>(config_.columns.size()),
                                   model()->columnCount());
    for (int i = 0; i < cols; ++i) {
      const auto& col = config_.columns[static_cast<size_t>(i)];
      if (col.default_width > 0) {
        setColumnWidth(i, col.default_width);
      }
      if (col.hidden_by_default) {
        setColumnHidden(i, true);
      }
      horizontalHeader()->setSectionResizeMode(i, QHeaderView::Interactive);
    }
  }
  if (config_.style.row_height_hint > 0) {
    verticalHeader()->setDefaultSectionSize(config_.style.row_height_hint);
  }
  viewport()->update();
}

bool StructuredListView::is_primary_column(const QModelIndex& index) const {
  return index.isValid() && index.column() == primary_column();
}

QModelIndex StructuredListView::normalize_to_primary_column(
    const QModelIndex& index) const {
  if (!index.isValid() || model() == nullptr) return {};
  if (index.column() == primary_column()) return index;
  return model()->index(index.row(), primary_column(), index.parent());
}

QModelIndex StructuredListView::hover_index() const { return hover_index_; }

void StructuredListView::refresh_hover_from_cursor() {
  if (model() == nullptr || viewport() == nullptr) {
    clear_hover_index();
    return;
  }

  const QPoint viewport_pos = viewport()->mapFromGlobal(QCursor::pos());
  if (!viewport()->rect().contains(viewport_pos)) {
    clear_hover_index();
    return;
  }

  if (point_is_on_primary(viewport_pos)) {
    set_hover_index(primary_index_at(viewport_pos));
  } else {
    clear_hover_index();
  }
}

void StructuredListView::set_hover_index(const QModelIndex& index) {
  const QPersistentModelIndex normalized(index);
  if (normalized == hover_index_) return;
  const QPersistentModelIndex old = hover_index_;
  hover_index_ = normalized;
  // Repaint whole rows so row-wide faint hover updates correctly.
  if (old.isValid()) {
    for (int c = 0; c < model()->columnCount(); ++c) {
      viewport()->update(visualRect(model()->index(old.row(), c)));
    }
  }
  if (hover_index_.isValid()) {
    for (int c = 0; c < model()->columnCount(); ++c) {
      viewport()->update(visualRect(model()->index(hover_index_.row(), c)));
    }
  }
}

void StructuredListView::clear_hover_index() { set_hover_index({}); }

QModelIndex StructuredListView::primary_index_at(const QPoint& p) const {
  const QModelIndex hit = indexAt(p);
  if (!hit.isValid()) return {};
  return normalize_to_primary_column(hit);
}

bool StructuredListView::point_is_on_primary(const QPoint& p) const {
  const QModelIndex hit = indexAt(p);
  return hit.isValid() && hit.column() == primary_column();
}

void StructuredListView::blank_selection() {
  if (auto* sel = selectionModel()) {
    const bool had = sel->hasSelection() || sel->currentIndex().isValid();
    sel->clearSelection();
    sel->clearCurrentIndex();
    if (had) emit selection_blanked();
  }
  selection_anchor_ = QPersistentModelIndex();
}

void StructuredListView::select_single(const QModelIndex& primary) {
  auto* sel = selectionModel();
  if (sel == nullptr || !primary.isValid()) return;
  sel->setCurrentIndex(
      primary, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Current);
  selection_anchor_ = primary;
}

void StructuredListView::select_toggle(const QModelIndex& primary) {
  auto* sel = selectionModel();
  if (sel == nullptr || !primary.isValid()) return;
  QItemSelection one(primary, primary);
  sel->select(one, QItemSelectionModel::Toggle);
  sel->setCurrentIndex(primary, QItemSelectionModel::NoUpdate);
  if (!selection_anchor_.isValid()) selection_anchor_ = primary;
}

void StructuredListView::select_range_to(const QModelIndex& primary) {
  auto* sel = selectionModel();
  if (sel == nullptr || !primary.isValid() || model() == nullptr) return;
  QModelIndex anchor = selection_anchor_;
  if (!anchor.isValid()) anchor = primary;
  const int top = std::min(anchor.row(), primary.row());
  const int bottom = std::max(anchor.row(), primary.row());
  const QModelIndex tl =
      model()->index(top, primary_column(), primary.parent());
  const QModelIndex br =
      model()->index(bottom, primary_column(), primary.parent());
  QItemSelection range(tl, br);
  sel->select(range, QItemSelectionModel::ClearAndSelect);
  sel->setCurrentIndex(primary, QItemSelectionModel::NoUpdate);
}

void StructuredListView::mousePressEvent(QMouseEvent* event) {
  const QPoint pos = event->pos();

  if (event->button() == Qt::RightButton) {
    // Right click never mutates selection; contextMenuEvent handles the menu.
    event->accept();
    return;
  }
  if (event->button() != Qt::LeftButton) {
    QTableView::mousePressEvent(event);
    return;
  }

  left_pressed_ = true;
  press_viewport_pos_ = pos;
  defer_single_collapse_ = false;

  if (!point_is_on_primary(pos)) {
    blank_selection();
    press_primary_index_ = QPersistentModelIndex();
    event->accept();
    return;
  }

  const QModelIndex primary = primary_index_at(pos);
  press_primary_index_ = primary;

  const Qt::KeyboardModifiers mods = event->modifiers();
  auto* sel = selectionModel();
  if (mods & Qt::ControlModifier) {
    select_toggle(primary);
  } else if (mods & Qt::ShiftModifier) {
    select_range_to(primary);
  } else {
    if (sel != nullptr && sel->isSelected(primary) &&
        sel->selection().indexes().size() > 1) {
      // Preserve multi-selection so drag-and-drop works on a group. Collapse
      // to single on mouseRelease if no drag started.
      defer_single_collapse_ = true;
      sel->setCurrentIndex(primary, QItemSelectionModel::NoUpdate);
    } else {
      select_single(primary);
    }
  }
  event->accept();
}

void StructuredListView::mouseReleaseEvent(QMouseEvent* event) {
  if (event->button() == Qt::LeftButton) {
    const bool was_pressed = left_pressed_;
    const QPersistentModelIndex primary = press_primary_index_;
    const bool drag_happened = drag_in_progress_;
    const bool deferred = defer_single_collapse_;
    left_pressed_ = false;
    drag_in_progress_ = false;
    defer_single_collapse_ = false;
    press_primary_index_ = QPersistentModelIndex();

    if (was_pressed && !drag_happened && primary.isValid() &&
        !(event->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier))) {
      if (deferred) {
        select_single(primary);
      }
      emit primary_clicked(primary);
    }
  }
  QTableView::mouseReleaseEvent(event);
}

void StructuredListView::mouseMoveEvent(QMouseEvent* event) {
  if (left_pressed_ && !drag_in_progress_ && press_primary_index_.isValid()) {
    const int dist = (event->pos() - press_viewport_pos_).manhattanLength();
    if (dist >= QApplication::startDragDistance()) {
      drag_in_progress_ = true;
      if (model() != nullptr) {
        startDrag(model()->supportedDragActions());
      }
      return;
    }
  }
  if (point_is_on_primary(event->pos())) {
    set_hover_index(primary_index_at(event->pos()));
  } else {
    clear_hover_index();
  }
  QTableView::mouseMoveEvent(event);
}

void StructuredListView::mouseDoubleClickEvent(QMouseEvent* event) {
  if (event->button() != Qt::LeftButton) {
    QTableView::mouseDoubleClickEvent(event);
    return;
  }
  if (!point_is_on_primary(event->pos())) {
    event->accept();
    return;
  }
  const QModelIndex primary = primary_index_at(event->pos());
  if (primary.isValid()) {
    emit primary_double_clicked(primary);
  }
  event->accept();
}

void StructuredListView::leaveEvent(QEvent* event) {
  clear_hover_index();
  QTableView::leaveEvent(event);
}

void StructuredListView::keyPressEvent(QKeyEvent* event) {
  if (state() == QAbstractItemView::EditingState) {
    QTableView::keyPressEvent(event);
    return;
  }

  switch (event->key()) {
    case Qt::Key_Delete:
      emit delete_pressed();
      event->accept();
      return;
    case Qt::Key_Backspace:
      emit backspace_pressed();
      event->accept();
      return;
    case Qt::Key_Return:
    case Qt::Key_Enter: {
      if (suppress_next_enter_activation_) {
        suppress_next_enter_activation_ = false;
        event->accept();
        return;
      }
      const QModelIndex current =
          selectionModel() != nullptr
              ? normalize_to_primary_column(selectionModel()->currentIndex())
              : QModelIndex();
      emit primary_enter_pressed(current);
      event->accept();
      return;
    }
    case Qt::Key_Left:
    case Qt::Key_Right:
      // Horizontal navigation is forbidden.
      event->accept();
      return;
    default:
      break;
  }
  QTableView::keyPressEvent(event);
}

void StructuredListView::closeEditor(
    QWidget* editor,
    QAbstractItemDelegate::EndEditHint hint) {
  QTableView::closeEditor(editor, hint);
  suppress_next_enter_activation_ = true;
  QTimer::singleShot(0, this, [this]() {
    suppress_next_enter_activation_ = false;
  });
}

void StructuredListView::contextMenuEvent(QContextMenuEvent* event) {
  const QPoint vp = event->pos();
  const QModelIndex primary = primary_index_at(vp);
  emit context_menu_requested(primary, vp, event->globalPos());
  event->accept();
}

void StructuredListView::currentChanged(const QModelIndex& current,
                                        const QModelIndex& previous) {
  if (current.isValid() && current.column() != primary_column()) {
    const QModelIndex normalized = normalize_to_primary_column(current);
    if (selectionModel() != nullptr) {
      selectionModel()->setCurrentIndex(normalized,
                                        QItemSelectionModel::NoUpdate);
    }
    return;
  }
  QTableView::currentChanged(current, previous);
}

QModelIndex StructuredListView::moveCursor(CursorAction action,
                                           Qt::KeyboardModifiers mods) {
  if (action == MoveLeft || action == MoveRight) {
    const QModelIndex cur = selectionModel() != nullptr
                                ? selectionModel()->currentIndex()
                                : QModelIndex();
    return normalize_to_primary_column(cur);
  }
  const QModelIndex default_next = QTableView::moveCursor(action, mods);
  if (!default_next.isValid()) return default_next;
  return normalize_to_primary_column(default_next);
}

void StructuredListView::startDrag(Qt::DropActions supported) {
  QTableView::startDrag(supported);
}

void StructuredListView::initViewItemOption(QStyleOptionViewItem* option)
    const {
  QTableView::initViewItemOption(option);
  // QTableView::initViewItemOption unconditionally forces this to true,
  // which makes QCommonStyle::drawPrimitive(PE_PanelItemViewRow) paint a
  // cell-wide palette.Highlight fill on any selected cell *before* the
  // delegate runs. Our delegate owns selection visuals via a narrow chip;
  // clearing the flag suppresses that prefill path entirely.
  option->showDecorationSelected = false;
}

}  // namespace z7::ui::widgets
