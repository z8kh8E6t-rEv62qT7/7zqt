#include "json_localization.h"
#include "json_localization_internal.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QMutexLocker>
#include <QLocale>
#include <QStringList>

void ensure_i18n_resources_initialized_global() {
  Q_INIT_RESOURCE(z7_i18n_resources);
}

namespace {

struct LocaleDocument {
  bool loaded = false;
  QJsonObject root;
};

QMutex& i18n_mutex() {
  static QMutex mutex;
  return mutex;
}

QHash<QString, LocaleDocument>& locale_cache() {
  static QHash<QString, LocaleDocument> cache;
  return cache;
}

QString& current_locale_hint_storage() {
  static QString locale_hint;
  return locale_hint;
}

QString resource_path_for_locale(const QString& locale_key) {
  return QStringLiteral(":/z7/i18n/z7_strings_%1.json").arg(locale_key);
}

QString source_path_for_locale(const QString& locale_key) {
#ifdef Z7_I18N_SOURCE_DIR
  return QDir(QStringLiteral(Z7_I18N_SOURCE_DIR))
      .filePath(QStringLiteral("z7_strings_%1.json").arg(locale_key));
#else
  Q_UNUSED(locale_key);
  return QString();
#endif
}

QJsonObject load_locale_root(const QString& locale_key) {
  const QStringList candidate_paths = {
      resource_path_for_locale(locale_key),
      source_path_for_locale(locale_key),
      QDir(QCoreApplication::applicationDirPath())
          .absoluteFilePath(
              QStringLiteral("../Resources/i18n/z7_strings_%1.json").arg(locale_key)),
      QDir(QCoreApplication::applicationDirPath())
          .absoluteFilePath(
              QStringLiteral("../Resources/z7_strings_%1.json").arg(locale_key))};

  for (const QString& path : candidate_paths) {
    if (path.trimmed().isEmpty()) {
      continue;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
      continue;
    }
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    if (document.isObject()) {
      return document.object();
    }
  }
  return {};
}

const QJsonObject& locale_root(const QString& locale_key) {
  QMutexLocker locker(&i18n_mutex());
  LocaleDocument& entry = locale_cache()[locale_key];
  if (!entry.loaded) {
    entry.root = load_locale_root(locale_key);
    entry.loaded = true;
  }
  return entry.root;
}

QString resolve_nested_value(const QJsonObject& root, QStringView key) {
  if (root.isEmpty() || key.isEmpty()) {
    return QString();
  }

  QJsonValue current(root);
  const QStringList segments = key.toString().split(QLatin1Char('.'));
  for (const QString& segment : segments) {
    if (!current.isObject()) {
      return QString();
    }
    current = current.toObject().value(segment);
    if (current.isUndefined()) {
      return QString();
    }
  }
  return current.isString() ? current.toString() : QString();
}

QString marker_for_key(QStringView key) {
  return QStringLiteral("!%1!").arg(key.toString());
}

QString localized_text_for_key(QStringView key, QStringView locale_hint) {
  const QString effective_hint =
      locale_hint.isEmpty() ? z7::i18n::internal::current_locale_hint()
                            : locale_hint.toString();
  const QString locale_key = z7::i18n::locale_key_from_hint(effective_hint);
  const QString localized = resolve_nested_value(locale_root(locale_key), key).trimmed();
  if (!localized.isEmpty()) {
    return localized;
  }
  const QString english = resolve_nested_value(locale_root(QStringLiteral("en")), key).trimmed();
  if (!english.isEmpty()) {
    return english;
  }
  return marker_for_key(key);
}

QString replace_placeholders(QString pattern, const QStringList& args) {
  for (int i = 0; i < args.size(); ++i) {
    pattern.replace(QStringLiteral("{%1}").arg(i), args.at(i));
  }
  return pattern;
}

void ensure_resources_initialized() {
  static const bool initialized = []() {
    ensure_i18n_resources_initialized_global();
    return true;
  }();
  Q_UNUSED(initialized);
}

}  // namespace

namespace z7::i18n {

QString text(QStringView key) {
  return text(key, {});
}

QString text(QStringView key, QStringView locale_hint) {
  ensure_resources_initialized();
  return localized_text_for_key(key, locale_hint);
}

QString format(QStringView key, const QStringList& args) {
  return format(key, args, {});
}

QString format(QStringView key, const QStringList& args, QStringView locale_hint) {
  return replace_placeholders(text(key, locale_hint), args);
}

QString locale_key_from_hint(QString hint) {
  QString lowered = hint.trimmed().toLower();
  if (lowered.isEmpty()) {
    lowered = QLocale::system().name().toLower();
  }
  if (lowered.startsWith(QStringLiteral("zh"))) {
    return QStringLiteral("zh-CN");
  }
  return QStringLiteral("en");
}

}  // namespace z7::i18n

namespace z7::i18n::internal {

void set_current_locale_hint(QString locale_hint) {
  QMutexLocker locker(&i18n_mutex());
  current_locale_hint_storage() = locale_hint.trimmed();
}

QString current_locale_hint() {
  QMutexLocker locker(&i18n_mutex());
  return current_locale_hint_storage();
}

}  // namespace z7::i18n::internal
