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
                    == 0
                || reason.compare(QStringLiteral("There are some data after the end of the payload data"),
                                  Qt::CaseInsensitive)
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
            if (reason.compare(QStringLiteral("Wrong password?"), Qt::CaseInsensitive) == 0) {
                return L(3710);
            }
            if (reason.compare(QStringLiteral("Warning"), Qt::CaseInsensitive) == 0) {
                return L(1073);
            }
            if (reason.compare(QStringLiteral("Warnings"), Qt::CaseInsensitive) == 0) {
                return L(1072);
            }
            if (reason.compare(QStringLiteral("The operation requires big amount of memory (RAM)."),
                               Qt::CaseInsensitive)
                == 0) {
                return L(7811);
            }
            if (reason.compare(QStringLiteral("required memory usage size"), Qt::CaseInsensitive) == 0) {
                return L(7812);
            }
            if (reason.compare(QStringLiteral("allowed memory usage limit"), Qt::CaseInsensitive) == 0) {
                return L(7813);
            }
            if (reason.compare(QStringLiteral("Archive extraction was skipped."), Qt::CaseInsensitive) == 0) {
                return L(7822);
            }
            if (reason.compare(QStringLiteral("Unavailable start of archive"), Qt::CaseInsensitive) == 0) {
                return L(3763);
            }
            if (reason.compare(QStringLiteral("Unconfirmed start of archive"), Qt::CaseInsensitive) == 0) {
                return L(3764);
            }
            if (reason.compare(QStringLiteral("Unsupported feature"), Qt::CaseInsensitive) == 0) {
                return L(3768);
            }
            if (reason.compare(QStringLiteral("The archive is open with offset"), Qt::CaseInsensitive) == 0) {
                return L(3019);
            }
            if (reason.compare(QStringLiteral("Cannot open file as archive"), Qt::CaseInsensitive) == 0) {
                return empty_archive_open_message();
            }
            if (reason.compare(QStringLiteral("Cannot open encrypted archive. Wrong password?"),
                               Qt::CaseInsensitive)
                == 0) {
                return encrypted_archive_open_message();
            }
            if (reason.compare(QStringLiteral("Headers Error : Wrong password?"), Qt::CaseInsensitive) == 0) {
                return L(3728) + QStringLiteral(" : ") + L(3710);
            }
            constexpr QLatin1StringView kCannotOpenAsPrefix("Cannot open the file as ");
            constexpr QLatin1StringView kOpenAsPrefix("The file is open as ");
            constexpr QLatin1StringView kArchiveSuffix(" archive");
            if (reason.startsWith(kCannotOpenAsPrefix, Qt::CaseInsensitive)
                && reason.endsWith(kArchiveSuffix, Qt::CaseInsensitive)) {
                QString const type = reason.mid(kCannotOpenAsPrefix.size(),
                                                reason.size() - kCannotOpenAsPrefix.size() - kArchiveSuffix.size());
                return LF(3017, {type});
            }
            if (reason.startsWith(kOpenAsPrefix, Qt::CaseInsensitive)
                && reason.endsWith(kArchiveSuffix, Qt::CaseInsensitive)) {
                QString const type =
                    reason.mid(kOpenAsPrefix.size(), reason.size() - kOpenAsPrefix.size() - kArchiveSuffix.size());
                return LF(3018, {type});
            }
            if (reason.compare(QStringLiteral("Operation canceled"), Qt::CaseInsensitive) == 0) {
                return L(402);
            }
            return std::nullopt;
        }

        std::optional<QString> localize_semantic_fragment(QString const& fragment) {
            qsizetype content_begin = 0;
            while (content_begin < fragment.size() && fragment.at(content_begin).isSpace()) {
                ++content_begin;
            }
            qsizetype content_end = fragment.size();
            while (content_end > content_begin && fragment.at(content_end - 1).isSpace()) {
                --content_end;
            }
            if (content_begin == content_end) {
                return std::nullopt;
            }

            std::optional<QString> const localized =
                localized_failure_reason(fragment.mid(content_begin, content_end - content_begin));
            if (!localized.has_value()) {
                return std::nullopt;
            }
            return fragment.left(content_begin) + *localized + fragment.mid(content_end);
        }

        QString localize_failure_line(QString line) {
            qsizetype content_begin = 0;
            while (content_begin < line.size() && line.at(content_begin).isSpace()) {
                ++content_begin;
            }
            qsizetype content_end = line.size();
            while (content_end > content_begin && line.at(content_end - 1).isSpace()) {
                --content_end;
            }
            if (content_begin == content_end) {
                return line;
            }

            QString const content = line.mid(content_begin, content_end - content_begin);
            if (std::optional<QString> direct = localize_semantic_fragment(content); direct.has_value()) {
                return line.left(content_begin) + *direct + line.mid(content_end);
            }

            struct PrefixTranslation {
                QLatin1StringView source;
                QString translated;
            };
            PrefixTranslation const prefixes[] = {
                {QLatin1StringView("Warning"), L(1073)},
                {QLatin1StringView("Warnings"), L(1072)},
            };
            for (PrefixTranslation const& prefix : prefixes) {
                if (content.size() > prefix.source.size()
                    && content.startsWith(prefix.source, Qt::CaseInsensitive)
                    && content.at(prefix.source.size()) == QLatin1Char(':')) {
                    QString suffix = content.mid(prefix.source.size() + 1);
                    if (std::optional<QString> localized_suffix = localize_semantic_fragment(suffix);
                        localized_suffix.has_value()) {
                        suffix = *localized_suffix;
                    }
                    return line.left(content_begin) + prefix.translated + QLatin1Char(':') + suffix
                        + line.mid(content_end);
                }
            }

            constexpr QLatin1StringView kErrorPrefix("ERROR:");
            if (content.startsWith(kErrorPrefix, Qt::CaseInsensitive)) {
                QString suffix = content.mid(kErrorPrefix.size());
                if (std::optional<QString> localized_suffix = localize_semantic_fragment(suffix);
                    localized_suffix.has_value()) {
                    return line.left(content_begin) + content.left(kErrorPrefix.size()) + *localized_suffix
                        + line.mid(content_end);
                }
            }

            constexpr QLatin1StringView kReasonSeparator(" : ");
            QStringList fragments = content.split(kReasonSeparator, Qt::KeepEmptyParts);
            if (fragments.size() < 2) {
                return line;
            }

            bool changed = false;
            if (std::optional<QString> localized_first = localize_semantic_fragment(fragments.front());
                localized_first.has_value()) {
                fragments.front() = *localized_first;
                changed = true;

                // The encrypted-operation hint is semantic even when no path follows it.
                for (qsizetype i = 1; i < fragments.size(); ++i) {
                    QString const semantic = fragments.at(i).trimmed();
                    if (semantic.compare(QStringLiteral("Wrong password?"), Qt::CaseInsensitive) != 0) {
                        continue;
                    }
                    fragments[i] = *localize_semantic_fragment(fragments.at(i));
                    changed = true;
                }
            } else if (std::optional<QString> localized_last = localize_semantic_fragment(fragments.back());
                       localized_last.has_value()) {
                // Retain support for existing path-first diagnostics and memory size labels.
                fragments.back() = *localized_last;
                changed = true;
            }

            if (!changed) {
                return line;
            }
            return line.left(content_begin) + fragments.join(kReasonSeparator) + line.mid(content_end);
        }

    } // namespace

    QString localize_archive_failure_message(QString message) {
        QStringList lines = message.split(QLatin1Char('\n'), Qt::KeepEmptyParts);
        for (QString& line : lines) {
            line = localize_failure_line(line);
        }
        return lines.join(QLatin1Char('\n'));
    }

} // namespace z7::ui::runtime_support
