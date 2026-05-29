// src/ui/widgets/src/structured_list_proxy.cpp
// Role: Sort comparator for StructuredListView.

#include "structured_list_proxy.h"

#include <QCollator>
#include <QDateTime>
#include <QMetaType>
#include <QVariant>

#include <limits>
#include <optional>

namespace z7::ui::widgets {
namespace {

const QCollator& NaturalCollator() {
  static thread_local QCollator collator = [] {
    QCollator c;
    c.setNumericMode(true);
    c.setCaseSensitivity(Qt::CaseInsensitive);
    c.setIgnorePunctuation(false);
    return c;
  }();
  return collator;
}

QVariant SortValueFor(const QModelIndex& index) {
  const QVariant typed = index.data(StructuredListSortFilterProxy::kSortKeyRole);
  if (typed.isValid()) return typed;
  return index.data(Qt::DisplayRole);
}

bool IsUnsignedIntegralType(int type_id) {
  switch (type_id) {
    case QMetaType::Bool:
    case QMetaType::UChar:
    case QMetaType::UShort:
    case QMetaType::UInt:
    case QMetaType::ULong:
    case QMetaType::ULongLong:
      return true;
    default:
      return false;
  }
}

bool IsSignedIntegralType(int type_id) {
  switch (type_id) {
    case QMetaType::SChar:
    case QMetaType::Char:
    case QMetaType::Short:
    case QMetaType::Int:
    case QMetaType::Long:
    case QMetaType::LongLong:
      return true;
    default:
      return false;
  }
}

bool IsFloatingPointType(int type_id) {
  switch (type_id) {
    case QMetaType::Float:
    case QMetaType::Double:
      return true;
    default:
      return false;
  }
}

int CompareOrderedValues(qulonglong left, qulonglong right) {
  if (left == right) {
    return 0;
  }
  return left < right ? -1 : 1;
}

int CompareOrderedValues(qlonglong left, qlonglong right) {
  if (left == right) {
    return 0;
  }
  return left < right ? -1 : 1;
}

int CompareOrderedValues(double left, double right) {
  if (left == right) {
    return 0;
  }
  return left < right ? -1 : 1;
}

int CompareOrderedValues(const QDateTime& left, const QDateTime& right) {
  if (left == right) {
    return 0;
  }
  return left < right ? -1 : 1;
}

std::optional<int> CompareIntegralVariants(const QVariant& left,
                                           int left_type,
                                           const QVariant& right,
                                           int right_type) {
  if (IsUnsignedIntegralType(left_type) && IsUnsignedIntegralType(right_type)) {
    return CompareOrderedValues(left.toULongLong(), right.toULongLong());
  }
  if (!IsUnsignedIntegralType(left_type) && !IsUnsignedIntegralType(right_type)) {
    return CompareOrderedValues(left.toLongLong(), right.toLongLong());
  }

  if (IsUnsignedIntegralType(left_type)) {
    const qulonglong lhs = left.toULongLong();
    const qlonglong rhs = right.toLongLong();
    if (rhs < 0) {
      return 1;
    }
    if (lhs > static_cast<qulonglong>(std::numeric_limits<qlonglong>::max())) {
      return 1;
    }
    return CompareOrderedValues(lhs, static_cast<qulonglong>(rhs));
  }

  const qlonglong lhs = left.toLongLong();
  const qulonglong rhs = right.toULongLong();
  if (lhs < 0) {
    return -1;
  }
  if (rhs > static_cast<qulonglong>(std::numeric_limits<qlonglong>::max())) {
    return -1;
  }
  return CompareOrderedValues(static_cast<qulonglong>(lhs), rhs);
}

std::optional<int> CompareVariantValues(const QVariant& left,
                                        const QVariant& right,
                                        bool natural_compare) {
  if (!left.isValid() || !right.isValid()) {
    if (left.isValid() == right.isValid()) {
      return 0;
    }
    return left.isValid() ? 1 : -1;
  }

  const int left_type = left.typeId();
  const int right_type = right.typeId();

  if (left_type == QMetaType::QString && right_type == QMetaType::QString) {
    const QString lhs = left.toString();
    const QString rhs = right.toString();
    const int compared = natural_compare ? NaturalCollator().compare(lhs, rhs)
                                         : QString::localeAwareCompare(lhs, rhs);
    if (compared == 0) {
      return 0;
    }
    return compared < 0 ? -1 : 1;
  }

  if (left_type == QMetaType::QDateTime && right_type == QMetaType::QDateTime) {
    const QDateTime lhs = left.toDateTime();
    const QDateTime rhs = right.toDateTime();
    if (!lhs.isValid() || !rhs.isValid()) {
      if (lhs.isValid() == rhs.isValid()) {
        return 0;
      }
      return lhs.isValid() ? 1 : -1;
    }
    return CompareOrderedValues(lhs, rhs);
  }

  const bool left_integral =
      IsUnsignedIntegralType(left_type) || IsSignedIntegralType(left_type);
  const bool right_integral =
      IsUnsignedIntegralType(right_type) || IsSignedIntegralType(right_type);
  if (left_integral && right_integral) {
    return CompareIntegralVariants(left, left_type, right, right_type);
  }

  const bool left_numeric = left_integral || IsFloatingPointType(left_type);
  const bool right_numeric = right_integral || IsFloatingPointType(right_type);
  if (left_numeric && right_numeric) {
    return CompareOrderedValues(left.toDouble(), right.toDouble());
  }

  return std::nullopt;
}

bool DirectionIndependentLess(int comparison, Qt::SortOrder order) {
  if (comparison == 0) {
    return false;
  }
  const bool left_first = comparison < 0;
  return order == Qt::AscendingOrder ? left_first : !left_first;
}

}  // namespace

StructuredListSortFilterProxy::StructuredListSortFilterProxy(QObject* parent)
    : QSortFilterProxyModel(parent) {
  setSortRole(kSortKeyRole);
  // Auto-re-sort when the source model resets/changes rows.
  setDynamicSortFilter(true);
  setSortLocaleAware(true);
  setSortCaseSensitivity(Qt::CaseInsensitive);
}

StructuredListSortFilterProxy::~StructuredListSortFilterProxy() = default;

void StructuredListSortFilterProxy::set_natural_compare(bool enabled) {
  natural_compare_ = enabled;
  invalidate();
}

bool StructuredListSortFilterProxy::lessThan(const QModelIndex& l,
                                             const QModelIndex& r) const {
  // Pinned groups always sort to the top, irrespective of sort order.
  if (const auto group_cmp =
          CompareVariantValues(l.data(kSortGroupRole), r.data(kSortGroupRole), false);
      group_cmp.has_value() && *group_cmp != 0) {
    return DirectionIndependentLess(*group_cmp, sortOrder());
  }

  const std::optional<int> primary_cmp =
      CompareVariantValues(SortValueFor(l), SortValueFor(r), natural_compare_);
  if (primary_cmp.has_value() && *primary_cmp != 0) {
    return *primary_cmp < 0;
  }
  if (!primary_cmp.has_value()) {
    const bool left_first = QSortFilterProxyModel::lessThan(l, r);
    const bool right_first = QSortFilterProxyModel::lessThan(r, l);
    if (left_first != right_first) {
      return left_first;
    }
  }

  if (const auto tie_break_cmp =
          CompareVariantValues(l.data(kSortTieBreakRole),
                               r.data(kSortTieBreakRole),
                               false);
      tie_break_cmp.has_value() && *tie_break_cmp != 0) {
    return DirectionIndependentLess(*tie_break_cmp, sortOrder());
  }

  const QModelIndex left_source = mapToSource(l);
  const QModelIndex right_source = mapToSource(r);
  if (left_source.isValid() && right_source.isValid() &&
      left_source.row() != right_source.row()) {
    const int row_cmp = left_source.row() < right_source.row() ? -1 : 1;
    return DirectionIndependentLess(row_cmp, sortOrder());
  }

  return QSortFilterProxyModel::lessThan(l, r);
}

}  // namespace z7::ui::widgets
