// src/ui/widgets/include/structured_list_view.h
// Role: Reusable QTableView with "primary column owns interaction" semantics.
//
// One column (`primary_interactive_column`) owns item interaction: selection,
// hover highlight, double-click activation, and item drag initiation.
// Non-primary columns are display-only metadata, but act like background for
// rubber-band selection. Arrow keys keep the current index pinned to the
// primary column. Right-click raises a context menu signal without mutating
// selection. Nothing in this file is application specific.

#pragma once

#include <QItemSelection>
#include <QPersistentModelIndex>
#include <QPoint>
#include <QTableView>

#include "structured_list_config.h"

class QContextMenuEvent;
class QHideEvent;
class QKeyEvent;
class QMouseEvent;
class QRubberBand;
class QTimer;

namespace z7::ui::widgets {

    class StructuredListDelegate;

    class StructuredListView : public QTableView {
        Q_OBJECT
    public:
        explicit StructuredListView(QWidget* parent = nullptr);
        ~StructuredListView() override;

        void setModel(QAbstractItemModel* model) override;
        void reset() override;

        // Apply configuration. Safe to call after setModel(); columns and header
        // visuals are re-applied each time.
        void set_config(StructuredListConfig config);

        StructuredListConfig const& config() const { return config_; }

        int primary_column() const { return config_.primary_interactive_column; }

        bool is_primary_column(QModelIndex const& index) const;
        QModelIndex normalize_to_primary_column(QModelIndex const& index) const;

        // Index currently under the mouse pointer, constrained to the primary
        // column. Invalid when the pointer is outside the view or not on primary.
        QModelIndex hover_index() const;
        void refresh_hover_from_cursor();

    signals:
        // All indices are in this view's model space (typically the proxy).

        // A completed left-click on the primary column without drag/modifier keys.
        void primary_clicked(QModelIndex const& index);
        // A double-click on the primary column.
        void primary_double_clicked(QModelIndex const& index);
        // Return/Enter pressed while the view has focus. `index` is the current
        // index normalized to the primary column (may be invalid).
        void primary_enter_pressed(QModelIndex const& index);
        // Context menu requested at `viewport_pos`. `index_or_invalid` is the
        // primary-column index of the row under the cursor, or invalid when the
        // pointer is on whitespace. This signal does not mutate selection.
        void context_menu_requested(QModelIndex const& index_or_invalid,
                                    QPoint const& viewport_pos,
                                    QPoint const& global_pos);
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
        void hideEvent(QHideEvent* event) override;
        void keyPressEvent(QKeyEvent* event) override;
        void closeEditor(QWidget* editor, QAbstractItemDelegate::EndEditHint hint) override;
        void contextMenuEvent(QContextMenuEvent* event) override;
        void currentChanged(QModelIndex const& current, QModelIndex const& previous) override;
        QModelIndex moveCursor(CursorAction action, Qt::KeyboardModifiers modifiers) override;
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
        void set_hover_index(QModelIndex const& index);
        void clear_hover_index();
        void select_single(QModelIndex const& primary);
        void select_toggle(QModelIndex const& primary);
        void select_range_to(QModelIndex const& primary);
        void blank_selection();
        bool can_rubber_band_select() const;
        void begin_rubber_band_selection(QMouseEvent const* event);
        void update_rubber_band_selection(QPoint const& viewport_pos);
        void finish_rubber_band_selection(QPoint const& viewport_pos);
        void cancel_rubber_band_selection();
        void update_rubber_band_geometry_and_selection();
        void auto_scroll_rubber_band();
        QPoint viewport_to_content(QPoint const& viewport_pos) const;
        QModelIndex primary_index_at(QPoint const& viewport_pos) const;
        bool point_is_on_primary(QPoint const& viewport_pos) const;

        StructuredListConfig config_;
        StructuredListDelegate* delegate_ = nullptr;
        QPersistentModelIndex hover_index_;
        QPersistentModelIndex selection_anchor_;
        QPoint press_viewport_pos_;
        QPersistentModelIndex press_primary_index_;
        QRubberBand* rubber_band_ = nullptr;
        QTimer* rubber_band_auto_scroll_timer_ = nullptr;
        QPoint rubber_band_origin_content_;
        QPoint rubber_band_current_viewport_pos_;
        QItemSelection rubber_band_base_selection_;
        bool left_pressed_ = false;
        bool drag_in_progress_ = false;
        bool rubber_band_candidate_ = false;
        bool rubber_band_allowed_ = false;
        bool rubber_band_active_ = false;
        bool rubber_band_additive_ = false;
        bool suppress_next_enter_activation_ = false;
        // Deferred click handling: when a press lands on an already-selected primary
        // item, we do not collapse selection on press (to preserve drag). We collapse
        // on release if no drag happened.
        bool defer_single_collapse_ = false;
    };

} // namespace z7::ui::widgets
