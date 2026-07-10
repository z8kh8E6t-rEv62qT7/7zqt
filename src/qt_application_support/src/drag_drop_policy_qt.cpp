#include "drag_drop_policy_qt.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace z7::platform::qt {

    namespace {

        constexpr char const* kMacArchivePromiseMimeType = "application/x-z7-filemanager-macos-file-promise";

    } // namespace

    bool mac_archive_native_drag_enabled() {
#if defined(Q_OS_MAC)
        return true;
#else
        return false;
#endif
    }

    bool mac_archive_drag_strict_failure_enabled() {
#if defined(Q_OS_MAC)
        return true;
#else
        return false;
#endif
    }

    QString mac_archive_promise_mime_type() {
        return QString::fromLatin1(kMacArchivePromiseMimeType);
    }

    QByteArray encode_mac_archive_promise_payload(QString const& archive_path,
                                                  QString const& archive_type_hint,
                                                  QStringList const& entries) {
        if (archive_path.trimmed().isEmpty() || entries.isEmpty()) {
            return {};
        }

        QJsonObject root;
        root.insert(QStringLiteral("archive_path"), archive_path.trimmed());
        root.insert(QStringLiteral("archive_type_hint"), archive_type_hint.trimmed());

        QJsonArray entry_array;
        for (QString const& entry : entries) {
            QString const normalized = entry.trimmed();
            if (!normalized.isEmpty()) {
                entry_array.push_back(normalized);
            }
        }
        if (entry_array.isEmpty()) {
            return {};
        }
        root.insert(QStringLiteral("entries"), entry_array);

        return QJsonDocument(root).toJson(QJsonDocument::Compact);
    }

} // namespace z7::platform::qt
