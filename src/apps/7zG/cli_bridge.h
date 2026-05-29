#pragma once

#include <QString>
#include <QStringList>

#include "task_ipc_runtime.h"

namespace z7::apps::gui {

struct CliValidationResult {
  bool ok = false;
  bool no_command = false;
  bool suppress_gui_error = false;
  QString error_message;
};

struct CliWorkerResult {
  int exit_code = 2;
  QString summary;
  bool error_dialog_shown = false;
};

CliValidationResult validate_cli_arguments(const QStringList& argv);

int run_cli_launcher(const QStringList& argv);

CliWorkerResult run_cli_worker_payload(
    const z7::task_ipc_runtime::TaskIpcCliPayload& payload);

}  // namespace z7::apps::gui
