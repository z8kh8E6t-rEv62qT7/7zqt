#include "archive_string_codec_qt.h"

#include <QByteArray>
#include <QFile>

namespace z7::ui::archive_support {

QString from_native_string(std::string_view value) {
  return QFile::decodeName(
      QByteArray(value.data(), static_cast<int>(value.size())));
}

std::string to_native_string(const QString& value) {
  const QByteArray native = QFile::encodeName(value);
  return std::string(native.constData(), static_cast<size_t>(native.size()));
}

QString from_utf8_string(std::string_view value) {
  return QString::fromUtf8(value.data(), static_cast<int>(value.size()));
}

std::string to_utf8_string(const QString& value) {
  const QByteArray utf8 = value.toUtf8();
  return std::string(utf8.constData(), static_cast<size_t>(utf8.size()));
}

QString from_local8_string(std::string_view value) {
  return QString::fromLocal8Bit(value.data(), static_cast<int>(value.size()));
}

std::vector<std::string> to_native_string_list(const QStringList& list) {
  std::vector<std::string> out;
  out.reserve(static_cast<size_t>(list.size()));
  for (const QString& item : list) {
    out.push_back(to_native_string(item));
  }
  return out;
}

std::vector<std::string> to_utf8_string_list(const QStringList& list) {
  std::vector<std::string> out;
  out.reserve(static_cast<size_t>(list.size()));
  for (const QString& item : list) {
    out.push_back(to_utf8_string(item));
  }
  return out;
}

}  // namespace z7::ui::archive_support
