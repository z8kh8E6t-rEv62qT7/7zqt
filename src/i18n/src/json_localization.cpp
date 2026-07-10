#include "json_localization.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QMutex>
#include <QMutexLocker>
#include <QStringList>

#include "json_localization_internal.h"

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

    QString& current_language_hint_storage() {
        static QString language_hint;
        return language_hint;
    }

    QString resource_path_for_locale(QString const& locale_key) {
        return QStringLiteral(":/z7/i18n/z7_strings_%1.json").arg(locale_key);
    }

    QString source_path_for_locale(QString const& locale_key) {
#ifdef Z7_I18N_SOURCE_DIR
        return QDir(QStringLiteral(Z7_I18N_SOURCE_DIR)).filePath(QStringLiteral("z7_strings_%1.json").arg(locale_key));
#else
        Q_UNUSED(locale_key);
        return QString();
#endif
    }

    QJsonObject load_locale_root(QString const& locale_key) {
        QStringList const candidate_paths = {
            resource_path_for_locale(locale_key),
            source_path_for_locale(locale_key),
            QDir(QCoreApplication::applicationDirPath())
                .absoluteFilePath(QStringLiteral("../Resources/i18n/z7_strings_%1.json").arg(locale_key)),
            QDir(QCoreApplication::applicationDirPath())
                .absoluteFilePath(QStringLiteral("../Resources/z7_strings_%1.json").arg(locale_key))};

        for (QString const& path : candidate_paths) {
            if (path.trimmed().isEmpty()) {
                continue;
            }
            QFile file(path);
            if (!file.open(QIODevice::ReadOnly)) {
                continue;
            }
            QJsonDocument const document = QJsonDocument::fromJson(file.readAll());
            if (document.isObject()) {
                return document.object();
            }
        }
        return {};
    }

    QJsonObject const& locale_root(QString const& locale_key) {
        QMutexLocker locker(&i18n_mutex());
        LocaleDocument& entry = locale_cache()[locale_key];
        if (!entry.loaded) {
            entry.root = load_locale_root(locale_key);
            entry.loaded = true;
        }
        return entry.root;
    }

    QString resolve_nested_value(QJsonObject const& root, QStringView key) {
        if (root.isEmpty() || key.isEmpty()) {
            return QString();
        }

        QJsonValue current(root);
        QStringList const segments = key.toString().split(QLatin1Char('.'));
        for (QString const& segment : segments) {
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

    QString localized_text_for_key(QStringView key, QStringView language_hint) {
        QString const effective_hint =
            language_hint.isEmpty() ? z7::i18n::internal::current_language_hint() : language_hint.toString();
        QString const locale_key = z7::i18n::locale_key_from_hint(effective_hint);
        QString const localized = resolve_nested_value(locale_root(locale_key), key).trimmed();
        if (!localized.isEmpty()) {
            return localized;
        }
        QString const english = resolve_nested_value(locale_root(QStringLiteral("en")), key).trimmed();
        if (!english.isEmpty()) {
            return english;
        }
        return marker_for_key(key);
    }

    QString replace_placeholders(QString pattern, QStringList const& args) {
        for (int i = 0; i < args.size(); ++i) {
            pattern.replace(QStringLiteral("{%1}").arg(i), args.at(i));
        }
        return pattern;
    }

    void ensure_resources_initialized() {
        static bool const initialized = []() {
            ensure_i18n_resources_initialized_global();
            return true;
        }();
        Q_UNUSED(initialized);
    }

} // namespace

namespace z7::i18n {

    QString text(QStringView key) {
        return text(key, {});
    }

    QString text(QStringView key, QStringView language_hint) {
        ensure_resources_initialized();
        return localized_text_for_key(key, language_hint);
    }

    QString format(QStringView key, QStringList const& args) {
        return format(key, args, {});
    }

    QString format(QStringView key, QStringList const& args, QStringView language_hint) {
        return replace_placeholders(text(key, language_hint), args);
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

} // namespace z7::i18n

namespace z7::i18n::internal {

    void set_current_language_hint(QString language_hint) {
        QMutexLocker locker(&i18n_mutex());
        current_language_hint_storage() = language_hint.trimmed();
    }

    QString current_language_hint() {
        QMutexLocker locker(&i18n_mutex());
        return current_language_hint_storage();
    }

} // namespace z7::i18n::internal
