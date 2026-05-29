#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "portable_settings.h"

namespace z7::platform::qt {

class SettingsStoreQt final {
 public:
  explicit SettingsStoreQt(QString organization = QStringLiteral("7z2600"),
                           QString application = QStringLiteral("7zFM"));

  std::optional<std::string> load(std::string_view key) const;
  void save(std::string_view key, std::string_view value);

 private:
  QString organization_;
  QString application_;
};

}  // namespace z7::platform::qt
