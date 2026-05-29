#include "portable_settings.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QMutex>
#include <QSaveFile>
#include <QVariant>

#include "portable_settings_internal.h"

namespace z7::platform::qt {

using namespace portable_settings_internal;

PortableSettings::PortableSettings()
    : scope_(Scope::kApp), app_name_(app_name_or_default()) {}

PortableSettings::PortableSettings(QString organization, QString application)
    : scope_(Scope::kApp), app_name_(std::move(application)) {
  Q_UNUSED(organization)
  if (app_name_.trimmed().isEmpty()) {
    app_name_ = app_name_or_default();
  }
  if (app_name_ == QStringLiteral("7z-shared")) {
    scope_ = Scope::kShared;
  }
}

QVariant PortableSettings::value(const QString& key,
                                 const QVariant& default_value) const {
  QString init_error;
  {
    QMutexLocker locker(&state_mutex());
    if (!ensure_initialized_locked(&init_error)) {
      return default_value;
    }
  }

  QString error_message;
  QVariant out = default_value;
  const QString file_path = portable_settings_file_path();
  const QString lock_path =
      QDir(portable_settings_root_dir()).filePath(QString::fromLatin1(kSettingsLockFileName));
  with_locked_root(file_path, lock_path, &error_message, [&](QJsonObject* root, QString*) {
    const QJsonObject ns = namespace_object(*root, scope_, app_name_);
    const QJsonValue val = ns.value(key);
    out = json_value_to_variant(val, default_value);
    return false;
  });
  return out;
}

void PortableSettings::setValue(const QString& key, const QVariant& value) {
  QString init_error;
  {
    QMutexLocker locker(&state_mutex());
    if (!ensure_initialized_locked(&init_error)) {
      return;
    }
  }

  QString error_message;
  const QString file_path = portable_settings_file_path();
  const QString lock_path =
      QDir(portable_settings_root_dir()).filePath(QString::fromLatin1(kSettingsLockFileName));
  with_locked_root(file_path, lock_path, &error_message, [&](QJsonObject* root, QString*) {
    QJsonObject ns = namespace_object(*root, scope_, app_name_);
    ns.insert(key, variant_to_json_value(value));
    assign_namespace_object(root, scope_, app_name_, ns);
    return true;
  });
}

void PortableSettings::remove(const QString& key) {
  QString init_error;
  {
    QMutexLocker locker(&state_mutex());
    if (!ensure_initialized_locked(&init_error)) {
      return;
    }
  }

  QString error_message;
  const QString file_path = portable_settings_file_path();
  const QString lock_path =
      QDir(portable_settings_root_dir()).filePath(QString::fromLatin1(kSettingsLockFileName));
  with_locked_root(file_path, lock_path, &error_message, [&](QJsonObject* root, QString*) {
    QJsonObject ns = namespace_object(*root, scope_, app_name_);
    if (!ns.contains(key)) {
      return false;
    }
    ns.remove(key);
    assign_namespace_object(root, scope_, app_name_, ns);
    return true;
  });
}

bool PortableSettings::contains(const QString& key) const {
  QString init_error;
  {
    QMutexLocker locker(&state_mutex());
    if (!ensure_initialized_locked(&init_error)) {
      return false;
    }
  }

  bool out = false;
  QString error_message;
  const QString file_path = portable_settings_file_path();
  const QString lock_path =
      QDir(portable_settings_root_dir()).filePath(QString::fromLatin1(kSettingsLockFileName));
  with_locked_root(file_path, lock_path, &error_message, [&](QJsonObject* root, QString*) {
    const QJsonObject ns = namespace_object(*root, scope_, app_name_);
    out = ns.contains(key);
    return false;
  });
  return out;
}

void PortableSettings::clear() {
  QString init_error;
  {
    QMutexLocker locker(&state_mutex());
    if (!ensure_initialized_locked(&init_error)) {
      return;
    }
  }

  QString error_message;
  const QString file_path = portable_settings_file_path();
  const QString lock_path =
      QDir(portable_settings_root_dir()).filePath(QString::fromLatin1(kSettingsLockFileName));
  with_locked_root(file_path, lock_path, &error_message, [&](QJsonObject* root, QString*) {
    assign_namespace_object(root, scope_, app_name_, QJsonObject{});
    return true;
  });
}

void PortableSettings::sync() const {}

bool initialize_portable_settings(QString* error_message) {
  QMutexLocker locker(&state_mutex());
  return ensure_initialized_locked(error_message);
}

QString portable_settings_root_dir() {
  QMutexLocker locker(&state_mutex());
  if (state().initialized) {
    return state().root_dir;
  }
  return resolve_root_dir_unlocked();
}

QString portable_settings_file_path() {
  QMutexLocker locker(&state_mutex());
  return current_settings_file_path();
}

#ifdef Z7_TESTING
void set_portable_settings_root(const QString& root_dir) {
  QMutexLocker locker(&state_mutex());
  state().test_root_override = root_dir;
  state().root_dir.clear();
  state().file_path.clear();
  state().initialized = false;
  state().init_error.clear();
}
#endif

}  // namespace z7::platform::qt
