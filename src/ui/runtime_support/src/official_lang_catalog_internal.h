#pragma once

#include <QList>
#include <QString>
#include <QStringList>

#include "official_lang_catalog.h"
#include "portable_settings.h"

namespace z7::ui::runtime_support::official_lang_catalog_internal {

    constexpr uint32_t kLangEnglishNameId = 1;
    constexpr uint32_t kLangNativeNameId = 2;

    constexpr char const* kRequiredEnglishFileName = "en.ttt";
    constexpr char const* kSettingsKeyLang = "Lang";

    bool is_blank_line(QString const& line);
    bool is_decimal_number(QString const& line);
    QString decode_lang2_text_line(QString const& line);
    QString normalize_locale_name(QString const& locale_name);
    QString primary_subtag(QString const& lang_id);
    QString format_line_with_id(uint32_t id, QString const& text);
    z7::platform::qt::PortableSettings make_settings();
    void assign_language_marks(QList<LangInfo>* languages);

} // namespace z7::ui::runtime_support::official_lang_catalog_internal
