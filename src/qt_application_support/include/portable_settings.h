#pragma once

#include <QString>
#include <QVariant>

namespace z7::platform::qt {

class PortableSettings {
 public:
  enum class Scope {
    kApp,
    kShared
  };

  PortableSettings();
  PortableSettings(QString organization, QString application);

  QVariant value(const QString& key,
                 const QVariant& default_value = QVariant()) const;
  void setValue(const QString& key, const QVariant& value);
  void remove(const QString& key);
  bool contains(const QString& key) const;
  void clear();
  void sync() const;

 private:
  Scope scope_ = Scope::kApp;
  QString app_name_;
};

bool initialize_portable_settings(QString* error_message = nullptr);
QString portable_settings_root_dir();
QString portable_settings_file_path();

#ifdef Z7_TESTING
// Redirect settings to a different root directory and reset initialization
// state. Production defaults always resolve to ~/.config/7zqt/settings.json.
void set_portable_settings_root(const QString& root_dir);
#endif

}  // namespace z7::platform::qt
