#pragma once

#include "task_progress_dialog_base.h"

namespace z7::ui::gui {

class TaskProgressDialog final : public z7::ui::runtime_support::TaskProgressDialogBase {
 public:
  explicit TaskProgressDialog(QWidget* parent = nullptr);
};

}  // namespace z7::ui::gui
