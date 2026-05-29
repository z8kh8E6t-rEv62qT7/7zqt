// src/ui/widgets/src/structured_list_delegate.cpp
// Role: Paint cells for StructuredListView with primary-column-owned visuals.

#include "structured_list_delegate.h"

#include <QApplication>
#include <QFontMetrics>
#include <QIcon>
#include <QPainter>
#include <QStyle>
#include <QStyleOptionViewItem>

#include <algorithm>

#include "structured_list_config.h"
#include "structured_list_view.h"

namespace z7::ui::widgets {

StructuredListDelegate::StructuredListDelegate(StructuredListView* owner_view)
    : QStyledItemDelegate(owner_view), owner_view_(owner_view) {}

StructuredListDelegate::~StructuredListDelegate() = default;

const StructuredListConfig& StructuredListDelegate::config() const {
  return owner_view_->config();
}

QRect StructuredListDelegate::primary_text_rect(
    const QStyleOptionViewItem& option, const QModelIndex& index) const {
  const auto& style = config().style;
  const QString text = index.data(Qt::DisplayRole).toString();
  const QFontMetrics fm(option.font);

  QRect cell = option.rect;
  // Reserve space for decoration (icon) on the left.
  int icon_w = 0;
  const QVariant decoration = index.data(Qt::DecorationRole);
  if (decoration.isValid()) {
    const QSize icon_size =
        option.decorationSize.isValid() ? option.decorationSize : QSize(16, 16);
    icon_w = icon_size.width() + style.primary_text_padding_h;
  }

  const int pad_h = style.primary_text_padding_h;
  const int pad_v = style.primary_text_padding_v;
  const int text_w = fm.horizontalAdvance(text);
  const int h = fm.height() + 2 * pad_v;
  // Extend the chip 1px to the left so it reads as hugging the text
  // (without the sliver of untinted cell between the left cell edge / icon
  // and the chip). The extra pixel is absorbed into width to keep the right
  // edge where it was.
  const int x = cell.left() + pad_h + icon_w - 1;
  const int y = cell.top() + (cell.height() - h) / 2;
  const int available = cell.width() - pad_h - icon_w - pad_h + 1;
  const int w = std::min(text_w + 2 * pad_h + 1, std::max(0, available));
  return QRect(x, y, w, h);
}

void StructuredListDelegate::paint(QPainter* painter,
                                   const QStyleOptionViewItem& option,
                                   const QModelIndex& index) const {
  QStyleOptionViewItem opt = option;
  initStyleOption(&opt, index);

  const auto& cfg = config();
  const auto& style = cfg.style;
  const bool is_primary = index.column() == cfg.primary_interactive_column;
  const bool is_selected = (opt.state & QStyle::State_Selected) != 0;

  const QPersistentModelIndex hover = owner_view_->hover_index();
  const bool row_is_hovered = hover.isValid() && hover.row() == index.row();
  const bool is_primary_hover = is_primary && row_is_hovered;

  // Strip framework-drawn selection/focus on non-primary columns.
  if (!is_primary) {
    opt.state &= ~QStyle::State_Selected;
  }
  opt.state &= ~QStyle::State_HasFocus;

  painter->save();
  painter->setClipRect(opt.rect);

  // Row-wide faint hover (only when hover lives on the primary column).
  if (style.row_hover_bg.isValid() && row_is_hovered) {
    painter->fillRect(opt.rect, style.row_hover_bg);
  }

  // Primary-column chip: selection vs hover. Selection wins.
  if (is_primary) {
    const QRect chip = primary_text_rect(opt, index);
    if (is_selected && style.primary_selected_bg.isValid()) {
      painter->fillRect(chip, style.primary_selected_bg);
    } else if (is_primary_hover && style.primary_hover_bg.isValid()) {
      painter->fillRect(chip, style.primary_hover_bg);
    }
  }

  // Icon (primary column only).
  QRect content_rect =
      opt.rect.adjusted(style.primary_text_padding_h, 0,
                        -style.primary_text_padding_h, 0);
  if (is_primary) {
    const QVariant decoration = index.data(Qt::DecorationRole);
    if (decoration.isValid()) {
      const QIcon icon = qvariant_cast<QIcon>(decoration);
      const QSize icon_size =
          opt.decorationSize.isValid() ? opt.decorationSize : QSize(16, 16);
      const QRect icon_rect(
          content_rect.left(),
          content_rect.top() + (content_rect.height() - icon_size.height()) / 2,
          icon_size.width(),
          icon_size.height());
      icon.paint(painter, icon_rect, Qt::AlignCenter,
                 (opt.state & QStyle::State_Enabled) != 0 ? QIcon::Normal
                                                          : QIcon::Disabled,
                 is_selected ? QIcon::On : QIcon::Off);
      content_rect.setLeft(icon_rect.right() + style.primary_text_padding_h);
    }
  }

  // Text.
  const QString text = index.data(Qt::DisplayRole).toString();
  if (!text.isEmpty()) {
    QColor text_color = opt.palette.color(QPalette::Text);
    if (is_primary && is_selected && style.primary_selected_text.isValid()) {
      text_color = style.primary_selected_text;
    }
    painter->setPen(text_color);

    Qt::Alignment align = opt.displayAlignment;
    if (index.column() >= 0 &&
        index.column() < static_cast<int>(cfg.columns.size())) {
      align = cfg.columns[static_cast<size_t>(index.column())].alignment;
    }
    const QString elided =
        opt.fontMetrics.elidedText(text, Qt::ElideRight, content_rect.width());
    painter->drawText(content_rect, static_cast<int>(align), elided);
  }

  // Optional grid line.
  if (style.grid_line.isValid()) {
    painter->setPen(style.grid_line);
    painter->drawLine(opt.rect.bottomLeft(), opt.rect.bottomRight());
  }

  painter->restore();
}

QSize StructuredListDelegate::sizeHint(const QStyleOptionViewItem& option,
                                       const QModelIndex& index) const {
  QSize base = QStyledItemDelegate::sizeHint(option, index);
  const int h = config().style.row_height_hint;
  if (h > 0) {
    base.setHeight(std::max(base.height(), h));
  }
  return base;
}

}  // namespace z7::ui::widgets
