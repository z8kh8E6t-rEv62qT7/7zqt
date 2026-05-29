#pragma once

#include <QList>
#include <QString>
#include <QStringList>

#include "portable_settings.h"
#include "official_lang_catalog.h"

namespace z7::ui::runtime_support::official_lang_catalog_internal {

constexpr uint32_t kLangEnglishNameId = 1;
constexpr uint32_t kLangNativeNameId = 2;

constexpr const char* kRequiredEnglishFileName = "en.ttt";
constexpr const char* kSettingsKeyLang = "Lang";

bool is_blank_line(const QString& line);
bool is_decimal_number(const QString& line);
QString decode_lang2_text_line(const QString& line);
QString normalize_locale_name(const QString& locale_name);
QString primary_subtag(const QString& lang_id);
QString format_line_with_id(uint32_t id, const QString& text);
z7::platform::qt::PortableSettings make_settings();
void assign_language_marks(QList<LangInfo>* languages);

}  // namespace z7::ui::runtime_support::official_lang_catalog_internal
