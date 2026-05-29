#pragma once

#include <cstdint>

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

namespace z7::ui::runtime_support {

struct LangInfo {
  QString id;
  QString english_name;
  QString native_name;
  QString mark;
  int translated_lines = 0;
  int total_lines = 0;
  QStringList comments;
  QStringList missing_lines;
  QStringList extra_lines;
};

class OfficialLangCatalog {
 public:
  static OfficialLangCatalog& instance();
  static QString language_directory_path();
  static QString required_english_language_file_path();
  static bool validate_required_language_resources(QString* error_out);

  QString text(uint32_t id) const;
  QString format(uint32_t id, const QStringList& args) const;

  QString current_language() const;
  bool set_language(const QString& lang_id);
  bool set_language_and_persist(const QString& lang_id);

  QList<LangInfo> available_languages() const;
  void reload_from_settings();

 private:
  OfficialLangCatalog();

  void rebuild_language_index();
  bool set_language_internal(const QString& lang_id, bool persist);
  bool load_language_file(const QString& lang_id,
                          QHash<uint32_t, QString>* map_out,
                          QString* english_name_out,
                          QString* native_name_out,
                          QStringList* comments_out) const;
  void apply_resource_fallbacks(QHash<uint32_t, QString>* map) const;

  static QHash<uint32_t, QString> parse_lang2(
      const QByteArray& bytes,
      QString* english_name_out,
      QString* native_name_out,
      QStringList* comments_out);
  static QHash<uint32_t, QString> parse_rc_string_tables(const QByteArray& bytes);

  static QString substitute_placeholders(const QString& text,
                                         const QStringList& args);
  static QString normalize_language_id(const QString& lang_id);

  QString settings_language() const;
  void save_settings_language(const QString& lang_id) const;

  QHash<uint32_t, QString> english_;
  QHash<uint32_t, QString> active_;
  QHash<uint32_t, QString> resource_fallbacks_;
  QList<LangInfo> languages_;
  QStringList english_comments_;
  QString current_language_id_ = QStringLiteral("-");
};

QString L(uint32_t id);
QString LF(uint32_t id, const QStringList& args);
QString strip_mnemonic(const QString& text);

}  // namespace z7::ui::runtime_support
