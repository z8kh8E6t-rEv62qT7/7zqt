#pragma once

#include <QMainWindow>
#include <QMessageBox>
#include <QHash>
#include <QList>
#include <QModelIndex>
#include <QPointer>
#include <QPair>
#include <QSharedPointer>
#include <QString>
#include <QStringList>
#include <QSet>
#include <QVector>

#include <array>
#include <functional>
#include <memory>

#include "archive_process_runner.h"
#include "display_settings.h"

class QAction;
class QActionGroup;
class QAbstractItemView;
class QComboBox;
class QCloseEvent;
class QDropEvent;
class QEvent;
class QFileSystemWatcher;
class QItemSelectionModel;
class QKeyEvent;
class QLabel;
class QListView;
class QMenu;
class QPoint;
class QProcess;
class QSplitter;
class QStackedWidget;
class QStatusBar;
class QTemporaryDir;
class QTimer;
class QToolBar;
class QToolButton;
class QTreeView;

namespace z7::ui::filemanager {

class ArchiveProcessRunner;
class OptionsDialog;
class TaskProgressDialog;
class DirectoryListModel;
class DragAwareStructuredListView;
struct DropTargetInfo;
struct DragExecutionReport;

}  // namespace z7::ui::filemanager

namespace z7::ui::widgets {
class StructuredListSortFilterProxy;
}  // namespace z7::ui::widgets

namespace z7::app {
struct ListResult;
}

namespace z7::task_ipc_runtime {
struct TaskIpcOpenPayload;
struct TaskIpcPayload;
}  // namespace z7::task_ipc_runtime

namespace z7::ui::filemanager {

class MainWindow : public QMainWindow {
  Q_OBJECT

 public:
 explicit MainWindow(QWidget* parent = nullptr);
 ~MainWindow() override;

 protected:
  void closeEvent(QCloseEvent* event) override;
 bool eventFilter(QObject* watched, QEvent* event) override;

 private:
  #include "main_window_private_decls.h"
};

}  // namespace z7::ui::filemanager
