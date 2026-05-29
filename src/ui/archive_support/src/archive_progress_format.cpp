#include "archive_progress_format.h"

#include <QDateTime>

namespace z7::ui::archive_support {

qint64 now_msecs() {
  return QDateTime::currentMSecsSinceEpoch();
}

QString format_hhmmss(quint64 seconds) {
  const quint64 h = seconds / 3600;
  const quint64 m = (seconds % 3600) / 60;
  const quint64 s = seconds % 60;
  return QStringLiteral("%1:%2:%3")
      .arg(h, 2, 10, QLatin1Char('0'))
      .arg(m, 2, 10, QLatin1Char('0'))
      .arg(s, 2, 10, QLatin1Char('0'));
}

QString format_size_short(quint64 bytes) {
  if (bytes >= (1ULL << 30)) {
    return QStringLiteral("%1 GB").arg(bytes >> 30);
  }
  if (bytes >= (1ULL << 20)) {
    return QStringLiteral("%1 MB").arg(bytes >> 20);
  }
  if (bytes >= (1ULL << 10)) {
    return QStringLiteral("%1 KB").arg(bytes >> 10);
  }
  return QStringLiteral("%1 B").arg(bytes);
}

QString format_speed(quint64 completed_bytes, qint64 elapsed_ms) {
  if (elapsed_ms <= 0 || completed_bytes == 0) {
    return QString();
  }

  const quint64 value =
      (completed_bytes * 1000ULL) / static_cast<quint64>(elapsed_ms);
  if (value >= (10000ULL << 20)) {
    return QStringLiteral("%1 GB/s").arg(value >> 30);
  }
  if (value >= (10000ULL << 10)) {
    return QStringLiteral("%1 MB/s").arg(value >> 20);
  }
  if (value >= 10000ULL) {
    return QStringLiteral("%1 KB/s").arg(value >> 10);
  }
  return QStringLiteral("%1 B/s").arg(value);
}

}  // namespace z7::ui::archive_support
