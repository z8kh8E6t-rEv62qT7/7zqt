#include "archive_virtual_path.h"

#include <QDir>

namespace z7::ui::archive_support {

    QString normalize_virtual_dir(QString const& value) {
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

    QString join_virtual_path(QString const& base, QString const& child) {
        QString const left = normalize_virtual_dir(base);
        QString const right = normalize_virtual_dir(child);
        if (left.isEmpty()) {
            return right;
        }
        if (right.isEmpty()) {
            return left;
        }
        return left + QLatin1Char('/') + right;
    }

    QString virtual_display_path(QString const& display_source, QString const& virtual_dir) {
        QString source = QDir::toNativeSeparators(display_source.trimmed());
        if (source.isEmpty()) {
            return source;
        }

        QChar const sep = QDir::separator();
        if (!source.endsWith(sep)) {
            source += sep;
        }

        QString const rel = normalize_virtual_dir(virtual_dir);
        if (rel.isEmpty()) {
            return source;
        }
        return source + QDir::toNativeSeparators(rel);
    }

} // namespace z7::ui::archive_support
