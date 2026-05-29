// src/ui/widgets/include/structured_list_proxy.h
// Role: Sort/filter proxy for StructuredListView.
//
// The proxy is the only source of sort truth for a StructuredListView. Source
// models must expose typed sort keys via `kSortKeyRole` for the active column;
// they may optionally expose `kSortGroupRole` to pin a subset of rows (e.g. a
// "parent directory" link) to the top regardless of sort order.

#pragma once

#include <QSortFilterProxyModel>

namespace z7::ui::widgets {

class StructuredListSortFilterProxy : public QSortFilterProxyModel {
  Q_OBJECT
 public:
  // Per-index typed value used for column comparison. Source models should
  // return a QVariant of an appropriate numeric/string/date type for the given
  // column so ordering is correct independent of display formatting.
  static constexpr int kSortKeyRole = Qt::UserRole + 1000;

  // Optional small integer role. Rows with a lower group value sort before
  // rows with a higher group value, regardless of `sortOrder()`. Used to pin
  // header-like items (e.g. the parent directory link) above everything else.
  static constexpr int kSortGroupRole = Qt::UserRole + 1001;

  // Optional typed secondary key consulted only when the primary sort key is
  // equal. The proxy keeps this tie-break direction-independent so stable
  // source ordering survives both ascending and descending sorts.
  static constexpr int kSortTieBreakRole = Qt::UserRole + 1002;

  explicit StructuredListSortFilterProxy(QObject* parent = nullptr);
  ~StructuredListSortFilterProxy() override;

  void set_natural_compare(bool enabled);
  bool natural_compare() const { return natural_compare_; }

 protected:
  bool lessThan(const QModelIndex& left,
                const QModelIndex& right) const override;

 private:
  bool natural_compare_ = true;
};

}  // namespace z7::ui::widgets
