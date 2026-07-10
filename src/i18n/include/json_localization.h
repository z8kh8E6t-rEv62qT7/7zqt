#pragma once

#include <QString>
#include <QStringList>
#include <QStringView>

namespace z7::i18n {

    QString text(QStringView key);
    QString text(QStringView key, QStringView language_hint);
    QString format(QStringView key, QStringList const& args);
    QString format(QStringView key, QStringList const& args, QStringView language_hint);
    QString locale_key_from_hint(QString hint = {});

} // namespace z7::i18n
