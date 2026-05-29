// Role: Task-scoped process background mode control.

#include "task_background_mode.h"

#include <algorithm>
#include <cerrno>

#include <QtGlobal>

#ifdef Q_OS_WIN
#include <windows.h>
#elif defined(Q_OS_UNIX)
#include <sys/resource.h>
#endif

namespace z7::ui::common {
namespace {

#if defined(Q_OS_UNIX)
int current_priority(int which, bool* ok) {
  if (ok != nullptr) {
    *ok = false;
  }
  errno = 0;
  const int value = getpriority(which, 0);
  if (value == -1 && errno != 0) {
    return 0;
  }
  if (ok != nullptr) {
    *ok = true;
  }
  return value;
}
#endif

}  // namespace

TaskBackgroundModeController::TaskBackgroundModeController() {
#ifdef Q_OS_WIN
  const DWORD priority_class = ::GetPriorityClass(::GetCurrentProcess());
  if (priority_class != 0) {
    has_original_state_ = true;
    original_state_ = static_cast<int>(priority_class);
  }
#elif defined(Q_OS_DARWIN) && defined(PRIO_DARWIN_PROCESS) && defined(PRIO_DARWIN_BG)
  bool ok = false;
  const int background_state = current_priority(PRIO_DARWIN_PROCESS, &ok);
  if (ok) {
    has_original_state_ = true;
    original_state_ = background_state != 0 ? 1 : 0;
  }
#elif defined(Q_OS_UNIX)
  bool ok = false;
  const int nice_value = current_priority(PRIO_PROCESS, &ok);
  if (ok) {
    has_original_state_ = true;
    original_state_ = nice_value;
  }
#endif
}

TaskBackgroundModeController::~TaskBackgroundModeController() {
  restore();
}

void TaskBackgroundModeController::set_backgrounded(bool backgrounded) {
  if (restored_) {
    return;
  }

#ifdef Q_OS_WIN
  const DWORD target = backgrounded
                           ? IDLE_PRIORITY_CLASS
                           : static_cast<DWORD>(has_original_state_
                                                    ? original_state_
                                                    : NORMAL_PRIORITY_CLASS);
  ::SetPriorityClass(::GetCurrentProcess(), target);
#elif defined(Q_OS_DARWIN) && defined(PRIO_DARWIN_PROCESS) && defined(PRIO_DARWIN_BG)
  const int target = backgrounded ? PRIO_DARWIN_BG : 0;
  setpriority(PRIO_DARWIN_PROCESS, 0, target);
#elif defined(Q_OS_UNIX)
  const int target = backgrounded
                         ? std::max(has_original_state_ ? original_state_ : 0, 10)
                         : (has_original_state_ ? original_state_ : 0);
  setpriority(PRIO_PROCESS, 0, target);
#else
  (void)backgrounded;
#endif
}

void TaskBackgroundModeController::restore() {
  if (restored_) {
    return;
  }
  restored_ = true;

#ifdef Q_OS_WIN
  if (has_original_state_) {
    ::SetPriorityClass(::GetCurrentProcess(),
                       static_cast<DWORD>(original_state_));
  }
#elif defined(Q_OS_DARWIN) && defined(PRIO_DARWIN_PROCESS) && defined(PRIO_DARWIN_BG)
  if (has_original_state_) {
    setpriority(PRIO_DARWIN_PROCESS,
                0,
                original_state_ != 0 ? PRIO_DARWIN_BG : 0);
  }
#elif defined(Q_OS_UNIX)
  if (has_original_state_) {
    setpriority(PRIO_PROCESS, 0, original_state_);
  }
#endif
}

}  // namespace z7::ui::common
