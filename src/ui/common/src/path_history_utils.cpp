// Role: Shared path-history normalization helpers for UI persistence.

#include "path_history_utils.h"

#include <QDir>

namespace z7::ui::common {
namespace {

Qt::CaseSensitivity path_history_case_sensitivity() {
  return Qt::CaseInsensitive;
}

}  // namespace

QString normalize_path_history_entry(QString value) {
  value = QDir::fromNativeSeparators(value.trimmed());
  if (value.isEmpty()) {
    return QString();
  }
  const bool preserve_double_slash =
      value.startsWith(QStringLiteral("//")) &&
      !value.startsWith(QStringLiteral("///"));
  QString cleaned = QDir::cleanPath(value);
  if (!preserve_double_slash) {
    return cleaned;
  }
  if (cleaned.startsWith(QStringLiteral("//"))) {
    return cleaned;
  }
  if (cleaned.startsWith(QLatin1Char('/'))) {
    cleaned.prepend(QLatin1Char('/'));
    return cleaned;
  }
  cleaned.prepend(QStringLiteral("//"));
  return cleaned;
}

QStringList normalized_path_history(QStringList history,
                                    const QString& new_item,
                                    int max_items) {
  QStringList out;
  out.reserve(history.size() + 1);

  const QString normalized_new = normalize_path_history_entry(new_item);
  if (!normalized_new.isEmpty()) {
    out << normalized_new;
  }

  for (const QString& raw : history) {
    const QString normalized = normalize_path_history_entry(raw);
    if (normalized.isEmpty() ||
        out.contains(normalized, path_history_case_sensitivity())) {
      continue;
    }
    out << normalized;
  }

  while (out.size() > max_items) {
    out.removeLast();
  }
  return out;
}

}  // namespace z7::ui::common
