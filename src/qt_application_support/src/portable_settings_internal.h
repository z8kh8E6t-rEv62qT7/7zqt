#pragma once

#include <functional>

#include <QJsonValue>
#include <QJsonObject>
#include <QLockFile>
#include <QMetaType>
#include <QMutex>
#include <QString>
#include <QVariant>

#include "portable_settings.h"

namespace z7::platform::qt::portable_settings_internal {

constexpr int kSettingsVersion = 1;
constexpr const char* kSettingsFileName = "settings.json";
constexpr const char* kSettingsLockFileName = "settings.json.lock";
constexpr int kLockTimeoutMs = 15000;
#ifdef Z7_TESTING
constexpr const char* kTestRootEnv = "Z7_TEST_PORTABLE_SETTINGS_ROOT";
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
QJsonValue variant_to_json_value(const QVariant& value);
QVariant json_value_to_variant(const QJsonValue& value,
                               const QVariant& default_value = QVariant());
bool ensure_root_schema(QJsonObject* root);
bool read_json_root(const QString& file_path,
                    QJsonObject* root_out,
                    QString* error_message);
bool write_json_root(const QString& file_path,
                     const QJsonObject& root,
                     QString* error_message);
bool ensure_writable_root(const QString& root_dir, QString* error_message);
QString default_portable_settings_root_for_application_dir(
    const QString& application_dir);
QString default_portable_settings_root_for_executable_hint(
    const QString& argv0_hint);
QString resolve_root_dir_unlocked();
bool ensure_initialized_locked(QString* error_message);
QString current_settings_file_path();
QJsonObject namespace_object(const QJsonObject& root,
                             PortableSettings::Scope scope,
                             const QString& app_name);
void assign_namespace_object(QJsonObject* root,
                             PortableSettings::Scope scope,
                             const QString& app_name,
                             const QJsonObject& object);
bool with_locked_root(
    const QString& file_path,
    const QString& lock_path,
    QString* error_message,
    const std::function<bool(QJsonObject* root, QString* op_error)>& fn);

}  // namespace z7::platform::qt::portable_settings_internal
