// src/ui/filemanager/src/main_window/view/view_widgets.cpp
// Role: Small view helper widget implementations for main window internals.

#include "main_window/deps.h"
#include "main_window/internal.h"

namespace z7::ui::filemanager {

PathComboBox::PathComboBox(QWidget* parent)
    : QComboBox(parent) {}

void PathComboBox::showPopup() {
  if (before_show_popup) {
    before_show_popup();
  }
  QComboBox::showPopup();
}

}  // namespace z7::ui::filemanager
