// Role: Shared archive suffix catalog for UI-layer archive recognition.

#pragma once

#include <QString>
#include <QStringList>

namespace z7::ui::common {

const QStringList& ordered_archive_suffixes();
bool is_archive_suffix(const QString& suffix);
bool is_archive_suffix_or_alias(const QString& suffix_or_alias);
bool archive_name_has_known_suffix(const QString& name);

}  // namespace z7::ui::common
