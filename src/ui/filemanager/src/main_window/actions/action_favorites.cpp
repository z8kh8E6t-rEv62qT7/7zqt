// src/ui/filemanager/src/main_window/actions/action_favorites.cpp
// Role: Favorites menu behaviors and persistent bookmark slots.

#include "main_window/deps.h"
#include "main_window/internal.h"

#if defined(Q_OS_WIN)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace z7::ui::filemanager {
namespace {

constexpr int kFavoriteSlotCount = 10;
constexpr auto kFolderShortcutsKey = "FM/FolderShortcuts";
#if defined(Q_OS_MACOS)
// Qt preserves the AppKit/IOKit device-specific right-Control flag here.
constexpr quint32 kMacDeviceRightControlKeyMask = 0x00002000;
#endif

QString favorite_set_slot_label(int slot) {
  return QStringLiteral("%1 %2\tAlt+Shift+%2")
      .arg(z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(801)))
      .arg(slot);
}

QString favorite_display_label(int slot, QString path) {
  if (path.isEmpty()) {
    path = QStringLiteral("-");
  }
  reduce_menu_string(&path);
  return QStringLiteral("%1: %2\tAlt+%1").arg(slot).arg(path);
}

bool favorite_slot_is_valid(int slot) {
  return slot >= 0 && slot < kFavoriteSlotCount;
}

std::optional<int> favorite_slot_from_digit_key(int key) {
  if (key < Qt::Key_0 || key > Qt::Key_9) {
    return std::nullopt;
  }
  return key - Qt::Key_0;
}

QStringList load_folder_shortcuts(
    const z7::platform::qt::PortableSettings& settings) {
  QStringList shortcuts =
      settings.value(QString::fromLatin1(kFolderShortcutsKey), QStringList())
          .toStringList();
  while (shortcuts.size() < kFavoriteSlotCount) {
    shortcuts.push_back(QString());
  }
  if (shortcuts.size() > kFavoriteSlotCount) {
    shortcuts = shortcuts.mid(0, kFavoriteSlotCount);
  }
  for (QString& shortcut : shortcuts) {
    shortcut = shortcut.trimmed();
  }
  return shortcuts;
}

bool key_event_has_original_right_control_modifier(const QKeyEvent& event) {
#if defined(Q_OS_MACOS)
  if ((event.nativeModifiers() & kMacDeviceRightControlKeyMask) != 0) {
    return true;
  }
#endif
#if defined(Q_OS_WIN)
  Q_UNUSED(event)
  return (::GetKeyState(VK_RCONTROL) & 0x8000) != 0;
#else
  Q_UNUSED(event)
  return false;
#endif
}

bool generic_ctrl_digit_has_no_existing_shortcut(int key) {
  return key == Qt::Key_0 || (key >= Qt::Key_5 && key <= Qt::Key_9);
}

}  // namespace

void MainWindow::rebuild_favorites_menu() {
  if (favorites_menu_ == nullptr || add_to_favorites_menu_ == nullptr) {
    return;
  }

  favorites_menu_->clear();
  add_to_favorites_menu_->clear();
  for (int slot = 0; slot < kFavoriteSlotCount; ++slot) {
    QAction* action = add_to_favorites_menu_->addAction(favorite_set_slot_label(slot));
    action->setData(slot);
    connect(action, &QAction::triggered, this, &MainWindow::on_add_to_favorites_requested);
  }

  favorites_menu_->addMenu(add_to_favorites_menu_);
  favorites_menu_->addSeparator();

  z7::platform::qt::PortableSettings settings;
  const QStringList shortcuts = load_folder_shortcuts(settings);
  for (int slot = 0; slot < kFavoriteSlotCount; ++slot) {
    const QString path = shortcuts.value(slot);
    QAction* action = favorites_menu_->addAction(favorite_display_label(slot, path));
    action->setData(path);
    action->setEnabled(!path.isEmpty());
    if (!path.isEmpty()) {
      connect(action, &QAction::triggered, this, &MainWindow::on_open_favorite_requested);
    }
  }
}

bool MainWindow::handle_favorite_slot_shortcut(
    const QKeyEvent& event,
    Qt::KeyboardModifiers modifiers) {
  const std::optional<int> slot = favorite_slot_from_digit_key(event.key());
  if (!slot.has_value()) {
    return false;
  }

  const bool set_slot = (modifiers & Qt::ShiftModifier) != 0;
  modifiers &= ~Qt::ShiftModifier;

  const bool alt_shortcut = modifiers == Qt::AltModifier;
  const bool ctrl_shortcut =
      modifiers == Qt::ControlModifier &&
      (key_event_has_original_right_control_modifier(event) ||
       generic_ctrl_digit_has_no_existing_shortcut(event.key()) ||
       set_slot);
  if (!alt_shortcut && !ctrl_shortcut) {
    return false;
  }

  if (set_slot) {
    set_favorite_slot(*slot);
  } else {
    open_favorite_slot(*slot);
  }
  return true;
}

void MainWindow::set_favorite_slot(int slot) {
  if (!favorite_slot_is_valid(slot)) {
    return;
  }

  QString folder_prefix = active_panel_controller().favorite_folder_prefix().trimmed();
  if (!folder_prefix.isEmpty() && !in_archive_view()) {
    folder_prefix = QDir(folder_prefix).absolutePath();
  }

  if (folder_prefix.isEmpty()) {
    QMessageBox::warning(this,
                         z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(800)),
                         z7::ui::runtime_support::J(
                             QStringLiteral("ui.state.favorites.no_filesystem_folder")));
    return;
  }

  z7::platform::qt::PortableSettings settings;
  QStringList shortcuts = load_folder_shortcuts(settings);
  shortcuts[slot] = folder_prefix;
  settings.setValue(QString::fromLatin1(kFolderShortcutsKey), shortcuts);
  rebuild_favorites_menu();
}

void MainWindow::open_favorite_slot(int slot) {
  if (!favorite_slot_is_valid(slot)) {
    return;
  }

  z7::platform::qt::PortableSettings settings;
  const QStringList shortcuts = load_folder_shortcuts(settings);
  open_favorite_path(shortcuts.value(slot));
}

void MainWindow::open_favorite_path(const QString& raw_path) {
  const QString path = raw_path.trimmed();
  if (path.isEmpty()) {
    return;
  }

  const int panel_index = active_panel_index_;
  if (!open_folder_prefix_for_panel(panel_index, path)) {
    QMessageBox::warning(this,
                         z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(503)),
                         z7::ui::runtime_support::JF(
                             QStringLiteral("ui.state.favorites.folder_missing"),
                             {QDir::toNativeSeparators(path)}));
  }
}

void MainWindow::on_add_to_favorites_requested() {
  const auto* action = qobject_cast<const QAction*>(sender());
  if (action == nullptr) {
    return;
  }

  bool slot_ok = false;
  const int slot = action->data().toInt(&slot_ok);
  if (!slot_ok) {
    return;
  }
  set_favorite_slot(slot);
}

void MainWindow::on_open_favorite_requested() {
  const auto* action = qobject_cast<const QAction*>(sender());
  if (action == nullptr) {
    return;
  }

  open_favorite_path(action->data().toString());
}

}  // namespace z7::ui::filemanager
