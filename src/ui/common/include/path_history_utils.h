// Role: Shared path-history normalization helpers for UI persistence.

#pragma once

#include <QString>
#include <QStringList>

namespace z7::ui::common {

QString normalize_path_history_entry(QString value);
QStringList normalized_path_history(QStringList history,
                                    const QString& new_item,
                                    int max_items);

}  // namespace z7::ui::common
