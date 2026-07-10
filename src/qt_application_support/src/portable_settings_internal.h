#pragma once

#include <QJsonObject>
#include <QJsonValue>
#include <QLockFile>
#include <QMetaType>
#include <QMutex>
#include <QString>
#include <QVariant>
#include <functional>

#include "portable_settings.h"

namespace z7::platform::qt::portable_settings_internal {

    constexpr int kSettingsVersion = 1;
    constexpr char const* kSettingsFileName = "settings.json";
    constexpr char const* kSettingsLockFileName = "settings.json.lock";
    constexpr int kLockTimeoutMs = 15000;
#ifdef Z7_TESTING
    constexpr char const* kTestRootEnv = "Z7_TEST_PORTABLE_SETTINGS_ROOT";
#endif

    struct State {
        QString root_dir;
        QString file_path;
#ifdef Z7_TESTING
        QString test_root_override;
#endif
        bool initialized = false;
        QString init_error;
    };

    QMutex& state_mutex();
    State& state();
    QString app_name_or_default();
    QJsonObject make_default_root();
    QString lock_error_to_string(QLockFile::LockError error);
    bool is_int_meta_type(QMetaType type);
    QJsonValue variant_to_json_value(QVariant const& value);
    QVariant json_value_to_variant(QJsonValue const& value, QVariant const& default_value = QVariant());
    bool ensure_root_schema(QJsonObject* root);
    bool read_json_root(QString const& file_path, QJsonObject* root_out, QString* error_message);
    bool write_json_root(QString const& file_path, QJsonObject const& root, QString* error_message);
    bool ensure_writable_root(QString const& root_dir, QString* error_message);
    QString default_portable_settings_root_for_application_dir(QString const& application_dir);
    QString default_portable_settings_root_for_executable_hint(QString const& argv0_hint);
    QString resolve_root_dir_unlocked();
    bool ensure_initialized_locked(QString* error_message);
    QString current_settings_file_path();
    QJsonObject namespace_object(QJsonObject const& root, PortableSettings::Scope scope, QString const& app_name);
    void assign_namespace_object(QJsonObject* root,
                                 PortableSettings::Scope scope,
                                 QString const& app_name,
                                 QJsonObject const& object);
    bool with_locked_root(QString const& file_path,
                          QString const& lock_path,
                          QString* error_message,
                          std::function<bool(QJsonObject* root, QString* op_error)> const& fn);

} // namespace z7::platform::qt::portable_settings_internal
