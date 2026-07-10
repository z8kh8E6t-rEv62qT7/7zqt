#pragma once

#include <QFileInfo>
#include <QString>
#include <QtGlobal>

namespace z7::ui::filemanager {

    struct ExternalCommandParts {
        QString program;
        QString arguments;
    };

    inline bool external_command_candidate_looks_like_path(QString const& value) {
        return value.contains(QLatin1Char('/'))
            || value.contains(QLatin1Char('\\'))
#ifdef Q_OS_WIN
            || (value.size() >= 2 && value.at(1) == QLatin1Char(':'))
#endif
            ;
    }

    inline bool external_command_candidate_exists(QString const& value) {
        if (!external_command_candidate_looks_like_path(value)) {
            return false;
        }
        QFileInfo const info(value);
        return info.exists() && (info.isFile() || info.isSymLink());
    }

    inline int external_command_closing_quote(QString const& value) {
        for (int i = 1; i < value.size(); ++i) {
            if (value.at(i) == QLatin1Char('"')) {
                return i;
            }
        }
        return -1;
    }

    inline ExternalCommandParts parse_external_command_line(QString const& command_line) {
        QString command = command_line.trimmed();
        if (command.isEmpty()) {
            return {};
        }

        if (command.startsWith(QLatin1Char('"'))) {
            int const pos = external_command_closing_quote(command);
            if (pos >= 0 && (pos + 1 == command.size() || command.at(pos + 1).isSpace())) {
                return {command.mid(1, pos - 1).trimmed(), command.mid(pos + 1).trimmed()};
            }
        }

        if (external_command_candidate_exists(command)) {
            return {command, QString()};
        }

        int first_space = -1;
        for (int i = 0; i < command.size(); ++i) {
            if (!command.at(i).isSpace()) {
                continue;
            }
            if (first_space < 0) {
                first_space = i;
            }
        }

        if (first_space < 0) {
            return {command, QString()};
        }

        for (int i = command.size() - 1; i >= 0; --i) {
            if (!command.at(i).isSpace()) {
                continue;
            }
            QString const candidate = command.left(i).trimmed();
            if (external_command_candidate_exists(candidate)) {
                return {candidate, command.mid(i + 1).trimmed()};
            }
        }

        return {command.left(first_space).trimmed(), command.mid(first_space + 1).trimmed()};
    }

} // namespace z7::ui::filemanager
