// src/ui/filemanager/src/main_window/archive/archive_open_actions_tracking_platform.cpp
// Role: Minimal platform helpers for launching external Open outside targets.

#include "archive_open_actions_tracking.h"

#ifdef Q_OS_WIN
#include <shellapi.h>
#endif

namespace z7::ui::filemanager::archive_open_tracking {

#ifdef Q_OS_WIN
bool open_path_externally_with_process_handle(const QString& path,
                                              HANDLE* out_process) {
  if (out_process != nullptr) {
    *out_process = nullptr;
  }
  const QString normalized_path = QDir::toNativeSeparators(path);
  if (normalized_path.trimmed().isEmpty()) {
    return false;
  }

  std::wstring file_path = normalized_path.toStdWString();
  SHELLEXECUTEINFOW exec_info{};
  exec_info.cbSize = sizeof(exec_info);
  exec_info.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_DDEWAIT;
  exec_info.lpVerb = L"open";
  exec_info.lpFile = file_path.c_str();
  exec_info.nShow = SW_SHOWNORMAL;
  if (!::ShellExecuteExW(&exec_info)) {
    return false;
  }

  if (out_process != nullptr) {
    *out_process = exec_info.hProcess;
  } else if (exec_info.hProcess != nullptr) {
    ::CloseHandle(exec_info.hProcess);
  }
  return true;
}
#endif

}  // namespace z7::ui::filemanager::archive_open_tracking
