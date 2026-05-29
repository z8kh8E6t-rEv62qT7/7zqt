#pragma once

#include <QString>
#include <QStringList>
#include <QStringView>

namespace z7::ui::runtime_support {

QString J(QStringView key);
QString JF(QStringView key, const QStringList& args);

}  // namespace z7::ui::runtime_support
