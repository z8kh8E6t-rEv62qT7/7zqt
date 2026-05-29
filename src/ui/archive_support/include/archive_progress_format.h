#pragma once

#include <QString>
#include <QtGlobal>

namespace z7::ui::archive_support {

qint64 now_msecs();
QString format_hhmmss(quint64 seconds);
QString format_size_short(quint64 bytes);
QString format_speed(quint64 completed_bytes, qint64 elapsed_ms);

}  // namespace z7::ui::archive_support
