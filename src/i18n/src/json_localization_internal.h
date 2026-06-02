#pragma once

#include <QString>

namespace z7::i18n::internal {

void set_current_language_hint(QString language_hint);
QString current_language_hint();

}  // namespace z7::i18n::internal
