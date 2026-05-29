#pragma once

#include <QString>
#include <QStringList>
#include <QStringView>

namespace z7::i18n {

QString text(QStringView key);
QString text(QStringView key, QStringView locale_hint);
QString format(QStringView key, const QStringList& args);
QString format(QStringView key, const QStringList& args, QStringView locale_hint);
QString locale_key_from_hint(QString hint = {});

}  // namespace z7::i18n
