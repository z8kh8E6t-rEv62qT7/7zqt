// src/ui/filemanager/src/main_window/drag_drop/drop_logic_decision.cpp
// Role: Drop command decision rules and right-click menu routing.

#include "main_window/deps.h"
#include "main_window/internal.h"
#include "drop_logic.h"

namespace z7::ui::filemanager {

namespace {

#ifdef Z7_TESTING
bool parse_drop_command_override(const QVariant& value, DropCommand* command) {
  if (command == nullptr || !value.isValid()) {
    return false;
  }

  const QString key = value.toString().trimmed().toLower();
  if (key == QStringLiteral("copy")) {
    *command = DropCommand::kCopy;
    return true;
  }
  if (key == QStringLiteral("move")) {
    *command = DropCommand::kMove;
    return true;
  }
  if (key == QStringLiteral("copy_to_archive")) {
    *command = DropCommand::kCopyToArchive;
    return true;
  }
  if (key == QStringLiteral("add_to_archive")) {
    *command = DropCommand::kAddToArchive;
    return true;
  }
  if (key == QStringLiteral("cancel")) {
    *command = DropCommand::kCancel;
    return true;
  }
  return false;
}
#endif

DropCommand choose_internal_fs_left_drop_command(const QDropEvent* event,
                                                 bool source_target_same_volume) {
  const Qt::KeyboardModifiers modifiers =
      event != nullptr ? event->modifiers() : Qt::NoModifier;
  const bool has_ctrl = modifiers.testFlag(Qt::ControlModifier);
  const bool has_shift = modifiers.testFlag(Qt::ShiftModifier);
  const bool has_alt = modifiers.testFlag(Qt::AltModifier);

  if (has_ctrl) {
    if (has_alt || has_shift) {
      return source_target_same_volume ? DropCommand::kMove : DropCommand::kCopy;
    }
    return DropCommand::kCopy;
  }
  if (has_shift) {
    if (has_alt) {
      return source_target_same_volume ? DropCommand::kMove : DropCommand::kCopy;
    }
    return DropCommand::kMove;
  }
  if (has_alt) {
    return source_target_same_volume ? DropCommand::kMove : DropCommand::kCopy;
  }

  return source_target_same_volume ? DropCommand::kMove : DropCommand::kCopy;
}

DropCommand constrain_copy_move_command_by_possible_actions(
    DropCommand command,
    Qt::DropActions possible_actions,
    bool source_target_same_volume) {
  const bool allow_copy = possible_actions.testFlag(Qt::CopyAction);
  const bool allow_move = possible_actions.testFlag(Qt::MoveAction);
  if (!allow_copy && !allow_move) {
    return DropCommand::kCancel;
  }

  if (command == DropCommand::kMove) {
    if (allow_move) {
      return DropCommand::kMove;
    }
    if (allow_copy) {
      return DropCommand::kCopy;
    }
    return DropCommand::kCancel;
  }
  if (command == DropCommand::kCopy) {
    if (allow_copy) {
      return DropCommand::kCopy;
    }
    if (allow_move && source_target_same_volume) {
      return DropCommand::kMove;
    }
    return DropCommand::kCancel;
  }
  return command;
}

DropCommand normalize_drop_command(DropCommand command,
                                   bool in_archive_view,
                                   bool allow_copy_move) {
  if (command == DropCommand::kCancel) {
    return command;
  }

  if (in_archive_view) {
    if (command != DropCommand::kCopyToArchive) {
      return DropCommand::kCopyToArchive;
    }
    return command;
  }

  if (command == DropCommand::kCopyToArchive) {
    return DropCommand::kAddToArchive;
  }
  if ((command == DropCommand::kCopy || command == DropCommand::kMove) &&
      !allow_copy_move) {
    return DropCommand::kAddToArchive;
  }
  return command;
}

bool is_right_button_drop(const QDropEvent* event) {
  if (event == nullptr) {
    return false;
  }
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
  return event->buttons().testFlag(Qt::RightButton);
#else
  return event->mouseButtons().testFlag(Qt::RightButton);
#endif
}

QPoint drop_event_pos_in_target(const QObject* watched, const QDropEvent* event) {
  if (event == nullptr) {
    return QPoint();
  }
  const QWidget* target = qobject_cast<const QWidget*>(watched);
  if (target == nullptr) {
    return event->position().toPoint();
  }
  return event->position().toPoint();
}

QPoint drop_event_global_pos(const QObject* watched, const QDropEvent* event) {
  const QWidget* target = qobject_cast<const QWidget*>(watched);
  if (target == nullptr) {
    return QPoint();
  }
  return target->mapToGlobal(drop_event_pos_in_target(watched, event));
}

}  // namespace

DropCommand choose_drop_command(MainWindow* window,
                                bool in_archive_view,
                                bool allow_copy_move,
                                bool app_target,
                                bool internal_fs_source,
                                bool internal_archive_source,
                                bool source_target_same_volume,
                                const QObject* watched,
                                const QDropEvent* event) {
  if (window == nullptr) {
    return DropCommand::kCancel;
  }

  const bool right_button = is_right_button_drop(event);
  const Qt::DropActions possible_actions =
      event != nullptr ? event->possibleActions()
                       : (Qt::CopyAction | Qt::MoveAction);
  const bool allow_copy_effect = possible_actions.testFlag(Qt::CopyAction);
  const bool allow_move_effect = possible_actions.testFlag(Qt::MoveAction);
#ifdef Z7_TESTING
  DropCommand command_override = DropCommand::kCancel;
  if (parse_drop_command_override(
          window->property("z7.fm.drop.command.override"),
          &command_override)) {
    if (command_override == DropCommand::kCopy ||
        command_override == DropCommand::kMove) {
      command_override = constrain_copy_move_command_by_possible_actions(
          command_override,
          possible_actions,
          source_target_same_volume);
    }
    return normalize_drop_command(command_override, in_archive_view, allow_copy_move);
  }
#endif
  if (!right_button) {
    if (app_target) {
      return DropCommand::kAddToArchive;
    }
    if (!in_archive_view && internal_archive_source && allow_copy_move) {
      if (!allow_copy_effect) {
        return DropCommand::kCancel;
      }
      return DropCommand::kCopy;
    }
    if (!in_archive_view && internal_fs_source && allow_copy_move) {
      return normalize_drop_command(
          constrain_copy_move_command_by_possible_actions(
              choose_internal_fs_left_drop_command(event, source_target_same_volume),
              possible_actions,
              source_target_same_volume),
          in_archive_view,
          allow_copy_move);
    }
    const DropCommand default_command =
        in_archive_view ? DropCommand::kCopyToArchive : DropCommand::kAddToArchive;
    return normalize_drop_command(default_command, in_archive_view, allow_copy_move);
  }

  QMenu menu(window);
  QAction* copy_action = nullptr;
  QAction* move_action = nullptr;
  QAction* add_action = nullptr;
  QAction* copy_to_archive_action = nullptr;

  if (in_archive_view) {
    if (allow_copy_effect) {
      QString label = z7::ui::runtime_support::strip_mnemonic(lang_or(6002));
      const QString archive_suffix = z7::ui::runtime_support::strip_mnemonic(lang_or(2321));
      if (!archive_suffix.trimmed().isEmpty()) {
        label = QStringLiteral("%1 %2").arg(label, archive_suffix);
      }
      copy_to_archive_action = menu.addAction(label);
    }
  } else {
    if (!app_target && allow_copy_move) {
      if (allow_copy_effect) {
        copy_action = menu.addAction(z7::ui::runtime_support::strip_mnemonic(lang_or(6000)));
      }
      if (allow_move_effect && !internal_archive_source) {
        move_action = menu.addAction(z7::ui::runtime_support::strip_mnemonic(lang_or(6001)));
      }
    }
    if (allow_copy_effect) {
      add_action = menu.addAction(z7::ui::runtime_support::strip_mnemonic(lang_or(2324)));
    }
  }

  menu.addSeparator();
  QString cancel_text = z7::ui::runtime_support::strip_mnemonic(lang_or(402));
  if (cancel_text.trimmed().isEmpty()) {
    cancel_text = QStringLiteral("Cancel");
  }
  QAction* cancel_action = menu.addAction(cancel_text);

#ifdef Z7_TESTING
  DropCommand menu_selection_override = DropCommand::kCancel;
  if (parse_drop_command_override(
          window->property("z7.fm.drop.menu.selection.override"),
          &menu_selection_override)) {
    if (menu_selection_override == DropCommand::kCopy && copy_action != nullptr) {
      return normalize_drop_command(
          constrain_copy_move_command_by_possible_actions(DropCommand::kCopy,
                                                          possible_actions,
                                                          source_target_same_volume),
          in_archive_view,
          allow_copy_move);
    }
    if (menu_selection_override == DropCommand::kMove && move_action != nullptr) {
      return normalize_drop_command(
          constrain_copy_move_command_by_possible_actions(DropCommand::kMove,
                                                          possible_actions,
                                                          source_target_same_volume),
          in_archive_view,
          allow_copy_move);
    }
    if (menu_selection_override == DropCommand::kAddToArchive &&
        add_action != nullptr) {
      return DropCommand::kAddToArchive;
    }
    if (menu_selection_override == DropCommand::kCopyToArchive &&
        copy_to_archive_action != nullptr) {
      return DropCommand::kCopyToArchive;
    }
    return DropCommand::kCancel;
  }
#endif

  const QPoint global_pos = drop_event_global_pos(watched, event);
  QAction* chosen = menu.exec(global_pos);
  if (chosen == copy_action) {
    return normalize_drop_command(
        constrain_copy_move_command_by_possible_actions(DropCommand::kCopy,
                                                        possible_actions,
                                                        source_target_same_volume),
        in_archive_view,
        allow_copy_move);
  }
  if (chosen == move_action) {
    return normalize_drop_command(
        constrain_copy_move_command_by_possible_actions(DropCommand::kMove,
                                                        possible_actions,
                                                        source_target_same_volume),
        in_archive_view,
        allow_copy_move);
  }
  if (chosen == add_action) {
    return DropCommand::kAddToArchive;
  }
  if (chosen == copy_to_archive_action) {
    return DropCommand::kCopyToArchive;
  }
  if (chosen == cancel_action) {
    return DropCommand::kCancel;
  }
  return normalize_drop_command(DropCommand::kCancel, in_archive_view, allow_copy_move);
}

DropCommand choose_drop_preview_command(MainWindow* window,
                                        bool in_archive_view,
                                        bool allow_copy_move,
                                        bool app_target,
                                        bool internal_fs_source,
                                        bool internal_archive_source,
                                        bool source_target_same_volume,
                                        const QDropEvent* event) {
  if (window == nullptr) {
    return DropCommand::kCancel;
  }

  const bool right_button = is_right_button_drop(event);
  const Qt::DropActions possible_actions =
      event != nullptr ? event->possibleActions()
                       : (Qt::CopyAction | Qt::MoveAction);
  const bool allow_copy_effect = possible_actions.testFlag(Qt::CopyAction);
  if (app_target) {
    if (!right_button) {
      return DropCommand::kAddToArchive;
    }
    if (!allow_copy_effect) {
      return DropCommand::kCancel;
    }
    return in_archive_view ? DropCommand::kCopyToArchive
                           : DropCommand::kAddToArchive;
  }
  if (in_archive_view) {
    if (!allow_copy_effect) {
      return DropCommand::kCancel;
    }
    return DropCommand::kCopyToArchive;
  }

  if (!right_button && internal_fs_source && allow_copy_move) {
    return normalize_drop_command(
        constrain_copy_move_command_by_possible_actions(
            choose_internal_fs_left_drop_command(event, source_target_same_volume),
            possible_actions,
            source_target_same_volume),
        in_archive_view,
        allow_copy_move);
  }
  if (!right_button && internal_archive_source && allow_copy_move) {
    if (!allow_copy_effect) {
      return DropCommand::kCancel;
    }
    return DropCommand::kCopy;
  }
  if (right_button && allow_copy_move) {
    if (internal_archive_source) {
      if (!allow_copy_effect) {
        return DropCommand::kCancel;
      }
      return DropCommand::kCopy;
    }
    const DropCommand default_right_command =
        source_target_same_volume ? DropCommand::kMove : DropCommand::kCopy;
    return normalize_drop_command(
        constrain_copy_move_command_by_possible_actions(default_right_command,
                                                        possible_actions,
                                                        source_target_same_volume),
        in_archive_view,
        allow_copy_move);
  }
  if (!allow_copy_effect) {
    return DropCommand::kCancel;
  }
  return normalize_drop_command(
      DropCommand::kAddToArchive, in_archive_view, allow_copy_move);
}

}  // namespace z7::ui::filemanager
