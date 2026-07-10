#pragma once

#include <QString>
#include <QStringList>
#include <string>
#include <string_view>
#include <vector>

namespace z7::ui::archive_support {

    QString from_native_string(std::string_view value);
    std::string to_native_string(QString const& value);
    QString from_utf8_string(std::string_view value);
    std::string to_utf8_string(QString const& value);
    QString from_local8_string(std::string_view value);

    std::vector<std::string> to_native_string_list(QStringList const& list);
    std::vector<std::string> to_utf8_string_list(QStringList const& list);

} // namespace z7::ui::archive_support
