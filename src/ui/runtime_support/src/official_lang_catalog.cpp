#include "official_lang_catalog.h"

#include <QByteArray>
#include <QChar>
#include <QDir>
#include <QFile>
#include <QLocale>
#include <QRegularExpression>
#include <QVariant>

#include <algorithm>

#include "json_localization_internal.h"
#include "portable_settings.h"
#include "official_lang_catalog_internal.h"

namespace z7::ui::runtime_support {

using namespace official_lang_catalog_internal;

namespace {

QString filemanager_rc_directory_path() {
  return QStringLiteral(":/z7/fm-rc");
}

QString filemanager_rc_file_path(const QString& file_name) {
  return QDir(filemanager_rc_directory_path()).filePath(file_name);
}

void merge_string_map(QHash<uint32_t, QString>* target,
                      const QHash<uint32_t, QString>& source) {
  if (target == nullptr) {
    return;
  }
  for (auto it = source.cbegin(); it != source.cend(); ++it) {
    target->insert(it.key(), it.value());
  }
}

QByteArray read_rc_resource_file(const QString& file_name) {
  QFile file(filemanager_rc_file_path(file_name));
  if (!file.open(QIODevice::ReadOnly)) {
    return {};
  }
  return file.readAll();
}

}  // namespace

OfficialLangCatalog& OfficialLangCatalog::instance() {
  static OfficialLangCatalog catalog;
  return catalog;
}

QString OfficialLangCatalog::language_directory_path() {
  return QStringLiteral(":/z7/lang");
}

QString OfficialLangCatalog::required_english_language_file_path() {
  return QDir(language_directory_path())
      .filePath(QString::fromLatin1(kRequiredEnglishFileName));
}

bool OfficialLangCatalog::validate_required_language_resources(QString* error_out) {
  if (error_out != nullptr) {
    error_out->clear();
  }

  const QString en_path = required_english_language_file_path();
  QFile en_file(en_path);
  if (!en_file.exists()) {
    if (error_out != nullptr) {
      *error_out = QStringLiteral("Required language file is missing: %1").arg(en_path);
    }
    return false;
  }

  if (!en_file.open(QIODevice::ReadOnly)) {
    if (error_out != nullptr) {
      *error_out =
          QStringLiteral("Cannot open required language file \"%1\": %2")
              .arg(en_path, en_file.errorString());
    }
    return false;
  }

  QString english_name;
  QString native_name;
  QStringList comments;
  const QHash<uint32_t, QString> parsed =
      parse_lang2(en_file.readAll(), &english_name, &native_name, &comments);
  if (parsed.isEmpty()) {
    if (error_out != nullptr) {
      *error_out = QStringLiteral("Cannot parse required language file: %1").arg(en_path);
    }
    return false;
  }

  return true;
}

OfficialLangCatalog::OfficialLangCatalog() {
  {
    const QByteArray property_name_rc =
        read_rc_resource_file(QStringLiteral("PropertyName.rc"));
    const QByteArray property_name_res_h =
        read_rc_resource_file(QStringLiteral("PropertyNameRes.h"));
    if (!property_name_rc.isEmpty()) {
      merge_string_map(&resource_fallbacks_,
                       parse_rc_string_tables(property_name_res_h + "\n" + property_name_rc));
    }
  }
  {
    const QByteArray resource_rc = read_rc_resource_file(QStringLiteral("resource.rc"));
    const QByteArray resource_h = read_rc_resource_file(QStringLiteral("resource.h"));
    if (!resource_rc.isEmpty()) {
      merge_string_map(&resource_fallbacks_,
                       parse_rc_string_tables(resource_h + "\n" + resource_rc));
    }
  }
  {
    const QByteArray resource_gui_rc = read_rc_resource_file(QStringLiteral("resourceGui.rc"));
    const QByteArray resource_gui_h = read_rc_resource_file(QStringLiteral("resourceGui.h"));
    if (!resource_gui_rc.isEmpty()) {
      merge_string_map(&resource_fallbacks_,
                       parse_rc_string_tables(resource_gui_h + "\n" + resource_gui_rc));
    }
  }

  QString english_name;
  QString native_name;
  QFile en_file(required_english_language_file_path());
  if (en_file.open(QIODevice::ReadOnly)) {
    english_ = parse_lang2(
        en_file.readAll(), &english_name, &native_name, &english_comments_);
    apply_resource_fallbacks(&english_);
  }
  active_ = english_;
  rebuild_language_index();
  reload_from_settings();
}

QString OfficialLangCatalog::text(uint32_t id) const {
  const auto active_it = active_.constFind(id);
  if (active_it != active_.cend()) {
    return active_it.value();
  }
  return QStringLiteral("#%1").arg(id);
}

QString OfficialLangCatalog::format(uint32_t id, const QStringList& args) const {
  return substitute_placeholders(text(id), args);
}

QString OfficialLangCatalog::current_language() const {
  return current_language_id_;
}

bool OfficialLangCatalog::set_language(const QString& lang_id) {
  return set_language_internal(lang_id, false);
}

bool OfficialLangCatalog::set_language_and_persist(const QString& lang_id) {
  return set_language_internal(lang_id, true);
}

QList<LangInfo> OfficialLangCatalog::available_languages() const {
  return languages_;
}

void OfficialLangCatalog::reload_from_settings() {
  const QString stored = settings_language();
  if (!set_language(stored)) {
    set_language_internal(QStringLiteral("-"), true);
  }
}

void OfficialLangCatalog::rebuild_language_index() {
  languages_.clear();

  QList<uint32_t> english_ids = english_.keys();
  std::sort(english_ids.begin(), english_ids.end());

  LangInfo english_info;
  english_info.id = QStringLiteral("-");
  english_info.english_name = english_.value(
      kLangEnglishNameId, QStringLiteral("English"));
  english_info.native_name = english_.value(
      kLangNativeNameId, QStringLiteral("English"));
  english_info.translated_lines = english_.size();
  english_info.total_lines = english_.size();
  english_info.comments = english_comments_;
  languages_.append(english_info);

  QDir lang_dir(language_directory_path());
  QStringList files =
      lang_dir.entryList(QStringList{QStringLiteral("*.txt")}, QDir::Files, QDir::Name);
  files.sort(Qt::CaseInsensitive);

  for (const QString& file_name : files) {
    const QString lang_id = file_name.left(file_name.size() - 4).toLower();
    if (lang_id.isEmpty()) {
      continue;
    }

    QHash<uint32_t, QString> map;
    QString english_name;
    QString native_name;
    QStringList comments;
    if (!load_language_file(lang_id, &map, &english_name, &native_name, &comments)) {
      continue;
    }

    int translated = 0;
    for (auto it = map.cbegin(); it != map.cend(); ++it) {
      if (english_.contains(it.key())) {
        ++translated;
      }
    }

    QList<uint32_t> map_ids = map.keys();
    std::sort(map_ids.begin(), map_ids.end());

    QStringList missing_lines;
    for (uint32_t id : english_ids) {
      if (!map.contains(id)) {
        missing_lines.append(format_line_with_id(id, english_.value(id)));
      }
    }

    QStringList extra_lines;
    for (uint32_t id : map_ids) {
      if (!english_.contains(id)) {
        extra_lines.append(format_line_with_id(id, map.value(id)));
      }
    }

    LangInfo info;
    info.id = lang_id;
    info.english_name = english_name.isEmpty() ? lang_id : english_name;
    info.native_name = native_name.isEmpty() ? info.english_name : native_name;
    info.translated_lines = translated;
    info.total_lines = english_.size();
    info.comments = comments;
    info.missing_lines = missing_lines;
    info.extra_lines = extra_lines;
    languages_.append(info);
  }

  assign_language_marks(&languages_);
}

bool OfficialLangCatalog::set_language_internal(const QString& lang_id,
                                                bool persist) {
  const QString normalized = normalize_language_id(lang_id);
  if (normalized == QStringLiteral("-")) {
    current_language_id_ = normalized;
    active_ = english_;
    z7::i18n::internal::set_current_locale_hint(current_language_id_);
    if (persist) {
      save_settings_language(normalized);
    }
    return true;
  }

  QHash<uint32_t, QString> map;
  QString english_name;
  QString native_name;
  QStringList comments;
  if (!load_language_file(normalized, &map, &english_name, &native_name, &comments)) {
    return false;
  }

  current_language_id_ = normalized;
  active_ = map;
  z7::i18n::internal::set_current_locale_hint(current_language_id_);
  if (persist) {
    save_settings_language(normalized);
  }
  return true;
}

bool OfficialLangCatalog::load_language_file(
    const QString& lang_id,
    QHash<uint32_t, QString>* map_out,
    QString* english_name_out,
    QString* native_name_out,
    QStringList* comments_out) const {
  if (map_out == nullptr) {
    return false;
  }

  const QString path = QDir(language_directory_path())
                           .filePath(QStringLiteral("%1.txt").arg(normalize_language_id(lang_id)));
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    return false;
  }

  *map_out = parse_lang2(file.readAll(),
                         english_name_out,
                         native_name_out,
                         comments_out);
  apply_resource_fallbacks(map_out);
  return !map_out->isEmpty();
}

QHash<uint32_t, QString> OfficialLangCatalog::parse_lang2(
    const QByteArray& bytes,
    QString* english_name_out,
    QString* native_name_out,
    QStringList* comments_out) {
  QHash<uint32_t, QString> map;

  QString content = QString::fromUtf8(bytes);
  if (content.startsWith(QChar(0xFEFF))) {
    content.remove(0, 1);
  }

  const QStringList lines = content.split(QLatin1Char('\n'));
  if (lines.isEmpty()) {
    return map;
  }

  if (!lines.front().startsWith(QStringLiteral(";!@Lang2@!UTF-8!"))) {
    return map;
  }

  int64_t id = -1024;
  for (int line_index = 1; line_index < lines.size(); ++line_index) {
    QString line = lines[line_index];
    if (line.endsWith(QLatin1Char('\r'))) {
      line.chop(1);
    }

    if (is_blank_line(line)) {
      ++id;
      continue;
    }

    if (line.startsWith(QLatin1Char(';'))) {
      if (comments_out != nullptr) {
        QString comment = line.mid(1).trimmed();
        if (!comment.isEmpty()) {
          comments_out->append(comment);
        }
      }
      ++id;
      continue;
    }

    const QString trimmed = line.trimmed();
    if (is_decimal_number(trimmed)) {
      bool ok = false;
      const uint32_t explicit_id = trimmed.toUInt(&ok);
      if (ok) {
        id = static_cast<int64_t>(explicit_id);
        continue;
      }
    }

    if (id >= 0) {
      map.insert(static_cast<uint32_t>(id), decode_lang2_text_line(line));
    }
    ++id;
  }

  if (english_name_out != nullptr) {
    *english_name_out = map.value(kLangEnglishNameId);
  }
  if (native_name_out != nullptr) {
    *native_name_out = map.value(kLangNativeNameId);
  }
  return map;
}

QHash<uint32_t, QString> OfficialLangCatalog::parse_rc_string_tables(const QByteArray& bytes) {
  QHash<uint32_t, QString> map;

  QString content = QString::fromUtf8(bytes);
  if (content.startsWith(QChar(0xFEFF))) {
    content.remove(0, 1);
  }

  const QRegularExpression begin_re(QStringLiteral("^\\s*STRINGTABLE\\s*$"));
  const QRegularExpression block_begin_re(QStringLiteral("^\\s*BEGIN\\s*$"));
  const QRegularExpression block_end_re(QStringLiteral("^\\s*END\\s*$"));
  const QRegularExpression entry_re(
      QStringLiteral("^\\s*([A-Z0-9_]+)\\s+\"((?:[^\"\\\\]|\\\\.)*)\"\\s*$"));
  const QRegularExpression define_re(
      QStringLiteral("^\\s*#define\\s+([A-Z0-9_]+)\\s+(\\d+)\\s*$"));

  const QStringList lines = content.split(QLatin1Char('\n'));
  QHash<QString, uint32_t> ids_by_name;
  bool in_stringtable = false;
  bool in_block = false;

  for (QString line : lines) {
    if (line.endsWith(QLatin1Char('\r'))) {
      line.chop(1);
    }

    const auto define_match = define_re.match(line);
    if (define_match.hasMatch()) {
      bool ok = false;
      const uint32_t id = define_match.captured(2).toUInt(&ok);
      if (ok) {
        ids_by_name.insert(define_match.captured(1), id);
      }
      continue;
    }

    if (!in_stringtable) {
      if (begin_re.match(line).hasMatch()) {
        in_stringtable = true;
      }
      continue;
    }

    if (!in_block) {
      if (block_begin_re.match(line).hasMatch()) {
        in_block = true;
      } else if (begin_re.match(line).hasMatch()) {
        in_stringtable = true;
      }
      continue;
    }

    if (block_end_re.match(line).hasMatch()) {
      in_stringtable = false;
      in_block = false;
      continue;
    }

    const auto entry_match = entry_re.match(line);
    if (!entry_match.hasMatch()) {
      continue;
    }

    const QString symbol = entry_match.captured(1);
    if (!ids_by_name.contains(symbol)) {
      continue;
    }

    const QString decoded = decode_lang2_text_line(entry_match.captured(2));
    if (decoded.trimmed().isEmpty()) {
      continue;
    }
    map.insert(ids_by_name.value(symbol), decoded);
  }

  return map;
}

void OfficialLangCatalog::apply_resource_fallbacks(QHash<uint32_t, QString>* map) const {
  if (map == nullptr || resource_fallbacks_.isEmpty()) {
    return;
  }

  for (auto it = resource_fallbacks_.cbegin(); it != resource_fallbacks_.cend(); ++it) {
    if (!map->contains(it.key()) || map->value(it.key()).trimmed().isEmpty()) {
      map->insert(it.key(), it.value());
    }
  }
}

QString OfficialLangCatalog::substitute_placeholders(const QString& text,
                                                     const QStringList& args) {
  QString out = text;
  for (int i = 0; i < args.size(); ++i) {
    out.replace(QStringLiteral("{%1}").arg(i), args[i]);
  }
  return out;
}

QString OfficialLangCatalog::normalize_language_id(const QString& lang_id) {
  const QString trimmed = lang_id.trimmed();
  if (trimmed.isEmpty() || trimmed == QStringLiteral("-")) {
    return QStringLiteral("-");
  }
  return trimmed.toLower();
}

QString OfficialLangCatalog::settings_language() const {
  const QString key = QString::fromLatin1(kSettingsKeyLang);

  z7::platform::qt::PortableSettings settings = make_settings();
  const QVariant value = settings.value(key);
  if (value.isValid()) {
    return normalize_language_id(value.toString());
  }

  return QStringLiteral("-");
}

void OfficialLangCatalog::save_settings_language(const QString& lang_id) const {
  const QString normalized = normalize_language_id(lang_id);
  const QString key = QString::fromLatin1(kSettingsKeyLang);

  z7::platform::qt::PortableSettings settings = make_settings();
  settings.setValue(key, normalized);
}

QString L(uint32_t id) {
  return OfficialLangCatalog::instance().text(id);
}

QString LF(uint32_t id, const QStringList& args) {
  return OfficialLangCatalog::instance().format(id, args);
}

QString strip_mnemonic(const QString& text) {
  QString out;
  out.reserve(text.size());

  for (int i = 0; i < text.size(); ++i) {
    const QChar ch = text[i];
    if (ch != QLatin1Char('&')) {
      out.append(ch);
      continue;
    }

    if (i + 1 < text.size() && text[i + 1] == QLatin1Char('&')) {
      out.append(QLatin1Char('&'));
      ++i;
    }
  }

  return out;
}

}  // namespace z7::ui::runtime_support
