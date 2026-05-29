#include "internal.h"

#include <QDir>
#include <QFileInfo>

#include <algorithm>

namespace z7::shell_integration {

ShellIntegrationMenuPlan build_shell_integration_menu_plan(
    const ShellIntegrationSelection& selection,
    const ShellIntegrationConfig& config,
    const QString& locale_hint) {
  ShellIntegrationMenuPlan plan;
  plan.selected_paths = selection.selected_paths;

  if (!config.enabled || selection.selected_paths.isEmpty()) {
    return plan;
  }

  plan.menu_visible = true;

  for (const QString& path : selection.selected_paths) {
    const QFileInfo info(path);
    if (info.isFile()) {
      plan.selected_files << info.absoluteFilePath();
    }
  }

  const QFileInfo first_selected(selection.selected_paths.front());
  plan.base_folder = first_selected.absolutePath();
  plan.archive_name = shell_integration_create_archive_name_from_paths(
      selection.selected_paths, false, nullptr);
  plan.archive_name_7z = plan.archive_name + QStringLiteral(".7z");
  plan.archive_name_zip = plan.archive_name + QStringLiteral(".zip");
  if (selection.selected_paths.size() == 1) {
    plan.extract_subdir =
        shell_integration_extract_subfolder_name(first_selected.fileName()) +
        QDir::separator();
  } else {
    plan.extract_subdir = QStringLiteral("*") + QDir::separator();
  }

  bool has_dir = false;
  for (const QString& path : selection.selected_paths) {
    if (QFileInfo(path).isDir()) {
      has_dir = true;
      break;
    }
  }

  bool need_extract = !has_dir && !plan.selected_files.isEmpty();
  if (need_extract && !selection.shift_pressed) {
    for (const QString& file_path : plan.selected_files) {
      if (!shell_integration_do_need_extract_name(
              QFileInfo(file_path).fileName())) {
        need_extract = false;
        break;
      }
    }
  }

  plan.show_extract_group = need_extract;
  plan.show_test = need_extract;
  plan.show_compress_group = true;
  plan.show_crc_group = true;

  if (selection.selected_paths.size() == 1) {
    const QFileInfo info(selection.selected_paths.front());
    if (info.isFile() &&
        shell_integration_do_need_extract_name(info.fileName())) {
      plan.show_open = true;
      plan.show_open_as = true;
    }
  }

  const QString locale_key = menu_internal::locale_key_from_hint(
      locale_hint.isEmpty() ? config.locale_preferred : locale_hint);

  if (plan.show_open) {
    menu_internal::append_action_if_visible(
        &plan, config, QString::fromLatin1(kActionOpen), locale_key);
  }

  if (plan.show_open_as &&
      menu_internal::action_visible_in_config(
          config, QString::fromLatin1(kActionOpenAsMenu))) {
    menu_internal::append_action_if_visible(
        &plan, config, QString::fromLatin1(kActionOpenAsMenu), locale_key);
    menu_internal::append_action_if_visible(
        &plan, config, QString::fromLatin1(kActionOpenAsStar), locale_key);
    menu_internal::append_action_if_visible(
        &plan, config, QString::fromLatin1(kActionOpenAsHash), locale_key);
    menu_internal::append_action_if_visible(
        &plan, config, QString::fromLatin1(kActionOpenAsHashE), locale_key);
    menu_internal::append_action_if_visible(
        &plan, config, QString::fromLatin1(kActionOpenAs7z), locale_key);
    menu_internal::append_action_if_visible(
        &plan, config, QString::fromLatin1(kActionOpenAsZip), locale_key);
    menu_internal::append_action_if_visible(
        &plan, config, QString::fromLatin1(kActionOpenAsCab), locale_key);
    menu_internal::append_action_if_visible(
        &plan, config, QString::fromLatin1(kActionOpenAsRar), locale_key);
  }

  if (plan.show_extract_group) {
    menu_internal::append_action_if_visible(
        &plan, config, QString::fromLatin1(kActionExtractFiles), locale_key);
    menu_internal::append_action_if_visible(
        &plan, config, QString::fromLatin1(kActionExtractHere), locale_key);
    menu_internal::append_action_if_visible(
        &plan, config, QString::fromLatin1(kActionExtractTo), locale_key);
    if (plan.show_test) {
      menu_internal::append_action_if_visible(
          &plan, config, QString::fromLatin1(kActionTestArchive), locale_key);
    }
  }

  if (plan.show_compress_group) {
    menu_internal::append_action_if_visible(
        &plan, config, QString::fromLatin1(kActionAddToArchive), locale_key);
    menu_internal::append_action_if_visible(
        &plan, config, QString::fromLatin1(kActionAddTo7z), locale_key);
    menu_internal::append_action_if_visible(
        &plan, config, QString::fromLatin1(kActionAddToZip), locale_key);
  }

  if (plan.show_crc_group &&
      menu_internal::action_visible_in_config(
          config, QString::fromLatin1(kActionCrcShaMenu))) {
    menu_internal::append_action_if_visible(
        &plan, config, QString::fromLatin1(kActionCrcShaMenu), locale_key);
    menu_internal::append_action_if_visible(
        &plan, config, QString::fromLatin1(kActionCrc32), locale_key);
    menu_internal::append_action_if_visible(
        &plan, config, QString::fromLatin1(kActionCrc64), locale_key);
    menu_internal::append_action_if_visible(
        &plan, config, QString::fromLatin1(kActionXxh64), locale_key);
    menu_internal::append_action_if_visible(
        &plan, config, QString::fromLatin1(kActionMd5), locale_key);
    menu_internal::append_action_if_visible(
        &plan, config, QString::fromLatin1(kActionSha1), locale_key);
    menu_internal::append_action_if_visible(
        &plan, config, QString::fromLatin1(kActionSha256), locale_key);
    menu_internal::append_action_if_visible(
        &plan, config, QString::fromLatin1(kActionSha384), locale_key);
    menu_internal::append_action_if_visible(
        &plan, config, QString::fromLatin1(kActionSha512), locale_key);
    menu_internal::append_action_if_visible(
        &plan, config, QString::fromLatin1(kActionSha3_256), locale_key);
    menu_internal::append_action_if_visible(
        &plan, config, QString::fromLatin1(kActionBlake2sp), locale_key);
    menu_internal::append_action_if_visible(
        &plan, config, QString::fromLatin1(kActionCrcAll), locale_key);
    menu_internal::append_action_if_visible(
        &plan, config, QString::fromLatin1(kActionGenerateSha256), locale_key);
    menu_internal::append_action_if_visible(
        &plan, config, QString::fromLatin1(kActionChecksumTest), locale_key);
  }

  const auto has_action = [&plan](const char* action_id) {
    const QString id = QString::fromLatin1(action_id);
    return std::any_of(plan.actions.cbegin(),
                       plan.actions.cend(),
                       [&id](const ShellIntegrationMenuAction& action) {
                         return action.action_id == id;
                       });
  };
  plan.show_open = plan.show_open && has_action(kActionOpen);
  plan.show_open_as = plan.show_open_as && has_action(kActionOpenAsMenu);
  plan.show_test = plan.show_test && has_action(kActionTestArchive);
  plan.show_extract_group =
      plan.show_extract_group &&
      (has_action(kActionExtractFiles) || has_action(kActionExtractHere) ||
       has_action(kActionExtractTo) || plan.show_test);
  plan.show_compress_group =
      plan.show_compress_group &&
      (has_action(kActionAddToArchive) || has_action(kActionAddTo7z) ||
       has_action(kActionAddToZip));
  plan.show_crc_group = plan.show_crc_group && has_action(kActionCrcShaMenu);

  if (plan.actions.isEmpty()) {
    plan.menu_visible = false;
  }

  return plan;
}

ShellIntegrationValidationResult validate_shell_integration_action(
    const QString& action_id,
    const ShellIntegrationSelection& selection,
    const ShellIntegrationConfig& config,
    const QString& locale_hint) {
  ShellIntegrationValidationResult out;
  const ShellIntegrationMenuPlan plan = build_shell_integration_menu_plan(
      selection, config, locale_hint);
  if (!plan.menu_visible) {
    out.error = QStringLiteral(
        "Shell menu is unavailable for current selection.");
    return out;
  }

  const auto it =
      std::find_if(plan.actions.cbegin(),
                   plan.actions.cend(),
                   [&action_id](const ShellIntegrationMenuAction& entry) {
                     return entry.action_id == action_id;
                   });
  if (it == plan.actions.cend()) {
    out.error = QStringLiteral("Action is not available in current menu plan: %1")
                    .arg(action_id);
    return out;
  }

  out.ok = true;
  return out;
}

}  // namespace z7::shell_integration
