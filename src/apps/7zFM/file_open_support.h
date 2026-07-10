#pragma once

#include <QApplication>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>
#include <functional>

namespace z7::apps::filemanager {

    struct OpenRequest {
        QString path;
        QString type_hint;
    };

    struct StartupOpenArgumentParseResult {
        QVector<OpenRequest> requests;
        bool has_recognized_open_args = false;
    };

    StartupOpenArgumentParseResult parse_startup_open_arguments(QStringList const& arguments);

    class FileOpenApplication : public QApplication {
        Q_OBJECT

    public:
        using OpenRequestHandler = std::function<void(OpenRequest const&)>;

        FileOpenApplication(int& argc, char** argv);

        void enqueue_open_requests(QVector<OpenRequest> const& requests);
        QVector<OpenRequest> take_pending_open_requests();
        void remember_dispatched_startup_open_requests(QVector<OpenRequest> const& requests);
        void set_open_request_handler(OpenRequestHandler handler);

    protected:
        bool event(QEvent* event) override;

    private:
        void submit_open_request(OpenRequest const& request);

        QVector<OpenRequest> pending_open_requests_;
        QSet<QString> pending_open_request_keys_;
        QSet<QString> startup_duplicate_skip_keys_;
        OpenRequestHandler open_request_handler_;
        bool collecting_startup_open_requests_ = true;
    };

    void dispatch_startup_open_requests(QVector<OpenRequest> const& requests,
                                        std::function<void(OpenRequest const&)> const& open_in_primary_window,
                                        std::function<void(OpenRequest const&)> const& open_in_new_window);

} // namespace z7::apps::filemanager
