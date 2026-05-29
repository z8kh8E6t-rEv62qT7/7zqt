#include "settings_store_qt.h"

#include <QString>
#include <QVariant>

namespace z7::platform::qt {

SettingsStoreQt::SettingsStoreQt(QString organization, QString application)
    : organization_(std::move(organization)),
      application_(std::move(application)) {}

std::optional<std::string> SettingsStoreQt::load(std::string_view key) const {
  PortableSettings settings(organization_, application_);
  const QString key_qt = QString::fromUtf8(key.data(), static_cast<int>(key.size()));
  const QVariant value = settings.value(key_qt);
  if (!value.isValid()) {
    return std::nullopt;
  }

  const QByteArray utf8 = value.toString().toUtf8();
  return std::string(utf8.constData(), static_cast<size_t>(utf8.size()));
}

void SettingsStoreQt::save(std::string_view key, std::string_view value) {
  PortableSettings settings(organization_, application_);
  const QString key_qt = QString::fromUtf8(key.data(), static_cast<int>(key.size()));
  const QString value_qt = QString::fromUtf8(value.data(), static_cast<int>(value.size()));
  settings.setValue(key_qt, value_qt);
}

}  // namespace z7::platform::qt
