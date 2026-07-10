#pragma once

#include <QString>
#include <QVector>

class QHeaderView;

namespace z7::ui::filemanager::column_width_persistence {

    constexpr int kMinColumnWidth = 40;
    constexpr int kMaxColumnWidth = 2000;

    int clamp_column_width(int width);
    QString encode_widths(QVector<int> const& widths);
    bool decode_widths(QString const& encoded, int expected_count, QVector<int>* widths_out);
    QVector<int> capture_widths(QHeaderView const* header, int expected_count);
    void apply_widths(QHeaderView* header, QVector<int> const& widths);

} // namespace z7::ui::filemanager::column_width_persistence
