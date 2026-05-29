#pragma once

#include <string>
#include <string_view>
#include <vector>

#include <QString>
#include <QStringList>

namespace z7::ui::archive_support {

QString from_native_string(std::string_view value);
std::string to_native_string(const QString& value);
QString from_utf8_string(std::string_view value);
std::string to_utf8_string(const QString& value);
QString from_local8_string(std::string_view value);

std::vector<std::string> to_native_string_list(const QStringList& list);
std::vector<std::string> to_utf8_string_list(const QStringList& list);

}  // namespace z7::ui::archive_support
