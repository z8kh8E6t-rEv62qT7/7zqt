// src/ui/widgets/include/structured_list_delegate.h
// Role: Paint cells for StructuredListView.
//
// The delegate strips the framework's default selected/focus painting on
// non-primary columns, paints a text-hugging hover chip / selected chip on the
// primary column, and optionally paints row-wide faint hover and bottom grid
// lines. Nothing is application specific; all visuals come from
// StructuredListStyle.

#pragma once

#include <QRect>
#include <QStyledItemDelegate>

namespace z7::ui::widgets {

struct StructuredListConfig;
class StructuredListView;

class StructuredListDelegate : public QStyledItemDelegate {
  Q_OBJECT
 public:
  explicit StructuredListDelegate(StructuredListView* owner_view);
  ~StructuredListDelegate() override;

  void paint(QPainter* painter,
             const QStyleOptionViewItem& option,
             const QModelIndex& index) const override;
  QSize sizeHint(const QStyleOptionViewItem& option,
                 const QModelIndex& index) const override;

  // Exposed for subclasses / tests.
  QRect primary_text_rect(const QStyleOptionViewItem& option,
                          const QModelIndex& index) const;

 private:
  const StructuredListConfig& config() const;
  StructuredListView* owner_view_;
};

}  // namespace z7::ui::widgets
