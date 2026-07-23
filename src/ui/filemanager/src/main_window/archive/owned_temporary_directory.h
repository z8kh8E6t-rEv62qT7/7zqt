#pragma once

#include <QString>

namespace z7::ui::filemanager {

    class OwnedTemporaryDirectory {
    public:
        explicit OwnedTemporaryDirectory(QString prefix);
        ~OwnedTemporaryDirectory();

        OwnedTemporaryDirectory(OwnedTemporaryDirectory const&) = delete;
        OwnedTemporaryDirectory& operator=(OwnedTemporaryDirectory const&) = delete;

        bool isValid() const;
        QString path() const;
        bool autoRemove() const;
        void setAutoRemove(bool enabled);
        bool remove();

    private:
        QString path_;
        bool auto_remove_ = true;
    };

} // namespace z7::ui::filemanager
