#include "archive_virtual_path.h"

#include <QDir>

namespace z7::ui::archive_support {

QString normalize_virtual_dir(const QString& value) {
  QString out = QDir::fromNativeSeparators(value.trimmed());
  while (out.startsWith(QLatin1Char('/'))) {
    out.remove(0, 1);
  }
  while (out.endsWith(QLatin1Char('/'))) {
    out.chop(1);
  }
  while (out.contains(QStringLiteral("//"))) {
    out.replace(QStringLiteral("//"), QStringLiteral("/"));
  }
  return out;
}

QString join_virtual_path(const QString& base, const QString& child) {
  const QString left = normalize_virtual_dir(base);
  const QString right = normalize_virtual_dir(child);
  if (left.isEmpty()) {
    return right;
  }
  if (right.isEmpty()) {
    return left;
  }
  return left + QLatin1Char('/') + right;
}

QString virtual_display_path(const QString& display_source,
                             const QString& virtual_dir) {
  QString source = QDir::toNativeSeparators(display_source.trimmed());
  if (source.isEmpty()) {
    return source;
  }

  const QChar sep = QDir::separator();
  if (!source.endsWith(sep)) {
    source += sep;
  }

  const QString rel = normalize_virtual_dir(virtual_dir);
  if (rel.isEmpty()) {
    return source;
  }
  return source + QDir::toNativeSeparators(rel);
}

}  // namespace z7::ui::archive_support
