// src/ui/gui/src/gui_app_controller/helpers.cpp
// Role: Command parsing and dialog preparation helpers for GuiAppController.

#include "helpers.h"

#include <cstdlib>
#include <limits>
#include <string>

#include <QDialog>
#include <QFileInfo>
#ifdef Z7_TESTING
#include <QString>
#endif

#include "common/archive_type_normalization.h"
#include "official_lang_catalog.h"
#include "platform_support.h"
#include "archive_string_codec_qt.h"
#include "compress_dialog.h"
#include "extract_dialog.h"

namespace z7::ui::gui::gui_app_controller_helpers {
namespace {

using z7::ui::runtime_support::L;
using z7::ui::runtime_support::strip_mnemonic;

bool is_unsigned_decimal(std::string_view value) {
  if (value.empty()) {
    return false;
  }
  for (char c : value) {
    if (c < '0' || c > '9') {
      return false;
    }
  }
  return true;
}

#ifdef Z7_TESTING
constexpr const char* kSuppressResultDialogsEnv =
    "Z7_SUPPRESS_GUI_RESULT_DIALOGS_FOR_TESTS";
#endif

struct AddDialogInputContext {
  bool single_file_input = false;
  std::string single_file_name;
};

AddDialogInputContext input_context_for_add_dialog(const AddTaskSpec& spec) {
  AddDialogInputContext context;
  if (spec.input_paths.size() != 1) {
    return context;
  }

  const QString input_path = z7::ui::archive_support::from_native_string(
      spec.input_paths.front());
  const QFileInfo info(input_path);
  if (!info.exists() || info.isDir()) {
    return context;
  }

  context.single_file_input = true;
  context.single_file_name =
      z7::ui::archive_support::to_native_string(info.fileName());
  return context;
}

bool add_dialog_format_allows_encrypted_headers(
    const std::string& archive_type) {
  return z7::common::canonical_archive_type_token_copy(archive_type) == "7z";
}

CompressCommandOptions compress_options_from_add_task_spec(
    const AddTaskSpec& spec) {
  CompressCommandOptions options;
  const AddDialogInputContext input_context =
      input_context_for_add_dialog(spec);
  options.archive_path = spec.archive_path;
  options.archive_type = spec.archive_type;
  options.keep_archive_name_extension = !input_context.single_file_input;
  options.single_file_input = input_context.single_file_input;
  options.single_file_name = input_context.single_file_name;
  options.update_mode = spec.update_mode;
  options.path_mode = spec.path_mode;
  options.create_sfx = spec.create_sfx;
  options.share_for_write = spec.share_for_write;
  options.delete_after_compressing = spec.delete_after_compressing;
  options.compression_level = spec.compression_level;
  options.method = spec.method_value;
  options.dictionary_size = spec.dictionary_size;
  options.word_size = spec.word_size;
  options.solid_block_size = spec.solid_block_size;
  options.thread_count = spec.thread_count;
  options.volume_size = spec.volume_size;
  options.password = spec.password;
  if (!spec.password.empty()) {
    options.encryption_method = spec.encryption_method;
    options.encrypt_headers =
        spec.encrypt_headers_defined && spec.encrypt_headers;
  }
  options.extra_parameters = spec.extra_parameters;
  options.opaque_add_task.raw_update_switch = spec.raw_update_switch;
  options.opaque_add_task.raw_update_switches = spec.raw_update_switches;
  return options;
}

void apply_compress_options_to_add_task_spec(
    const CompressCommandOptions& options,
    AddTaskSpec* spec) {
  if (spec == nullptr) {
    return;
  }
  spec->archive_path = options.archive_path;
  spec->archive_type = options.archive_type;
  spec->update_mode = options.update_mode;
  spec->path_mode = options.path_mode;
  spec->create_sfx = options.create_sfx;
  spec->share_for_write = options.share_for_write;
  spec->delete_after_compressing = options.delete_after_compressing;
  spec->compression_level = options.compression_level;
  spec->method_value = options.method;
  spec->dictionary_size = options.dictionary_size;
  spec->word_size = options.word_size;
  spec->solid_block_size = options.solid_block_size;
  spec->thread_count = options.thread_count;
  spec->volume_size = options.volume_size;
  spec->password = options.password;
  const bool has_password = !options.password.empty();
  const bool encrypt_headers_allowed =
      has_password &&
      add_dialog_format_allows_encrypted_headers(options.archive_type);
  spec->encrypt_headers_defined = encrypt_headers_allowed;
  spec->encrypt_headers = encrypt_headers_allowed && options.encrypt_headers;
  spec->encryption_method =
      has_password ? options.encryption_method : std::string();
  spec->extra_parameters = options.extra_parameters;
  spec->raw_update_switch = options.opaque_add_task.raw_update_switch;
  spec->raw_update_switches = options.opaque_add_task.raw_update_switches;
}

}  // namespace

#ifdef Z7_TESTING
bool suppress_result_dialogs_for_tests() {
  return qEnvironmentVariable(kSuppressResultDialogsEnv).trimmed() ==
         QStringLiteral("1");
}
#endif

uint32_t benchmark_iterations_or_default(const GuiTaskSpec& spec) {
  return std::visit(
      [](const auto& typed_spec) -> uint32_t {
        using T = std::decay_t<decltype(typed_spec)>;
        if constexpr (std::is_same_v<T, BenchmarkTaskSpec>) {
          for (const std::string& operand : typed_spec.operands) {
            if (!is_unsigned_decimal(operand)) {
              continue;
            }
            const unsigned long long parsed =
                std::strtoull(operand.c_str(), nullptr, 10);
            if (parsed == 0 ||
                parsed > std::numeric_limits<uint32_t>::max()) {
              continue;
            }
            return static_cast<uint32_t>(parsed);
          }
        }
        return 10U;
      },
      spec);
}

QString task_title(const GuiTaskSpec& spec) {
  return std::visit(
      [](const auto& typed_spec) {
        using T = std::decay_t<decltype(typed_spec)>;
        if constexpr (std::is_same_v<T, AddTaskSpec>) {
          return QStringLiteral("7zG - %1")
              .arg(strip_mnemonic(L(7200)));
        } else if constexpr (std::is_same_v<T, ExtractTaskSpec>) {
          return QStringLiteral("7zG - %1")
              .arg(strip_mnemonic(L(7201)));
        } else if constexpr (std::is_same_v<T, ArchiveExportTaskSpec>) {
          return QStringLiteral("7zG - %1")
              .arg(strip_mnemonic(L(6000)));
        } else if constexpr (std::is_same_v<T, TestTaskSpec>) {
          return QStringLiteral("7zG - %1")
              .arg(strip_mnemonic(L(7202)));
        } else if constexpr (std::is_same_v<T, HashTaskSpec>) {
          return QStringLiteral("7zG - %1")
              .arg(strip_mnemonic(L(7500)));
        } else if constexpr (std::is_same_v<T, ArchiveHashTaskSpec>) {
          return QStringLiteral("7zG - %1")
              .arg(strip_mnemonic(L(7500)));
        } else if constexpr (std::is_same_v<T, ArchiveTestTaskSpec>) {
          return QStringLiteral("7zG - %1")
              .arg(strip_mnemonic(L(7202)));
        } else if constexpr (std::is_same_v<T, BenchmarkTaskSpec>) {
          return QStringLiteral("7zG - %1")
              .arg(strip_mnemonic(L(7600)));
        } else if constexpr (std::is_same_v<T, OpenTaskSpec>) {
          return QStringLiteral("7zG - %1")
              .arg(QStringLiteral("Open"));
        } else {
          return QStringLiteral("7zG - %1")
              .arg(QStringLiteral("Quick Look Export"));
        }
      },
      spec);
}

TaskSpecPreparationResult prepare_task_spec_with_optional_dialog(
    const GuiTaskSpec& requested_spec) {
  TaskSpecPreparationResult result;
  result.status = TaskSpecPreparationStatus::kPrepared;
  result.spec = requested_spec;
  const TaskSpecPreparationStatus status = std::visit(
      [&](auto& typed_spec) -> TaskSpecPreparationStatus {
        using T = std::decay_t<decltype(typed_spec)>;
        if constexpr (std::is_same_v<T, AddTaskSpec>) {
          typed_spec.show_dialog = false;
          if (!std::get<AddTaskSpec>(requested_spec).show_dialog) {
            return TaskSpecPreparationStatus::kPrepared;
          }
          const CompressCommandOptions options =
              compress_options_from_add_task_spec(typed_spec);
          CompressDialog dialog(options);
          const int dialog_result = dialog.exec();
          if (dialog_result == QDialog::Rejected) {
            return TaskSpecPreparationStatus::kCanceled;
          }
          if (dialog_result != QDialog::Accepted) {
            return TaskSpecPreparationStatus::kFailed;
          }
          const CompressCommandOptions accepted = dialog.options();
          apply_compress_options_to_add_task_spec(accepted, &typed_spec);
          return TaskSpecPreparationStatus::kPrepared;
        } else if constexpr (std::is_same_v<T, ExtractTaskSpec>) {
          typed_spec.show_dialog = false;
          if (!std::get<ExtractTaskSpec>(requested_spec).show_dialog) {
            return TaskSpecPreparationStatus::kPrepared;
          }
          ExtractCommandOptions options;
          options.output_dir = typed_spec.output_dir;
          options.split_dest_enabled = typed_spec.split_dest_enabled;
          options.split_dest_name = typed_spec.split_dest_name;
          options.overwrite_switch = typed_spec.overwrite_switch;
          options.path_mode = typed_spec.path_mode;
          options.eliminate_root_duplication = typed_spec.eliminate_root_duplication;
          options.password = typed_spec.password;
          options.restore_file_security =
              typed_spec.restore_file_security &&
              z7::ui::runtime_support::is_platform_supported(
                  z7::ui::runtime_support::PlatformSupport::kWindowsOnly);
          if (!typed_spec.archive_inputs.empty()) {
            options.archive_name = typed_spec.archive_inputs.front();
          }

          ExtractDialog dialog(options);
          const int dialog_result = dialog.exec();
          if (dialog_result == QDialog::Rejected) {
            return TaskSpecPreparationStatus::kCanceled;
          }
          if (dialog_result != QDialog::Accepted) {
            return TaskSpecPreparationStatus::kFailed;
          }
          const ExtractCommandOptions accepted = dialog.options();
          typed_spec.output_dir = accepted.output_dir;
          typed_spec.split_dest_enabled = accepted.split_dest_enabled;
          typed_spec.split_dest_name = accepted.split_dest_name;
          typed_spec.overwrite_switch = accepted.overwrite_switch;
          typed_spec.path_mode = accepted.path_mode;
          typed_spec.eliminate_root_duplication =
              accepted.eliminate_root_duplication;
          typed_spec.password = accepted.password;
          typed_spec.restore_file_security = accepted.restore_file_security;
          return TaskSpecPreparationStatus::kPrepared;
        } else {
          return TaskSpecPreparationStatus::kPrepared;
        }
      },
      result.spec);
  result.status = status;
  return result;
}

}  // namespace z7::ui::gui::gui_app_controller_helpers
