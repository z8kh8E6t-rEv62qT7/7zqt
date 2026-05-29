#include "custom_localization.h"

#include "json_localization.h"
#include "json_localization_internal.h"
#include "official_lang_catalog.h"

namespace z7::ui::runtime_support {

QString J(QStringView key) {
  z7::i18n::internal::set_current_locale_hint(
      OfficialLangCatalog::instance().current_language());
  return z7::i18n::text(key);
}

QString JF(QStringView key, const QStringList& args) {
  z7::i18n::internal::set_current_locale_hint(
      OfficialLangCatalog::instance().current_language());
  return z7::i18n::format(key, args);
}

}  // namespace z7::ui::runtime_support
