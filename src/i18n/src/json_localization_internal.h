#pragma once

#include <QString>

namespace z7::i18n::internal {

void set_current_locale_hint(QString locale_hint);
QString current_locale_hint();

}  // namespace z7::i18n::internal
