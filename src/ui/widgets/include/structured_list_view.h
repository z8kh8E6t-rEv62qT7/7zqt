// src/ui/widgets/include/structured_list_view.h
// Role: Reusable QTableView with "primary column owns interaction" semantics.
//
// One column (`primary_interactive_column`) owns all interaction: selection,
// hover highlight, double-click activation, drag initiation. Non-primary
// columns are display-only metadata. Arrow keys keep the current index pinned
// to the primary column. Right-click raises a context menu signal without
// mutating selection. Nothing in this file is application specific.

#pragma once

#include <QPersistentModelIndex>
#include <QPoint>
#include <QTableView>

#include "structured_list_config.h"

class QContextMenuEvent;
class QKeyEvent;
class QMouseEvent;

namespace z7::ui::widgets {

class StructuredListDelegate;

class StructuredListView : public QTableView {
  Q_OBJECT
 public:
  explicit StructuredListView(QWidget* parent = nullptr);
  ~StructuredListView() override;

  // Apply configuration. Safe to call after setModel(); columns and header
  // visuals are re-applied each time.
  void set_config(StructuredListConfig config);
  const StructuredListConfig& config() const { return config_; }

  int primary_column() const { return config_.primary_interactive_column; }
  bool is_primary_column(const QModelIndex& index) const;
  QModelIndex normalize_to_primary_column(const QModelIndex& index) const;

  // Index currently under the mouse pointer, constrained to the primary
  // column. Invalid when the pointer is outside the view or not on primary.
  QModelIndex hover_index() const;
  void refresh_hover_from_cursor();

 signals:
  // All indices are in this view's model space (typically the proxy).

  // A completed left-click on the primary column without drag/modifier keys.
  void primary_clicked(const QModelIndex& index);
  // A double-click on the primary column.
  void primary_double_clicked(const QModelIndex& index);
  // Return/Enter pressed while the view has focus. `index` is the current
  // index normalized to the primary column (may be invalid).
  void primary_enter_pressed(const QModelIndex& index);
  // Context menu requested at `viewport_pos`. `index_or_invalid` is the
  // primary-column index of the row under the cursor, or invalid when the
  // pointer is on whitespace. This signal does not mutate selection.
  void context_menu_requested(const QModelIndex& index_or_invalid,
                              const QPoint& viewport_pos,
                              const QPoint& global_pos);
  // Fired whenever a mouse press on whitespace / non-primary clears all
  // selection. The selection is already cleared when this fires.
  void selection_blanked();
  // Delete key pressed while the view has focus.
  void delete_pressed();
  // Backspace pressed while the view has focus.
  void backspace_pressed();

 protected:
  void mousePressEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mouseDoubleClickEvent(QMouseEvent* event) override;
  void leaveEvent(QEvent* event) override;
  void keyPressEvent(QKeyEvent* event) override;
  void closeEditor(QWidget* editor,
                   QAbstractItemDelegate::EndEditHint hint) override;
  void contextMenuEvent(QContextMenuEvent* event) override;
  void currentChanged(const QModelIndex& current,
                      const QModelIndex& previous) override;
  QModelIndex moveCursor(CursorAction action,
                         Qt::KeyboardModifiers modifiers) override;
  // QTableView::initViewItemOption hard-codes showDecorationSelected=true,
  // which makes QCommonStyle::drawPrimitive(PE_PanelItemViewRow) fill the
  // entire selected cell with palette.Highlight before the delegate paints.
  // Override to keep selection visuals confined to the delegate's narrow chip.
  void initViewItemOption(QStyleOptionViewItem* option) const override;
  // Subclasses override to materialize custom drag payloads. The base forwards
  // to QTableView::startDrag.
  void startDrag(Qt::DropActions supported_actions) override;

 private:
  void apply_config_to_view();
  void set_hover_index(const QModelIndex& index);
  void clear_hover_index();
  void select_single(const QModelIndex& primary);
  void select_toggle(const QModelIndex& primary);
  void select_range_to(const QModelIndex& primary);
  void blank_selection();
  QModelIndex primary_index_at(const QPoint& viewport_pos) const;
  bool point_is_on_primary(const QPoint& viewport_pos) const;

  StructuredListConfig config_;
  StructuredListDelegate* delegate_ = nullptr;
  QPersistentModelIndex hover_index_;
  QPersistentModelIndex selection_anchor_;
  QPoint press_viewport_pos_;
  QPersistentModelIndex press_primary_index_;
  bool left_pressed_ = false;
  bool drag_in_progress_ = false;
  bool suppress_next_enter_activation_ = false;
  // Deferred click handling: when a press lands on an already-selected primary
  // item, we do not collapse selection on press (to preserve drag). We collapse
  // on release if no drag happened.
  bool defer_single_collapse_ = false;
};

}  // namespace z7::ui::widgets
