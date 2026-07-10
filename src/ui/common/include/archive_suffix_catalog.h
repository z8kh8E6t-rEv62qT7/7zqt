// Role: Shared archive suffix catalog for UI-layer archive recognition.

#pragma once

#include <QString>
#include <QStringList>

namespace z7::ui::common {

    QStringList const& ordered_archive_suffixes();
    bool is_archive_suffix(QString const& suffix);
    bool is_archive_suffix_or_alias(QString const& suffix_or_alias);
    bool archive_name_has_known_suffix(QString const& name);

} // namespace z7::ui::common
