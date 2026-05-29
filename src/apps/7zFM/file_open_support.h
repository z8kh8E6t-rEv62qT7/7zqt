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

StartupOpenArgumentParseResult parse_startup_open_arguments(
    const QStringList& arguments);

class FileOpenApplication : public QApplication {
  Q_OBJECT

 public:
  using OpenRequestHandler = std::function<void(const OpenRequest&)>;

  FileOpenApplication(int& argc, char** argv);

  void enqueue_open_requests(const QVector<OpenRequest>& requests);
  QVector<OpenRequest> take_pending_open_requests();
  void remember_dispatched_startup_open_requests(
      const QVector<OpenRequest>& requests);
  void set_open_request_handler(OpenRequestHandler handler);

 protected:
  bool event(QEvent* event) override;

 private:
  void submit_open_request(const OpenRequest& request);

  QVector<OpenRequest> pending_open_requests_;
  QSet<QString> pending_open_request_keys_;
  QSet<QString> startup_duplicate_skip_keys_;
  OpenRequestHandler open_request_handler_;
  bool collecting_startup_open_requests_ = true;
};

void dispatch_startup_open_requests(
    const QVector<OpenRequest>& requests,
    const std::function<void(const OpenRequest&)>& open_in_primary_window,
    const std::function<void(const OpenRequest&)>& open_in_new_window);

}  // namespace z7::apps::filemanager
