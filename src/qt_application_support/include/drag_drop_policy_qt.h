#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>

namespace z7::platform::qt {

    bool mac_archive_native_drag_enabled();
    bool mac_archive_drag_strict_failure_enabled();
    QString mac_archive_promise_mime_type();
    QByteArray encode_mac_archive_promise_payload(QString const& archive_path,
                                                  QString const& archive_type_hint,
                                                  QStringList const& entries);

} // namespace z7::platform::qt
