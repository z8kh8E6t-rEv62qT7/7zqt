#include "shared/column_width_persistence.h"

#include <QHeaderView>
#include <QStringList>
#include <algorithm>

namespace z7::ui::filemanager::column_width_persistence {

    namespace {

        constexpr char const* kColumnWidthsVersion = "v1";

    } // namespace

    int clamp_column_width(int width) {
        return std::clamp(width, kMinColumnWidth, kMaxColumnWidth);
    }

    QString encode_widths(QVector<int> const& widths) {
        QStringList parts;
        parts.reserve(widths.size());
        for (int const width : widths) {
            parts.push_back(QString::number(clamp_column_width(width)));
        }
        return QStringLiteral("%1|%2|%3")
            .arg(QString::fromLatin1(kColumnWidthsVersion))
            .arg(widths.size())
            .arg(parts.join(QLatin1Char(',')));
    }

    bool decode_widths(QString const& encoded, int expected_count, QVector<int>* widths_out) {
        if (expected_count <= 0 || widths_out == nullptr) {
            return false;
        }

        QStringList const segments = encoded.split(QLatin1Char('|'), Qt::KeepEmptyParts);
        if (segments.size() != 3 || segments[0] != QString::fromLatin1(kColumnWidthsVersion)) {
            return false;
        }

        bool count_ok = false;
        int const declared_count = segments[1].toInt(&count_ok);
        if (!count_ok || declared_count != expected_count) {
            return false;
        }

        QStringList const raw_widths = segments[2].split(QLatin1Char(','), Qt::KeepEmptyParts);
        if (raw_widths.size() != expected_count) {
            return false;
        }

        QVector<int> widths;
        widths.reserve(expected_count);
        for (QString const& token : raw_widths) {
            bool ok = false;
            QString const trimmed = token.trimmed();
            int const width = trimmed.toInt(&ok);
            if (!ok) {
                return false;
            }
            widths.push_back(clamp_column_width(width));
        }

        *widths_out = widths;
        return true;
    }

    QVector<int> capture_widths(QHeaderView const* header, int expected_count) {
        QVector<int> widths;
        if (header == nullptr || expected_count <= 0 || header->count() < expected_count) {
            return widths;
        }

        widths.reserve(expected_count);
        for (int section = 0; section < expected_count; ++section) {
            widths.push_back(clamp_column_width(header->sectionSize(section)));
        }
        return widths;
    }

    void apply_widths(QHeaderView* header, QVector<int> const& widths) {
        if (header == nullptr) {
            return;
        }

        int const section_count = std::min(header->count(), static_cast<int>(widths.size()));
        for (int section = 0; section < section_count; ++section) {
            header->resizeSection(section, clamp_column_width(widths.at(section)));
        }
    }

} // namespace z7::ui::filemanager::column_width_persistence
