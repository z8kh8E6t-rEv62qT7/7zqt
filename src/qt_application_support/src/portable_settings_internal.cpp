#include "portable_settings_internal.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QSaveFile>
#include <QtGlobal>
#include <optional>

namespace z7::platform::qt::portable_settings_internal {

    namespace {

        constexpr char const* kTypedValueMarkerKey = "__z7_type";
        constexpr char const* kByteArrayTypeName = "QByteArray";
        constexpr char const* kByteArrayBase64Key = "base64";

        QJsonObject byte_array_to_json_object(QByteArray const& bytes) {
            QJsonObject object;
            object.insert(QString::fromLatin1(kTypedValueMarkerKey), QString::fromLatin1(kByteArrayTypeName));
            object.insert(QString::fromLatin1(kByteArrayBase64Key), QString::fromLatin1(bytes.toBase64()));
            return object;
        }

        std::optional<QByteArray> byte_array_from_json_object(QJsonObject const& object) {
            if (object.value(QString::fromLatin1(kTypedValueMarkerKey)).toString()
                != QString::fromLatin1(kByteArrayTypeName)) {
                return std::nullopt;
            }

            QJsonValue const encoded_value = object.value(QString::fromLatin1(kByteArrayBase64Key));
            if (!encoded_value.isString()) {
                return QByteArray();
            }
            return QByteArray::fromBase64(encoded_value.toString().toLatin1());
        }

    } // namespace

    QMutex& state_mutex() {
        static QMutex mutex;
        return mutex;
    }

    State& state() {
        static State s;
        return s;
    }

    QString app_name_or_default() {
        QString const app_name = QCoreApplication::applicationName().trimmed();
        return app_name.isEmpty() ? QStringLiteral("7zFM") : app_name;
    }

    QJsonObject make_default_root() {
        QJsonObject root;
        root.insert(QStringLiteral("version"), kSettingsVersion);
        root.insert(QStringLiteral("apps"), QJsonObject{});
        root.insert(QStringLiteral("shared"), QJsonObject{});
        return root;
    }

    QString lock_error_to_string(QLockFile::LockError error) {
        switch (error) {
            case QLockFile::NoError:
                return QStringLiteral("no error");
            case QLockFile::LockFailedError:
                return QStringLiteral("lock failed");
            case QLockFile::PermissionError:
                return QStringLiteral("permission denied");
            case QLockFile::UnknownError:
            default:
                return QStringLiteral("unknown error");
        }
    }

    bool is_int_meta_type(QMetaType const type) {
        int const id = type.id();
        return id == QMetaType::Int
            || id == QMetaType::UInt
            || id == QMetaType::LongLong
            || id == QMetaType::ULongLong
            || id == QMetaType::Short
            || id == QMetaType::UShort
            || id == QMetaType::Long
            || id == QMetaType::ULong
            || id == QMetaType::Char
            || id == QMetaType::SChar
            || id == QMetaType::UChar
            || id == QMetaType::Bool;
    }

    QJsonArray variant_list_to_json_array(QVariantList const& list) {
        QJsonArray array;
        for (QVariant const& item : list) {
            array.append(variant_to_json_value(item));
        }
        return array;
    }

    QJsonValue variant_to_json_value(QVariant const& value) {
        if (!value.isValid()) {
            return QJsonValue();
        }

        QMetaType const type = value.metaType();
        if (type.id() == QMetaType::QStringList) {
            QStringList const list = value.toStringList();
            QJsonArray array;
            for (QString const& item : list) {
                array.append(item);
            }
            return array;
        }
        if (type.id() == QMetaType::QVariantList) {
            return variant_list_to_json_array(value.toList());
        }
        if (type.id() == QMetaType::QByteArray) {
            return byte_array_to_json_object(value.toByteArray());
        }
        if (type.id() == QMetaType::Bool) {
            return QJsonValue(value.toBool());
        }
        if (is_int_meta_type(type) && type.id() != QMetaType::Bool) {
            return QJsonValue(static_cast<double>(value.toLongLong()));
        }
        if (type.id() == QMetaType::Double || type.id() == QMetaType::Float) {
            return QJsonValue(value.toDouble());
        }

        return QJsonValue(value.toString());
    }

    QVariant json_value_to_variant(QJsonValue const& value, QVariant const& default_value) {
        if (value.isUndefined() || value.isNull()) {
            return default_value;
        }
        if (value.isBool()) {
            return QVariant(value.toBool());
        }
        if (value.isDouble()) {
            double const as_double = value.toDouble();
            qint64 const as_int = static_cast<qint64>(as_double);
            if (static_cast<double>(as_int) == as_double) {
                return QVariant(as_int);
            }
            return QVariant(as_double);
        }
        if (value.isString()) {
            return QVariant(value.toString());
        }
        if (value.isArray()) {
            QJsonArray const array = value.toArray();
            QStringList string_list;
            string_list.reserve(array.size());
            QVariantList variant_list;
            variant_list.reserve(array.size());
            bool all_strings = true;
            for (QJsonValue const& element : array) {
                if (!element.isString()) {
                    all_strings = false;
                } else {
                    string_list.push_back(element.toString());
                }
                variant_list.push_back(json_value_to_variant(element, QVariant()));
            }

            if (default_value.metaType().id() == QMetaType::QStringList) {
                if (all_strings) {
                    return QVariant(string_list);
                }
                QStringList coerced;
                coerced.reserve(variant_list.size());
                for (QVariant const& element : variant_list) {
                    coerced.push_back(element.toString());
                }
                return QVariant(coerced);
            }
            if (default_value.metaType().id() == QMetaType::QVariantList) {
                return QVariant(variant_list);
            }
            if (all_strings) {
                return QVariant(string_list);
            }
            return QVariant(variant_list);
        }
        if (value.isObject()) {
            QJsonObject const object = value.toObject();
            if (std::optional<QByteArray> bytes = byte_array_from_json_object(object); bytes.has_value()) {
                return QVariant(*bytes);
            }
            return QVariant(QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact)));
        }
        return default_value;
    }

    bool ensure_root_schema(QJsonObject* root) {
        if (root == nullptr) {
            return false;
        }

        if (!root->contains(QStringLiteral("version"))) {
            root->insert(QStringLiteral("version"), kSettingsVersion);
        }

        QJsonValue const apps_value = root->value(QStringLiteral("apps"));
        if (!apps_value.isObject()) {
            root->insert(QStringLiteral("apps"), QJsonObject{});
        }

        QJsonValue const shared_value = root->value(QStringLiteral("shared"));
        if (!shared_value.isObject()) {
            root->insert(QStringLiteral("shared"), QJsonObject{});
        }

        return true;
    }

    bool read_json_root(QString const& file_path, QJsonObject* root_out, QString* error_message) {
        if (root_out == nullptr) {
            if (error_message != nullptr) {
                *error_message = QStringLiteral("Internal error: null output root.");
            }
            return false;
        }

        QFile file(file_path);
        if (!file.exists()) {
            *root_out = make_default_root();
            return true;
        }
        if (!file.open(QIODevice::ReadOnly)) {
            if (error_message != nullptr) {
                *error_message = QStringLiteral("Cannot open settings file: %1").arg(file.errorString());
            }
            return false;
        }

        QJsonParseError parse_error;
        QJsonDocument const doc = QJsonDocument::fromJson(file.readAll(), &parse_error);
        if (parse_error.error != QJsonParseError::NoError || !doc.isObject()) {
            if (error_message != nullptr) {
                *error_message = QStringLiteral("Invalid JSON in settings file: %1").arg(parse_error.errorString());
            }
            return false;
        }

        *root_out = doc.object();
        ensure_root_schema(root_out);
        return true;
    }

    bool write_json_root(QString const& file_path, QJsonObject const& root, QString* error_message) {
        QSaveFile file(file_path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            if (error_message != nullptr) {
                *error_message = QStringLiteral("Cannot open settings file for writing: %1").arg(file.errorString());
            }
            return false;
        }

        QJsonDocument doc(root);
        QByteArray const bytes = doc.toJson(QJsonDocument::Indented);
        if (file.write(bytes) != bytes.size()) {
            if (error_message != nullptr) {
                *error_message = QStringLiteral("Cannot write settings file: %1").arg(file.errorString());
            }
            file.cancelWriting();
            return false;
        }

        if (!file.commit()) {
            if (error_message != nullptr) {
                *error_message = QStringLiteral("Cannot commit settings file: %1").arg(file.errorString());
            }
            return false;
        }

        return true;
    }

    bool ensure_writable_root(QString const& root_dir, QString* error_message) {
        QDir dir(root_dir);
        if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
            if (error_message != nullptr) {
                *error_message = QStringLiteral("Cannot create config directory: %1").arg(root_dir);
            }
            return false;
        }

        QFileInfo info(root_dir);
        if (!info.exists() || !info.isDir()) {
            if (error_message != nullptr) {
                *error_message = QStringLiteral("Config path is not a directory: %1").arg(root_dir);
            }
            return false;
        }

        QString const probe_path = dir.filePath(QStringLiteral(".write_test"));
        QFile probe(probe_path);
        if (!probe.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            if (error_message != nullptr) {
                *error_message = QStringLiteral("Config directory is not writable: %1").arg(root_dir);
            }
            return false;
        }
        probe.close();
        probe.remove();
        return true;
    }

    QString global_portable_settings_root() {
        return QDir::home().filePath(QStringLiteral(".config/7zqt"));
    }

#ifdef Z7_TESTING
    QString environment_portable_settings_root() {
        return qEnvironmentVariable(kTestRootEnv).trimmed();
    }
#endif

    QString default_portable_settings_root_for_application_dir(QString const& application_dir) {
        QString const clean_dir = QDir::cleanPath(application_dir.trimmed());
        if (clean_dir.isEmpty()) {
            return QString();
        }
#ifdef Z7_TESTING
        QString const test_env_root = environment_portable_settings_root();
        if (!test_env_root.isEmpty()) {
            return test_env_root;
        }
#endif
        return global_portable_settings_root();
    }

    QString default_portable_settings_root_for_executable_hint(QString const& argv0_hint) {
        QString const trimmed_hint = argv0_hint.trimmed();
        if (trimmed_hint.isEmpty()) {
            return QString();
        }

        QFileInfo const exe_info(trimmed_hint);
        QString const absolute_exe_path = exe_info.isAbsolute()
                                            ? exe_info.absoluteFilePath()
                                            : QFileInfo(QDir::current(), trimmed_hint).absoluteFilePath();
        if (absolute_exe_path.isEmpty()) {
            return QString();
        }

        return default_portable_settings_root_for_application_dir(QFileInfo(absolute_exe_path).absolutePath());
    }

    QString resolve_root_dir_unlocked() {
#ifdef Z7_TESTING
        if (!state().test_root_override.trimmed().isEmpty()) {
            return state().test_root_override;
        }
        QString const test_env_root = environment_portable_settings_root();
        if (!test_env_root.isEmpty()) {
            return test_env_root;
        }
#endif
        return global_portable_settings_root();
    }

    bool ensure_initialized_locked(QString* error_message) {
        if (state().initialized) {
            return true;
        }

        QString init_error;
        QString const root_dir = resolve_root_dir_unlocked();
        if (!ensure_writable_root(root_dir, &init_error)) {
            state().init_error = init_error;
            if (error_message != nullptr) {
                *error_message = init_error;
            }
            return false;
        }

        QString const file_path = QDir(root_dir).filePath(QString::fromLatin1(kSettingsFileName));
        QString const lock_path = QDir(root_dir).filePath(QString::fromLatin1(kSettingsLockFileName));

        QLockFile lock(lock_path);
        lock.setStaleLockTime(0);
        if (!lock.tryLock(kLockTimeoutMs)) {
            init_error = QStringLiteral("Cannot lock settings file: %1")
                             .arg(lock.error() == QLockFile::NoError ? QStringLiteral("timeout")
                                                                     : lock_error_to_string(lock.error()));
            state().init_error = init_error;
            if (error_message != nullptr) {
                *error_message = init_error;
            }
            return false;
        }

        QJsonObject root;
        if (!read_json_root(file_path, &root, &init_error)) {
            lock.unlock();
            state().init_error = init_error;
            if (error_message != nullptr) {
                *error_message = init_error;
            }
            return false;
        }
        ensure_root_schema(&root);

        if (!QFile::exists(file_path)) {
            if (!write_json_root(file_path, root, &init_error)) {
                lock.unlock();
                state().init_error = init_error;
                if (error_message != nullptr) {
                    *error_message = init_error;
                }
                return false;
            }
        }

        lock.unlock();
        state().root_dir = root_dir;
        state().file_path = file_path;
        state().initialized = true;
        state().init_error.clear();
        return true;
    }

    QString current_settings_file_path() {
        if (state().initialized) {
            return state().file_path;
        }
        return QDir(resolve_root_dir_unlocked()).filePath(QString::fromLatin1(kSettingsFileName));
    }

    QJsonObject namespace_object(QJsonObject const& root, PortableSettings::Scope scope, QString const& app_name) {
        if (scope == PortableSettings::Scope::kShared) {
            return root.value(QStringLiteral("shared")).toObject();
        }

        QJsonObject const apps = root.value(QStringLiteral("apps")).toObject();
        return apps.value(app_name).toObject();
    }

    void assign_namespace_object(QJsonObject* root,
                                 PortableSettings::Scope scope,
                                 QString const& app_name,
                                 QJsonObject const& object) {
        if (root == nullptr) {
            return;
        }
        if (scope == PortableSettings::Scope::kShared) {
            root->insert(QStringLiteral("shared"), object);
            return;
        }

        QJsonObject apps = root->value(QStringLiteral("apps")).toObject();
        apps.insert(app_name, object);
        root->insert(QStringLiteral("apps"), apps);
    }

    bool with_locked_root(QString const& file_path,
                          QString const& lock_path,
                          QString* error_message,
                          std::function<bool(QJsonObject* root, QString* op_error)> const& fn) {
        QLockFile lock(lock_path);
        lock.setStaleLockTime(0);
        if (!lock.tryLock(kLockTimeoutMs)) {
            if (error_message != nullptr) {
                *error_message = QStringLiteral("Cannot lock settings file: %1")
                                     .arg(lock.error() == QLockFile::NoError ? QStringLiteral("timeout")
                                                                             : lock_error_to_string(lock.error()));
            }
            return false;
        }

        QJsonObject root;
        QString op_error;
        if (!read_json_root(file_path, &root, &op_error)) {
            lock.unlock();
            if (error_message != nullptr) {
                *error_message = op_error;
            }
            return false;
        }
        ensure_root_schema(&root);

        bool const needs_write = fn(&root, &op_error);
        if (!op_error.isEmpty()) {
            lock.unlock();
            if (error_message != nullptr) {
                *error_message = op_error;
            }
            return false;
        }

        if (needs_write && !write_json_root(file_path, root, &op_error)) {
            lock.unlock();
            if (error_message != nullptr) {
                *error_message = op_error;
            }
            return false;
        }

        lock.unlock();
        return true;
    }

} // namespace z7::platform::qt::portable_settings_internal
