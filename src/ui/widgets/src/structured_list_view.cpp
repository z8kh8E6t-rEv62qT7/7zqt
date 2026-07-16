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
#include <QRubberBand>
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

        rubber_band_ = new QRubberBand(QRubberBand::Rectangle, viewport());
        rubber_band_->setAttribute(Qt::WA_TransparentForMouseEvents);
        rubber_band_->hide();

        rubber_band_auto_scroll_timer_ = new QTimer(this);
        rubber_band_auto_scroll_timer_->setInterval(30);
        connect(rubber_band_auto_scroll_timer_, &QTimer::timeout, this, [this]() { auto_scroll_rubber_band(); });
    }

    StructuredListView::~StructuredListView() = default;

    void StructuredListView::setModel(QAbstractItemModel* model) {
        cancel_rubber_band_selection();
        QTableView::setModel(model);
    }

    void StructuredListView::reset() {
        cancel_rubber_band_selection();
        left_pressed_ = false;
        drag_in_progress_ = false;
        defer_single_collapse_ = false;
        press_primary_index_ = QPersistentModelIndex();
        QTableView::reset();
    }

    void StructuredListView::set_config(StructuredListConfig config) {
        config_ = std::move(config);
        apply_config_to_view();
    }

    void StructuredListView::apply_config_to_view() {
        horizontalHeader()->setVisible(config_.show_header);
        setSortingEnabled(config_.sorting_enabled);

        if (model() != nullptr) {
            int const cols = std::min<int>(static_cast<int>(config_.columns.size()), model()->columnCount());
            for (int i = 0; i < cols; ++i) {
                auto const& col = config_.columns[static_cast<size_t>(i)];
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

    bool StructuredListView::is_primary_column(QModelIndex const& index) const {
        return index.isValid() && index.column() == primary_column();
    }

    QModelIndex StructuredListView::normalize_to_primary_column(QModelIndex const& index) const {
        if (!index.isValid() || model() == nullptr)
            return {};
        if (index.column() == primary_column())
            return index;
        return model()->index(index.row(), primary_column(), index.parent());
    }

    QModelIndex StructuredListView::hover_index() const {
        return hover_index_;
    }

    void StructuredListView::refresh_hover_from_cursor() {
        if (model() == nullptr || viewport() == nullptr) {
            clear_hover_index();
            return;
        }

        QPoint const viewport_pos = viewport()->mapFromGlobal(QCursor::pos());
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

    void StructuredListView::set_hover_index(QModelIndex const& index) {
        QPersistentModelIndex const normalized(index);
        if (normalized == hover_index_)
            return;
        QPersistentModelIndex const old = hover_index_;
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

    void StructuredListView::clear_hover_index() {
        set_hover_index({});
    }

    QModelIndex StructuredListView::primary_index_at(QPoint const& p) const {
        QModelIndex const hit = indexAt(p);
        if (!hit.isValid())
            return {};
        return normalize_to_primary_column(hit);
    }

    bool StructuredListView::point_is_on_primary(QPoint const& p) const {
        QModelIndex const hit = indexAt(p);
        return hit.isValid() && hit.column() == primary_column();
    }

    void StructuredListView::blank_selection() {
        if (auto* sel = selectionModel()) {
            bool const had = sel->hasSelection() || sel->currentIndex().isValid();
            sel->clearSelection();
            sel->clearCurrentIndex();
            if (had)
                emit selection_blanked();
        }
        selection_anchor_ = QPersistentModelIndex();
    }

    bool StructuredListView::can_rubber_band_select() const {
        return model() != nullptr
            && selectionModel() != nullptr
            && selectionMode() != QAbstractItemView::SingleSelection
            && selectionMode() != QAbstractItemView::NoSelection;
    }

    QPoint StructuredListView::viewport_to_content(QPoint const& viewport_pos) const {
        return viewport_pos + QPoint(horizontalHeader()->offset(), verticalHeader()->offset());
    }

    void StructuredListView::begin_rubber_band_selection(QMouseEvent const* event) {
        if (event == nullptr)
            return;

        rubber_band_candidate_ = true;
        rubber_band_allowed_ = can_rubber_band_select();
        rubber_band_active_ = false;
        rubber_band_additive_ =
            rubber_band_allowed_
            && (event->modifiers().testFlag(Qt::ControlModifier) || event->modifiers().testFlag(Qt::MetaModifier));
        rubber_band_origin_content_ = viewport_to_content(event->pos());
        rubber_band_current_viewport_pos_ = event->pos();
        rubber_band_base_selection_ = selectionModel() != nullptr ? selectionModel()->selection() : QItemSelection();
        selection_anchor_ = QPersistentModelIndex();

        if (!rubber_band_additive_) {
            blank_selection();
        }
    }

    void StructuredListView::update_rubber_band_selection(QPoint const& viewport_pos) {
        if (!rubber_band_candidate_ || !rubber_band_allowed_)
            return;

        rubber_band_current_viewport_pos_ = viewport_pos;
        if (!rubber_band_active_) {
            int const distance = (viewport_pos - press_viewport_pos_).manhattanLength();
            if (distance < QApplication::startDragDistance())
                return;
            rubber_band_active_ = true;
            rubber_band_->show();
            if (hasAutoScroll()) {
                rubber_band_auto_scroll_timer_->start();
            }
        }
        update_rubber_band_geometry_and_selection();
    }

    void StructuredListView::finish_rubber_band_selection(QPoint const& viewport_pos) {
        if (!rubber_band_candidate_)
            return;
        rubber_band_current_viewport_pos_ = viewport_pos;
        if (rubber_band_active_) {
            update_rubber_band_geometry_and_selection();
        }
        cancel_rubber_band_selection();
    }

    void StructuredListView::cancel_rubber_band_selection() {
        if (rubber_band_auto_scroll_timer_ != nullptr) {
            rubber_band_auto_scroll_timer_->stop();
        }
        if (rubber_band_ != nullptr) {
            rubber_band_->hide();
        }
        rubber_band_candidate_ = false;
        rubber_band_allowed_ = false;
        rubber_band_active_ = false;
        rubber_band_additive_ = false;
        rubber_band_base_selection_.clear();
    }

    void StructuredListView::update_rubber_band_geometry_and_selection() {
        if (!rubber_band_active_ || rubber_band_ == nullptr || model() == nullptr || selectionModel() == nullptr)
            return;

        QPoint const scroll_offset(horizontalHeader()->offset(), verticalHeader()->offset());
        QPoint const current_content = viewport_to_content(rubber_band_current_viewport_pos_);
        QRect const content_rect = QRect(rubber_band_origin_content_, current_content).normalized();
        QRect const viewport_rect = content_rect.translated(-scroll_offset).intersected(viewport()->rect());
        rubber_band_->setGeometry(viewport_rect);

        QItemSelection band_selection;
        int const primary = primary_column();
        int const row_count = model()->rowCount(rootIndex());
        if (primary >= 0
            && primary < model()->columnCount(rootIndex())
            && row_count > 0
            && !horizontalHeader()->isSectionHidden(primary)) {
            int const primary_x = horizontalHeader()->sectionPosition(primary);
            int const primary_width = horizontalHeader()->sectionSize(primary);
            QRect const primary_column_rect(primary_x, content_rect.top(), primary_width, content_rect.height());

            if (primary_x >= 0 && content_rect.intersects(primary_column_rect)) {
                auto first_intersecting_row = [this, row_count, &content_rect]() {
                    int low = 0;
                    int high = row_count;
                    while (low < high) {
                        int const middle = low + (high - low) / 2;
                        int const top = verticalHeader()->sectionPosition(middle);
                        int const bottom = top + verticalHeader()->sectionSize(middle) - 1;
                        if (top < 0 || bottom < content_rect.top()) {
                            low = middle + 1;
                        } else {
                            high = middle;
                        }
                    }
                    return low;
                };
                auto row_after_selection = [this, row_count, &content_rect]() {
                    int low = 0;
                    int high = row_count;
                    while (low < high) {
                        int const middle = low + (high - low) / 2;
                        int const top = verticalHeader()->sectionPosition(middle);
                        if (top >= 0 && top <= content_rect.bottom()) {
                            low = middle + 1;
                        } else {
                            high = middle;
                        }
                    }
                    return low;
                };

                int const first_row = first_intersecting_row();
                int const row_after = row_after_selection();
                if (first_row >= 0 && first_row < row_after && row_after <= row_count) {
                    QModelIndex const first = model()->index(first_row, primary, rootIndex());
                    QModelIndex const last = model()->index(row_after - 1, primary, rootIndex());
                    if (first.isValid() && last.isValid()) {
                        band_selection.select(first, last);
                    }
                }
            }
        }

        QItemSelection target_selection = rubber_band_additive_ ? rubber_band_base_selection_ : QItemSelection();
        target_selection.merge(band_selection, QItemSelectionModel::Select);
        selectionModel()->select(target_selection, QItemSelectionModel::ClearAndSelect);
    }

    void StructuredListView::auto_scroll_rubber_band() {
        if (!rubber_band_active_ || !hasAutoScroll())
            return;

        int const margin = std::max(1, autoScrollMargin());
        auto scroll_for_position = [margin](QScrollBar* bar, int position, int extent) {
            if (bar == nullptr || bar->minimum() == bar->maximum())
                return false;
            int delta = 0;
            if (position < margin) {
                delta = -std::max(1, bar->singleStep());
            } else if (position >= extent - margin) {
                delta = std::max(1, bar->singleStep());
            }
            if (delta == 0)
                return false;
            int const old_value = bar->value();
            bar->setValue(old_value + delta);
            return bar->value() != old_value;
        };

        bool const horizontal_changed =
            scroll_for_position(horizontalScrollBar(), rubber_band_current_viewport_pos_.x(), viewport()->width());
        bool const vertical_changed =
            scroll_for_position(verticalScrollBar(), rubber_band_current_viewport_pos_.y(), viewport()->height());
        if (horizontal_changed || vertical_changed) {
            update_rubber_band_geometry_and_selection();
        }
    }

    void StructuredListView::select_single(QModelIndex const& primary) {
        auto* sel = selectionModel();
        if (sel == nullptr || !primary.isValid())
            return;
        sel->setCurrentIndex(primary, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Current);
        selection_anchor_ = primary;
    }

    void StructuredListView::select_toggle(QModelIndex const& primary) {
        auto* sel = selectionModel();
        if (sel == nullptr || !primary.isValid())
            return;
        QItemSelection one(primary, primary);
        sel->select(one, QItemSelectionModel::Toggle);
        sel->setCurrentIndex(primary, QItemSelectionModel::NoUpdate);
        if (!selection_anchor_.isValid())
            selection_anchor_ = primary;
    }

    void StructuredListView::select_range_to(QModelIndex const& primary) {
        auto* sel = selectionModel();
        if (sel == nullptr || !primary.isValid() || model() == nullptr)
            return;
        QModelIndex anchor = selection_anchor_;
        if (!anchor.isValid())
            anchor = primary;
        int const top = std::min(anchor.row(), primary.row());
        int const bottom = std::max(anchor.row(), primary.row());
        QModelIndex const tl = model()->index(top, primary_column(), primary.parent());
        QModelIndex const br = model()->index(bottom, primary_column(), primary.parent());
        QItemSelection range(tl, br);
        sel->select(range, QItemSelectionModel::ClearAndSelect);
        sel->setCurrentIndex(primary, QItemSelectionModel::NoUpdate);
    }

    void StructuredListView::mousePressEvent(QMouseEvent* event) {
        QPoint const pos = event->pos();

        if (event->button() == Qt::RightButton) {
            // Right click never mutates selection; contextMenuEvent handles the menu.
            event->accept();
            return;
        }
        if (event->button() != Qt::LeftButton) {
            QTableView::mousePressEvent(event);
            return;
        }

        setFocus(Qt::MouseFocusReason);
        left_pressed_ = true;
        press_viewport_pos_ = pos;
        defer_single_collapse_ = false;

        if (!point_is_on_primary(pos)) {
            press_primary_index_ = QPersistentModelIndex();
            begin_rubber_band_selection(event);
            event->accept();
            return;
        }

        QModelIndex const primary = primary_index_at(pos);
        press_primary_index_ = primary;

        Qt::KeyboardModifiers const mods = event->modifiers();
        auto* sel = selectionModel();
        if (mods & Qt::ControlModifier) {
            select_toggle(primary);
        } else if (mods & Qt::ShiftModifier) {
            select_range_to(primary);
        } else {
            if (sel != nullptr && sel->isSelected(primary) && sel->selection().indexes().size() > 1) {
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
            if (rubber_band_candidate_) {
                finish_rubber_band_selection(event->pos());
                left_pressed_ = false;
                drag_in_progress_ = false;
                defer_single_collapse_ = false;
                press_primary_index_ = QPersistentModelIndex();
                event->accept();
                return;
            }
            bool const was_pressed = left_pressed_;
            QPersistentModelIndex const primary = press_primary_index_;
            bool const drag_happened = drag_in_progress_;
            bool const deferred = defer_single_collapse_;
            left_pressed_ = false;
            drag_in_progress_ = false;
            defer_single_collapse_ = false;
            press_primary_index_ = QPersistentModelIndex();

            if (was_pressed
                && !drag_happened
                && primary.isValid()
                && !(event->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier))) {
                if (deferred) {
                    select_single(primary);
                }
                emit primary_clicked(primary, event->modifiers());
            }
        }
        QTableView::mouseReleaseEvent(event);
    }

    void StructuredListView::mouseMoveEvent(QMouseEvent* event) {
        if (rubber_band_candidate_) {
            if (event->buttons().testFlag(Qt::LeftButton)) {
                update_rubber_band_selection(event->pos());
            } else {
                cancel_rubber_band_selection();
            }
            event->accept();
            return;
        }
        if (left_pressed_ && !drag_in_progress_ && press_primary_index_.isValid()) {
            int const dist = (event->pos() - press_viewport_pos_).manhattanLength();
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
        QModelIndex const primary = primary_index_at(event->pos());
        if (primary.isValid()) {
            emit primary_double_clicked(primary);
        }
        event->accept();
    }

    void StructuredListView::leaveEvent(QEvent* event) {
        clear_hover_index();
        QTableView::leaveEvent(event);
    }

    void StructuredListView::hideEvent(QHideEvent* event) {
        cancel_rubber_band_selection();
        left_pressed_ = false;
        drag_in_progress_ = false;
        defer_single_collapse_ = false;
        press_primary_index_ = QPersistentModelIndex();
        QTableView::hideEvent(event);
    }

    void StructuredListView::keyPressEvent(QKeyEvent* event) {
        if (event->key() == Qt::Key_Escape && rubber_band_candidate_) {
            cancel_rubber_band_selection();
            left_pressed_ = false;
            event->accept();
            return;
        }
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
            case Qt::Key_Enter:
                {
                    if (suppress_next_enter_activation_) {
                        suppress_next_enter_activation_ = false;
                        event->accept();
                        return;
                    }
                    QModelIndex const current = selectionModel() != nullptr
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

    void StructuredListView::closeEditor(QWidget* editor, QAbstractItemDelegate::EndEditHint hint) {
        QTableView::closeEditor(editor, hint);
        suppress_next_enter_activation_ = true;
        QTimer::singleShot(0, this, [this]() { suppress_next_enter_activation_ = false; });
    }

    void StructuredListView::contextMenuEvent(QContextMenuEvent* event) {
        QPoint const vp = event->pos();
        QModelIndex const primary = primary_index_at(vp);
        emit context_menu_requested(primary, vp, event->globalPos());
        event->accept();
    }

    void StructuredListView::currentChanged(QModelIndex const& current, QModelIndex const& previous) {
        if (current.isValid() && current.column() != primary_column()) {
            QModelIndex const normalized = normalize_to_primary_column(current);
            if (selectionModel() != nullptr) {
                selectionModel()->setCurrentIndex(normalized, QItemSelectionModel::NoUpdate);
            }
            return;
        }
        QTableView::currentChanged(current, previous);
    }

    QModelIndex StructuredListView::moveCursor(CursorAction action, Qt::KeyboardModifiers mods) {
        if (action == MoveLeft || action == MoveRight) {
            QModelIndex const cur = selectionModel() != nullptr ? selectionModel()->currentIndex() : QModelIndex();
            return normalize_to_primary_column(cur);
        }
        QModelIndex const default_next = QTableView::moveCursor(action, mods);
        if (!default_next.isValid())
            return default_next;
        return normalize_to_primary_column(default_next);
    }

    void StructuredListView::startDrag(Qt::DropActions supported) {
        QTableView::startDrag(supported);
    }

    void StructuredListView::initViewItemOption(QStyleOptionViewItem* option) const {
        QTableView::initViewItemOption(option);
        // QTableView::initViewItemOption unconditionally forces this to true,
        // which makes QCommonStyle::drawPrimitive(PE_PanelItemViewRow) paint a
        // cell-wide palette.Highlight fill on any selected cell *before* the
        // delegate runs. Our delegate owns selection visuals via a narrow chip;
        // clearing the flag suppresses that prefill path entirely.
        option->showDecorationSelected = false;
    }

} // namespace z7::ui::widgets
