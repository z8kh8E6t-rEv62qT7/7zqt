#include "macos_integration_c_api_menu_helpers.h"

#include <QDir>

#include <utility>

namespace z7::macos_integration::capi_internal {
namespace {

QString hash_method_for_action(const QString& action_id) {
  using namespace z7::shell_integration;
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

QString generated_sha256_file_name(const QStringList& selected_paths) {
  return z7::shell_integration::shell_integration_create_archive_name_from_paths(
             selected_paths,
             true,
             nullptr) +
         QStringLiteral(".sha256");
}

}  // namespace

QString open_as_type_for_action(const QString& action_id) {
  using namespace z7::shell_integration;
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

QString resolve_working_dir(
    const z7::shell_integration::ShellIntegrationMenuPlan& plan,
    const z7::shell_integration::ShellIntegrationSelection& selection) {
  if (!plan.base_folder.trimmed().isEmpty()) {
    return plan.base_folder.trimmed();
  }
  if (!selection.working_directory.trimmed().isEmpty()) {
    return selection.working_directory.trimmed();
  }
  return QDir::homePath();
}

bool build_task_ipc_payload_for_action(
    const QString& action_id,
    const z7::shell_integration::ShellIntegrationMenuPlan& plan,
    z7::task_ipc_runtime::TaskIpcPayload* out_payload,
    QString* error_message) {
  if (error_message != nullptr) {
    error_message->clear();
  }
  if (out_payload == nullptr) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("Task IPC payload output is null.");
    }
    return false;
  }

  using namespace z7::shell_integration;
  z7::task_ipc_runtime::TaskIpcPayload payload;
  payload.refresh_after_finish = false;
  payload.complete_on_claim = true;

  if (action_id == QString::fromLatin1(kActionExtractFiles)) {
    if (plan.selected_files.isEmpty()) {
      if (error_message != nullptr) {
        *error_message = QStringLiteral("Extract requires archive-like files.");
      }
      return false;
    }
    payload.command = z7::task_ipc_runtime::TaskIpcCommandKind::kExtract;
    payload.show_dialog = true;
    payload.extract = z7::task_ipc_runtime::TaskIpcExtractPayload{};
    payload.extract->output_dir = plan.base_folder;
    payload.extract->split_dest_enabled = true;
    payload.extract->split_dest_name = plan.extract_subdir;
    payload.extract->archive_inputs = plan.selected_files;
    *out_payload = std::move(payload);
    return true;
  }

  if (action_id == QString::fromLatin1(kActionExtractHere)) {
    if (plan.selected_files.isEmpty()) {
      if (error_message != nullptr) {
        *error_message = QStringLiteral("Extract requires archive-like files.");
      }
      return false;
    }
    payload.command = z7::task_ipc_runtime::TaskIpcCommandKind::kExtract;
    payload.extract = z7::task_ipc_runtime::TaskIpcExtractPayload{};
    payload.extract->output_dir = plan.base_folder;
    payload.extract->split_dest_enabled = false;
    payload.extract->archive_inputs = plan.selected_files;
    *out_payload = std::move(payload);
    return true;
  }

  if (action_id == QString::fromLatin1(kActionExtractTo)) {
    if (plan.selected_files.isEmpty()) {
      if (error_message != nullptr) {
        *error_message = QStringLiteral("Extract requires archive-like files.");
      }
      return false;
    }
    payload.command = z7::task_ipc_runtime::TaskIpcCommandKind::kExtract;
    payload.extract = z7::task_ipc_runtime::TaskIpcExtractPayload{};
    payload.extract->output_dir = plan.base_folder;
    payload.extract->split_dest_enabled = true;
    payload.extract->split_dest_name = plan.extract_subdir;
    payload.extract->eliminate_root_duplication = true;
    payload.extract->archive_inputs = plan.selected_files;
    *out_payload = std::move(payload);
    return true;
  }

  if (action_id == QString::fromLatin1(kActionTestArchive)) {
    if (plan.selected_files.isEmpty()) {
      if (error_message != nullptr) {
        *error_message = QStringLiteral("Test requires archive-like files.");
      }
      return false;
    }
    payload.command = z7::task_ipc_runtime::TaskIpcCommandKind::kTest;
    payload.test = z7::task_ipc_runtime::TaskIpcTestPayload{};
    payload.test->archive_inputs = plan.selected_files;
    *out_payload = std::move(payload);
    return true;
  }

  if (action_id == QString::fromLatin1(kActionAddToArchive)) {
    if (plan.selected_paths.isEmpty()) {
      if (error_message != nullptr) {
        *error_message = QStringLiteral("Add requires selected paths.");
      }
      return false;
    }
    payload.command = z7::task_ipc_runtime::TaskIpcCommandKind::kAdd;
    payload.show_dialog = true;
    payload.add = z7::task_ipc_runtime::TaskIpcAddPayload{};
    payload.add->archive_path = QDir(plan.base_folder).filePath(plan.archive_name);
    payload.add->input_paths = plan.selected_paths;
    *out_payload = std::move(payload);
    return true;
  }

  if (action_id == QString::fromLatin1(kActionAddTo7z) ||
      action_id == QString::fromLatin1(kActionAddToZip)) {
    if (plan.selected_paths.isEmpty()) {
      if (error_message != nullptr) {
        *error_message = QStringLiteral("Add requires selected paths.");
      }
      return false;
    }
    const QString archive_type =
        action_id == QString::fromLatin1(kActionAddTo7z) ? QStringLiteral("7z")
                                                          : QStringLiteral("zip");
    payload.command = z7::task_ipc_runtime::TaskIpcCommandKind::kAdd;
    payload.add = z7::task_ipc_runtime::TaskIpcAddPayload{};
    payload.add->archive_type = archive_type;
    payload.add->archive_path = QDir(plan.base_folder).filePath(
        plan.archive_name + QStringLiteral(".") + archive_type);
    payload.add->input_paths = plan.selected_paths;
    *out_payload = std::move(payload);
    return true;
  }

  if (action_id == QString::fromLatin1(kActionGenerateSha256)) {
    if (plan.selected_paths.isEmpty()) {
      if (error_message != nullptr) {
        *error_message = QStringLiteral("SHA-256 generation requires selected paths.");
      }
      return false;
    }
    payload.command = z7::task_ipc_runtime::TaskIpcCommandKind::kAdd;
    payload.add = z7::task_ipc_runtime::TaskIpcAddPayload{};
    payload.add->archive_type = QStringLiteral("hash");
    payload.add->archive_path = QDir(plan.base_folder).filePath(
        generated_sha256_file_name(plan.selected_paths));
    payload.add->input_paths = plan.selected_paths;
    *out_payload = std::move(payload);
    return true;
  }

  if (action_id == QString::fromLatin1(kActionChecksumTest)) {
    if (plan.selected_paths.isEmpty()) {
      if (error_message != nullptr) {
        *error_message = QStringLiteral("Checksum test requires selected paths.");
      }
      return false;
    }
    payload.command = z7::task_ipc_runtime::TaskIpcCommandKind::kCli;
    payload.cli = z7::task_ipc_runtime::TaskIpcCliPayload{};
    payload.cli->argv = {QStringLiteral("t"), QStringLiteral("-thash")};
    payload.cli->argv.append(plan.selected_paths);
    payload.cli->working_dir = plan.base_folder;
    *out_payload = std::move(payload);
    return true;
  }

  const QString hash_method = hash_method_for_action(action_id);
  if (!hash_method.isEmpty()) {
    if (plan.selected_paths.isEmpty()) {
      if (error_message != nullptr) {
        *error_message = QStringLiteral("Hash requires selected paths.");
      }
      return false;
    }
    payload.command = z7::task_ipc_runtime::TaskIpcCommandKind::kHash;
    payload.hash = z7::task_ipc_runtime::TaskIpcHashPayload{};
    payload.hash->hash_method = hash_method;
    payload.hash->input_paths = plan.selected_paths;
    *out_payload = std::move(payload);
    return true;
  }

  if (error_message != nullptr) {
    *error_message = QStringLiteral("Action is not supported in task IPC mode: %1")
                         .arg(action_id);
  }
  return false;
}

}  // namespace z7::macos_integration::capi_internal
