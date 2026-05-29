#include "internal.h"

#include <QCoreApplication>
#include <QDir>

#include "json_localization.h"

namespace z7::shell_integration::menu_internal {
namespace {

QString shell_key(const QString& suffix) {
  return QStringLiteral("shell.actions.%1").arg(suffix);
}

QString i18n_action_key(const QString& action_id) {
  if (action_id == QString::fromLatin1(kActionOpenAsMenu)) {
    return QStringLiteral("open_as_menu");
  }
  return action_id;
}

}  // namespace

QString locale_key_from_hint(QString locale_hint) {
  locale_hint = locale_hint.trimmed().toLower();
  if (locale_hint.startsWith(QStringLiteral("zh"))) {
    return QStringLiteral("zh-CN");
  }
  return QStringLiteral("en");
}

QString open_as_type_for_action(const QString& action_id) {
  if (action_id == QString::fromLatin1(kActionOpenAsStar)) {
    return QStringLiteral("*");
  }
  if (action_id == QString::fromLatin1(kActionOpenAsHash)) {
    return QStringLiteral("#");
  }
  if (action_id == QString::fromLatin1(kActionOpenAsHashE)) {
    return QStringLiteral("#:e");
  }
  if (action_id == QString::fromLatin1(kActionOpenAs7z)) {
    return QStringLiteral("7z");
  }
  if (action_id == QString::fromLatin1(kActionOpenAsZip)) {
    return QStringLiteral("zip");
  }
  if (action_id == QString::fromLatin1(kActionOpenAsCab)) {
    return QStringLiteral("cab");
  }
  if (action_id == QString::fromLatin1(kActionOpenAsRar)) {
    return QStringLiteral("rar");
  }
  return {};
}

QString crc_method_for_action(const QString& action_id) {
  if (action_id == QString::fromLatin1(kActionCrc32)) {
    return QStringLiteral("CRC32");
  }
  if (action_id == QString::fromLatin1(kActionCrc64)) {
    return QStringLiteral("CRC64");
  }
  if (action_id == QString::fromLatin1(kActionXxh64)) {
    return QStringLiteral("XXH64");
  }
  if (action_id == QString::fromLatin1(kActionMd5)) {
    return QStringLiteral("MD5");
  }
  if (action_id == QString::fromLatin1(kActionSha1)) {
    return QStringLiteral("SHA1");
  }
  if (action_id == QString::fromLatin1(kActionSha256)) {
    return QStringLiteral("SHA256");
  }
  if (action_id == QString::fromLatin1(kActionSha384)) {
    return QStringLiteral("SHA384");
  }
  if (action_id == QString::fromLatin1(kActionSha512)) {
    return QStringLiteral("SHA512");
  }
  if (action_id == QString::fromLatin1(kActionSha3_256)) {
    return QStringLiteral("SHA3-256");
  }
  if (action_id == QString::fromLatin1(kActionBlake2sp)) {
    return QStringLiteral("BLAKE2sp");
  }
  if (action_id == QString::fromLatin1(kActionCrcAll)) {
    return QStringLiteral("*");
  }
  return {};
}

QSet<QString> effective_visible_actions(const ShellIntegrationConfig& config) {
  if (config.visible_actions_configured) {
    return config.visible_actions;
  }
  const QStringList defaults = default_shell_integration_visible_actions();
  return QSet<QString>(defaults.cbegin(), defaults.cend());
}

bool action_visible_in_config(const ShellIntegrationConfig& config,
                              const QString& action_or_group) {
  if (!config.enabled) {
    return false;
  }
  const QSet<QString> visible_actions = effective_visible_actions(config);
  if (visible_actions.isEmpty()) {
    return false;
  }
  if (visible_actions.contains(action_or_group)) {
    return true;
  }

  if (action_or_group.startsWith(QStringLiteral("open_as_")) &&
      visible_actions.contains(QString::fromLatin1(kActionOpenAsMenu))) {
    return true;
  }
  if (!crc_method_for_action(action_or_group).isEmpty() &&
      visible_actions.contains(QString::fromLatin1(kActionCrcShaMenu))) {
    return true;
  }
  if (action_or_group == QString::fromLatin1(kActionChecksumTest) &&
      visible_actions.contains(QString::fromLatin1(kActionCrcShaMenu))) {
    return true;
  }
  if (action_or_group == QString::fromLatin1(kActionGenerateSha256) &&
      visible_actions.contains(QString::fromLatin1(kActionCrcShaMenu))) {
    return true;
  }
  return false;
}

QString menu_title_for_action(const QString& action_id,
                              const ShellIntegrationMenuPlan& plan,
                              const QString& locale_key) {
  if (action_id == QString::fromLatin1(kActionOpen)) {
    return z7::i18n::text(shell_key(i18n_action_key(action_id)), locale_key);
  }
  if (action_id == QString::fromLatin1(kActionOpenAsMenu)) {
    return z7::i18n::text(shell_key(i18n_action_key(action_id)), locale_key);
  }

  if (action_id.startsWith(QStringLiteral("open_as_"))) {
    return open_as_type_for_action(action_id);
  }

  if (action_id == QString::fromLatin1(kActionExtractFiles)) {
    return z7::i18n::text(shell_key(i18n_action_key(action_id)), locale_key);
  }
  if (action_id == QString::fromLatin1(kActionExtractHere)) {
    return z7::i18n::text(shell_key(i18n_action_key(action_id)), locale_key);
  }
  if (action_id == QString::fromLatin1(kActionExtractTo)) {
    return z7::i18n::format(
        shell_key(i18n_action_key(action_id)), {plan.extract_subdir}, locale_key);
  }
  if (action_id == QString::fromLatin1(kActionTestArchive)) {
    return z7::i18n::text(shell_key(i18n_action_key(action_id)), locale_key);
  }

  if (action_id == QString::fromLatin1(kActionAddToArchive)) {
    return z7::i18n::text(shell_key(i18n_action_key(action_id)), locale_key);
  }
  if (action_id == QString::fromLatin1(kActionAddTo7z)) {
    return z7::i18n::format(
        shell_key(i18n_action_key(action_id)), {plan.archive_name_7z}, locale_key);
  }
  if (action_id == QString::fromLatin1(kActionAddToZip)) {
    return z7::i18n::format(
        shell_key(i18n_action_key(action_id)), {plan.archive_name_zip}, locale_key);
  }

  if (action_id == QString::fromLatin1(kActionCrcShaMenu)) {
    return z7::i18n::text(shell_key(i18n_action_key(action_id)), locale_key);
  }

  if (action_id == QString::fromLatin1(kActionGenerateSha256)) {
    const QString dynamic =
        shell_integration_create_archive_name_from_paths(plan.selected_paths,
                                                         true,
                                                         nullptr) +
        QStringLiteral(".sha256");
    return z7::i18n::format(
        shell_key(i18n_action_key(action_id)), {dynamic}, locale_key);
  }

  if (action_id == QString::fromLatin1(kActionChecksumTest)) {
    return z7::i18n::text(shell_key(i18n_action_key(action_id)), locale_key);
  }

  for (const ShellIntegrationHashMethodDef& item :
       shell_integration_hash_method_defs()) {
    if (crc_method_for_action(action_id) == QString::fromLatin1(item.method)) {
      return QString::fromLatin1(item.label);
    }
  }

  return action_id;
}

void append_action_if_visible(ShellIntegrationMenuPlan* plan,
                              const ShellIntegrationConfig& config,
                              const QString& action_id,
                              const QString& locale_key) {
  if (plan == nullptr || !action_visible_in_config(config, action_id)) {
    return;
  }
  plan->actions.push_back(
      {action_id, menu_title_for_action(action_id, *plan, locale_key)});
}

}  // namespace z7::shell_integration::menu_internal
