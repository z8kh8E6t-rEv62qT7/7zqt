#include "file_open_support.h"

#include <QEvent>
#include <QFileInfo>
#include <QFileOpenEvent>
#include <QUrl>

namespace z7::apps::filemanager {

    namespace {

        QString normalized_existing_path(QString const& candidate) {
            QString const trimmed = candidate.trimmed();
            if (trimmed.isEmpty()) {
                return QString();
            }

            QFileInfo const info(trimmed);
            if (!info.exists()) {
                return QString();
            }
            return info.absoluteFilePath();
        }

        QString local_path_from_file_open_event(QFileOpenEvent const& event) {
            QUrl const url = event.url();
            if (url.isLocalFile()) {
                return normalized_existing_path(url.toLocalFile());
            }
            return normalized_existing_path(event.file());
        }

        QString pending_open_request_key(OpenRequest const& request) {
            QString key = request.path;
            key.append(QChar(0));
            key.append(request.type_hint);
            return key;
        }

        bool normalize_open_request(OpenRequest const& request, OpenRequest* normalized_request) {
            if (normalized_request == nullptr) {
                return false;
            }

            QString const normalized_path = normalized_existing_path(request.path);
            if (normalized_path.isEmpty()) {
                return false;
            }

            *normalized_request = request;
            normalized_request->path = normalized_path;
            normalized_request->type_hint = normalized_request->type_hint.trimmed();
            return true;
        }

    } // namespace

    StartupOpenArgumentParseResult parse_startup_open_arguments(QStringList const& arguments) {
        StartupOpenArgumentParseResult result;
        QString startup_type_hint;
        bool startup_type_hint_consumed = false;

        for (int i = 1; i < arguments.size(); ++i) {
            QString const argument = arguments.at(i).trimmed();
            if (argument.isEmpty()) {
                continue;
            }

            if (argument.startsWith(QStringLiteral("-t")) && argument.size() > 2) {
                result.has_recognized_open_args = true;
                if (startup_type_hint.isEmpty()) {
                    startup_type_hint = argument.mid(2).trimmed();
                }
                continue;
            }

            QString const normalized_path = normalized_existing_path(argument);
            if (normalized_path.isEmpty()) {
                continue;
            }

            OpenRequest request;
            request.path = normalized_path;
            if (!startup_type_hint_consumed) {
                request.type_hint = startup_type_hint;
                startup_type_hint_consumed = true;
            }
            result.has_recognized_open_args = true;
            result.requests.push_back(request);
        }

        return result;
    }

    FileOpenApplication::FileOpenApplication(int& argc, char** argv) : QApplication(argc, argv) {}

    void FileOpenApplication::enqueue_open_requests(QVector<OpenRequest> const& requests) {
        for (OpenRequest const& request : requests) {
            submit_open_request(request);
        }
    }

    QVector<OpenRequest> FileOpenApplication::take_pending_open_requests() {
        QVector<OpenRequest> const requests = pending_open_requests_;
        pending_open_requests_.clear();
        pending_open_request_keys_.clear();
        return requests;
    }

    void FileOpenApplication::remember_dispatched_startup_open_requests(QVector<OpenRequest> const& requests) {
        for (OpenRequest const& request : requests) {
            OpenRequest normalized_request;
            if (!normalize_open_request(request, &normalized_request)) {
                continue;
            }
            startup_duplicate_skip_keys_.insert(pending_open_request_key(normalized_request));
        }
    }

    void FileOpenApplication::set_open_request_handler(OpenRequestHandler handler) {
        open_request_handler_ = std::move(handler);
        collecting_startup_open_requests_ = !static_cast<bool>(open_request_handler_);
    }

    bool FileOpenApplication::event(QEvent* event) {
        if (event != nullptr && event->type() == QEvent::FileOpen) {
            auto const* file_open_event = static_cast<QFileOpenEvent*>(event);
            OpenRequest request;
            request.path = local_path_from_file_open_event(*file_open_event);
            submit_open_request(request);
            return true;
        }
        return QApplication::event(event);
    }

    void FileOpenApplication::submit_open_request(OpenRequest const& request) {
        OpenRequest normalized_request;
        if (!normalize_open_request(request, &normalized_request)) {
            return;
        }

        QString const key = pending_open_request_key(normalized_request);
        if (collecting_startup_open_requests_ || !open_request_handler_) {
            if (pending_open_request_keys_.contains(key)) {
                return;
            }
            pending_open_request_keys_.insert(key);
            pending_open_requests_.push_back(normalized_request);
            return;
        }

        if (startup_duplicate_skip_keys_.remove(key)) {
            return;
        }

        if (open_request_handler_) {
            open_request_handler_(normalized_request);
        }
    }

    void dispatch_startup_open_requests(QVector<OpenRequest> const& requests,
                                        std::function<void(OpenRequest const&)> const& open_in_primary_window,
                                        std::function<void(OpenRequest const&)> const& open_in_new_window) {
        if (requests.isEmpty()) {
            return;
        }

        open_in_primary_window(requests.front());
        for (qsizetype i = 1; i < requests.size(); ++i) {
            open_in_new_window(requests.at(i));
        }
    }

} // namespace z7::apps::filemanager
