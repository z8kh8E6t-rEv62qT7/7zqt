// Role: Task-scoped process background mode control shared by UI runners.

#pragma once

namespace z7::ui::common {

class TaskBackgroundModeController final {
 public:
  TaskBackgroundModeController();
  TaskBackgroundModeController(const TaskBackgroundModeController&) = delete;
  TaskBackgroundModeController& operator=(const TaskBackgroundModeController&) = delete;
  TaskBackgroundModeController(TaskBackgroundModeController&&) = delete;
  TaskBackgroundModeController& operator=(TaskBackgroundModeController&&) = delete;
  ~TaskBackgroundModeController();

  void set_backgrounded(bool backgrounded);
  void restore();

 private:
  bool restored_ = false;
  bool has_original_state_ = false;
  int original_state_ = 0;
};

}  // namespace z7::ui::common
