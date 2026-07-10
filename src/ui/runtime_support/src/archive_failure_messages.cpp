#include "archive_failure_messages.h"

#include <QStringList>
#include <optional>

#include "official_lang_catalog.h"

namespace z7::ui::runtime_support {
    namespace {

        QString empty_archive_open_message() {
            QString text = LF(3005, {QString()}).trimmed();
            text.replace(QStringLiteral(" ''"), QString());
            text.replace(QStringLiteral("''"), QString());
            return text;
        }

        QString encrypted_archive_open_message() {
            QString text = LF(3006, {QString()}).trimmed();
            text.replace(QStringLiteral(" ''"), QString());
            text.replace(QStringLiteral("''"), QString());
            return text;
        }

        std::optional<QString> localized_failure_reason(QString reason) {
            reason = reason.trimmed();
            if (reason.isEmpty()) {
                return QString();
            }

            if (reason.compare(QStringLiteral("Archive format is unsupported"), Qt::CaseInsensitive) == 0) {
                return empty_archive_open_message();
            }
            if (reason.compare(QStringLiteral("Password required or incorrect"), Qt::CaseInsensitive) == 0) {
                return encrypted_archive_open_message();
            }
            if (reason.compare(QStringLiteral("Unsupported method"), Qt::CaseInsensitive) == 0
                || reason.compare(QStringLiteral("Unsupported compression method"), Qt::CaseInsensitive) == 0) {
                return L(3721);
            }
            if (reason.compare(QStringLiteral("Data error"), Qt::CaseInsensitive) == 0) {
                return L(3722);
            }
            if (reason.compare(QStringLiteral("CRC failed"), Qt::CaseInsensitive) == 0) {
                return L(3723);
            }
            if (reason.compare(QStringLiteral("Unavailable data"), Qt::CaseInsensitive) == 0) {
                return L(3724);
            }
            if (reason.compare(QStringLiteral("Unexpected end of data"), Qt::CaseInsensitive) == 0) {
                return L(3725);
            }
            if (reason.compare(QStringLiteral("There are some data after the end of payload data"), Qt::CaseInsensitive)
                == 0) {
                return L(3726);
            }
            if (reason.compare(QStringLiteral("Is not archive"), Qt::CaseInsensitive) == 0) {
                return L(3727);
            }
            if (reason.compare(QStringLiteral("Headers error"), Qt::CaseInsensitive) == 0
                || reason.compare(QStringLiteral("Headers Error"), Qt::CaseInsensitive) == 0) {
                return L(3728);
            }
            if (reason.compare(QStringLiteral("Wrong password"), Qt::CaseInsensitive) == 0) {
                return L(3729);
            }
            if (reason.compare(QStringLiteral("Operation canceled"), Qt::CaseInsensitive) == 0) {
                return L(402);
            }
            return std::nullopt;
        }

        QString localize_failure_line(QString line) {
            QString const trimmed = line.trimmed();
            if (trimmed.isEmpty()) {
                return QString();
            }

            if (std::optional<QString> direct = localized_failure_reason(trimmed); direct.has_value()) {
                return *direct;
            }

            constexpr QLatin1StringView kPathReasonSeparator(" : ");
            qsizetype const separator = trimmed.lastIndexOf(kPathReasonSeparator);
            if (separator > 0) {
                QString const path = trimmed.left(separator).trimmed();
                QString const reason = trimmed.mid(separator + kPathReasonSeparator.size()).trimmed();
                if (!path.isEmpty()) {
                    if (std::optional<QString> localized = localized_failure_reason(reason); localized.has_value()) {
                        return path + QLatin1Char('\n') + *localized;
                    }
                }
            }

            return trimmed;
        }

    } // namespace

    QString localize_archive_failure_message(QString message) {
        QStringList lines = message.split(QLatin1Char('\n'));
        for (QString& line : lines) {
            line = localize_failure_line(line);
        }
        return lines.join(QLatin1Char('\n'));
    }

} // namespace z7::ui::runtime_support
