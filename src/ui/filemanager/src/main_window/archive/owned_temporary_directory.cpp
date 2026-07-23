#include "main_window/archive/owned_temporary_directory.h"

#include <QDir>
#include <QFileDevice>
#include <QRandomGenerator>

namespace z7::ui::filemanager {

    OwnedTemporaryDirectory::OwnedTemporaryDirectory(QString prefix) {
        QString const root_path = QDir::cleanPath(QDir::tempPath());
        if (root_path.trimmed().isEmpty() || (prefix != QStringLiteral("7zO") && prefix != QStringLiteral("7zE"))) {
            return;
        }

        QDir root(root_path);
        if (!root.exists() && !root.mkpath(QStringLiteral("."))) {
            return;
        }

        QFileDevice::Permissions const owner_permissions =
            QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner;
        for (int attempt = 0; attempt < 100; ++attempt) {
            quint32 const random_value = QRandomGenerator::system()->generate();
            QString const name =
                prefix + QString::number(random_value, 16).rightJustified(8, QLatin1Char('0')).toUpper();
            if (!root.mkdir(name, owner_permissions)) {
                continue;
            }
            path_ = root.filePath(name);
            return;
        }
    }

    OwnedTemporaryDirectory::~OwnedTemporaryDirectory() {
        if (auto_remove_) {
            remove();
        }
    }

    bool OwnedTemporaryDirectory::isValid() const {
        return !path_.isEmpty() && QDir(path_).exists();
    }

    QString OwnedTemporaryDirectory::path() const {
        return path_;
    }

    bool OwnedTemporaryDirectory::autoRemove() const {
        return auto_remove_;
    }

    void OwnedTemporaryDirectory::setAutoRemove(bool enabled) {
        auto_remove_ = enabled;
    }

    bool OwnedTemporaryDirectory::remove() {
        if (path_.isEmpty()) {
            return true;
        }
        QDir directory(path_);
        if (directory.exists() && !directory.removeRecursively()) {
            return false;
        }
        path_.clear();
        return true;
    }

} // namespace z7::ui::filemanager
