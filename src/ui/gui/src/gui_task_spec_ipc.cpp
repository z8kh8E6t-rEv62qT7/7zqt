#include "gui_task_spec_ipc.h"

#include "archive_string_codec_qt.h"
#include "task_ipc_runtime.h"

namespace z7::ui::gui {

using z7::task_ipc_runtime::TaskIpcCommandKind;

namespace {

QString unsupported_email_payload_message() {
  return QStringLiteral(
      "Email archive sending is not supported in this Qt build.");
}

ExtractPathRemapMatchKind convert_remap_match_kind(
    z7::task_ipc_runtime::TaskIpcExtractPathRemapMatchKind kind) {
  switch (kind) {
    case z7::task_ipc_runtime::TaskIpcExtractPathRemapMatchKind::kExactArchivePath:
      return ExtractPathRemapMatchKind::kExactArchivePath;
    case z7::task_ipc_runtime::TaskIpcExtractPathRemapMatchKind::kArchivePrefix:
      return ExtractPathRemapMatchKind::kArchivePrefix;
    case z7::task_ipc_runtime::TaskIpcExtractPathRemapMatchKind::kRequestRoot:
    default:
      return ExtractPathRemapMatchKind::kRequestRoot;
  }
}

}  // namespace

std::optional<GuiTaskSpec> build_task_spec_from_task_ipc_payload(
    const z7::task_ipc_runtime::TaskIpcPayload& payload,
    QString* error_message) {
  if (error_message != nullptr) {
    error_message->clear();
  }

  switch (payload.command) {
    case TaskIpcCommandKind::kAdd: {
      if (!payload.add.has_value()) {
        if (error_message != nullptr) {
          *error_message = QStringLiteral("Add command payload is missing.");
        }
        return std::nullopt;
      }
      if (payload.add->send_by_email) {
        if (error_message != nullptr) {
          *error_message = unsupported_email_payload_message();
        }
        return std::nullopt;
      }
      if (payload.add->archive_path.trimmed().isEmpty()) {
        if (error_message != nullptr) {
          *error_message = QStringLiteral("Missing archive path for add command.");
        }
        return std::nullopt;
      }
      AddTaskSpec spec;
      spec.show_dialog = payload.show_dialog;
      spec.archive_path =
          z7::ui::archive_support::to_native_string(payload.add->archive_path.trimmed());
      spec.archive_type =
          z7::ui::archive_support::to_native_string(payload.add->archive_type.trimmed());
      spec.update_mode =
          z7::ui::archive_support::to_native_string(payload.add->update_mode.trimmed());
      spec.raw_update_switch =
          z7::ui::archive_support::to_native_string(payload.add->raw_update_switch.trimmed());
      spec.path_mode =
          z7::ui::archive_support::to_native_string(payload.add->path_mode.trimmed());
      spec.create_sfx = payload.add->create_sfx;
      spec.share_for_write = payload.add->share_for_write;
      spec.delete_after_compressing = payload.add->delete_after_compressing;
      spec.compression_level =
          z7::ui::archive_support::to_native_string(
              payload.add->compression_level.trimmed());
      spec.method_value =
          z7::ui::archive_support::to_native_string(payload.add->method_value.trimmed());
      spec.dictionary_size =
          z7::ui::archive_support::to_native_string(
              payload.add->dictionary_size.trimmed());
      spec.word_size =
          z7::ui::archive_support::to_native_string(payload.add->word_size.trimmed());
      spec.solid_block_size =
          z7::ui::archive_support::to_native_string(
              payload.add->solid_block_size.trimmed());
      spec.thread_count =
          z7::ui::archive_support::to_native_string(payload.add->thread_count.trimmed());
      spec.volume_size =
          z7::ui::archive_support::to_native_string(payload.add->volume_size.trimmed());
      spec.password =
          z7::ui::archive_support::to_native_string(payload.add->password.trimmed());
      spec.encrypt_headers_defined = payload.add->encrypt_headers_defined;
      spec.encrypt_headers = payload.add->encrypt_headers;
      spec.encryption_method =
          z7::ui::archive_support::to_native_string(
              payload.add->encryption_method.trimmed());
      for (const QString& value : payload.add->raw_update_switches) {
        spec.raw_update_switches.push_back(
            z7::ui::archive_support::to_native_string(value.trimmed()));
      }
      for (const QString& value : payload.add->extra_parameters) {
        spec.extra_parameters.push_back(
            z7::ui::archive_support::to_native_string(value.trimmed()));
      }
      for (const QString& value : payload.add->input_paths) {
        spec.input_paths.push_back(
            z7::ui::archive_support::to_native_string(value.trimmed()));
      }
      return GuiTaskSpec{std::move(spec)};
    }
    case TaskIpcCommandKind::kExtract: {
      if (!payload.extract.has_value()) {
        if (error_message != nullptr) {
          *error_message = QStringLiteral("Extract command payload is missing.");
        }
        return std::nullopt;
      }
      ExtractTaskSpec spec;
      spec.show_dialog = payload.show_dialog;
      spec.output_dir =
          z7::ui::archive_support::to_native_string(payload.extract->output_dir.trimmed());
      spec.split_dest_enabled = payload.extract->split_dest_enabled;
      spec.split_dest_name =
          z7::ui::archive_support::to_native_string(
              payload.extract->split_dest_name.trimmed());
      spec.overwrite_switch =
          z7::ui::archive_support::to_native_string(
              payload.extract->overwrite_switch.trimmed());
      spec.archive_type =
          z7::ui::archive_support::to_native_string(payload.extract->archive_type.trimmed());
      spec.eliminate_root_duplication =
          payload.extract->eliminate_root_duplication;
      spec.restore_file_security = payload.extract->restore_file_security;
      spec.zone_id_mode =
          z7::ui::archive_support::to_native_string(
              payload.extract->zone_id_mode.trimmed());
      spec.password =
          z7::ui::archive_support::to_native_string(payload.extract->password.trimmed());
      if (payload.show_dialog) {
        spec.path_mode.clear();
      } else {
        spec.path_mode = "full";
      }
      spec.path_remaps.reserve(static_cast<size_t>(payload.extract->path_remaps.size()));
      for (const auto& remap : payload.extract->path_remaps) {
        ExtractPathRemap task_remap;
        task_remap.match_kind = convert_remap_match_kind(remap.match_kind);
        task_remap.source_path =
            z7::ui::archive_support::to_native_string(remap.source_path.trimmed());
        task_remap.destination_path =
            z7::ui::archive_support::to_native_string(remap.destination_path.trimmed());
        spec.path_remaps.push_back(std::move(task_remap));
      }
      for (const QString& value : payload.extract->archive_inputs) {
        spec.archive_inputs.push_back(
            z7::ui::archive_support::to_native_string(value.trimmed()));
      }
      return GuiTaskSpec{std::move(spec)};
    }
    case TaskIpcCommandKind::kArchiveExport: {
      if (!payload.archive_export.has_value()) {
        if (error_message != nullptr) {
          *error_message =
              QStringLiteral("Archive export command payload is missing.");
        }
        return std::nullopt;
      }
      if (payload.archive_export->root_archive_path.trimmed().isEmpty()) {
        if (error_message != nullptr) {
          *error_message =
              QStringLiteral("Archive export command requires root archive path.");
        }
        return std::nullopt;
      }
      if (payload.archive_export->output_dir.trimmed().isEmpty()) {
        if (error_message != nullptr) {
          *error_message =
              QStringLiteral("Archive export command requires output directory.");
        }
        return std::nullopt;
      }
      ArchiveExportTaskSpec spec;
      spec.root_archive_path =
          z7::ui::archive_support::to_native_string(
              payload.archive_export->root_archive_path.trimmed());
      spec.root_archive_type =
          z7::ui::archive_support::to_native_string(
              payload.archive_export->root_archive_type.trimmed());
      spec.output_dir =
          z7::ui::archive_support::to_native_string(
              payload.archive_export->output_dir.trimmed());
      spec.overwrite_mode =
          z7::ui::archive_support::to_native_string(
              payload.archive_export->overwrite_mode.trimmed());
      spec.path_mode =
          z7::ui::archive_support::to_native_string(
              payload.archive_export->path_mode.trimmed());
      spec.eliminate_root_duplication =
          payload.archive_export->eliminate_root_duplication;
      spec.restore_file_security =
          payload.archive_export->restore_file_security;
      spec.zone_id_mode =
          z7::ui::archive_support::to_native_string(
              payload.archive_export->zone_id_mode.trimmed());
      spec.password =
          z7::ui::archive_support::to_native_string(
              payload.archive_export->password.trimmed());
      spec.path_remaps.reserve(
          static_cast<size_t>(payload.archive_export->path_remaps.size()));
      for (const auto& remap : payload.archive_export->path_remaps) {
        ExtractPathRemap task_remap;
        task_remap.match_kind = convert_remap_match_kind(remap.match_kind);
        task_remap.source_path =
            z7::ui::archive_support::to_native_string(
                remap.source_path.trimmed());
        task_remap.destination_path =
            z7::ui::archive_support::to_native_string(
                remap.destination_path.trimmed());
        spec.path_remaps.push_back(std::move(task_remap));
      }
      for (const QString& value :
           payload.archive_export->nested_archive_entries) {
        spec.nested_archive_entries.push_back(
            z7::ui::archive_support::to_native_string(value.trimmed()));
      }
      for (const QString& value :
           payload.archive_export->archive_entry_paths) {
        spec.archive_entry_paths.push_back(
            z7::ui::archive_support::to_native_string(value.trimmed()));
      }
      return GuiTaskSpec{std::move(spec)};
    }
    case TaskIpcCommandKind::kTest: {
      if (!payload.test.has_value()) {
        if (error_message != nullptr) {
          *error_message = QStringLiteral("Test command payload is missing.");
        }
        return std::nullopt;
      }
      if (payload.open.has_value()) {
        const QString open_archive_path = payload.open->archive_path.trimmed();
        if (open_archive_path.isEmpty()) {
          if (error_message != nullptr) {
            *error_message =
                QStringLiteral("Archive-aware test command requires archive path.");
          }
          return std::nullopt;
        }
        ArchiveTestTaskSpec spec;
        spec.archive_path = z7::ui::archive_support::to_native_string(
            payload.open->archive_path.trimmed());
        spec.archive_type = z7::ui::archive_support::to_native_string(
            payload.open->archive_type.trimmed());
        for (const QString& value : payload.open->nested_archive_entries) {
          spec.nested_archive_entries.push_back(
              z7::ui::archive_support::to_native_string(value.trimmed()));
        }
        for (const QString& value : payload.test->archive_inputs) {
          spec.archive_entry_paths.push_back(
              z7::ui::archive_support::to_native_string(value.trimmed()));
        }
        return GuiTaskSpec{std::move(spec)};
      }
      TestTaskSpec spec;
      for (const QString& value : payload.test->archive_inputs) {
        spec.archive_inputs.push_back(
            z7::ui::archive_support::to_native_string(value.trimmed()));
      }
      return GuiTaskSpec{std::move(spec)};
    }
    case TaskIpcCommandKind::kHash: {
      if (!payload.hash.has_value()) {
        if (error_message != nullptr) {
          *error_message = QStringLiteral("Hash command payload is missing.");
        }
        return std::nullopt;
      }
      if (payload.open.has_value()) {
        if (payload.open->archive_path.trimmed().isEmpty()) {
          if (error_message != nullptr) {
            *error_message =
                QStringLiteral("Archive-aware hash command requires archive path.");
          }
          return std::nullopt;
        }
        ArchiveHashTaskSpec spec;
        spec.archive_path = z7::ui::archive_support::to_native_string(
            payload.open->archive_path.trimmed());
        spec.archive_type = z7::ui::archive_support::to_native_string(
            payload.open->archive_type.trimmed());
        spec.hash_method = z7::ui::archive_support::to_native_string(
            payload.hash->hash_method.trimmed());
        for (const QString& value : payload.open->nested_archive_entries) {
          spec.nested_archive_entries.push_back(
              z7::ui::archive_support::to_native_string(value.trimmed()));
        }
        for (const QString& value : payload.hash->input_paths) {
          spec.archive_entry_paths.push_back(
              z7::ui::archive_support::to_native_string(value.trimmed()));
        }
        return GuiTaskSpec{std::move(spec)};
      }
      HashTaskSpec spec;
      spec.hash_method =
          z7::ui::archive_support::to_native_string(payload.hash->hash_method.trimmed());
      for (const QString& value : payload.hash->input_paths) {
        spec.input_paths.push_back(
            z7::ui::archive_support::to_native_string(value.trimmed()));
      }
      return GuiTaskSpec{std::move(spec)};
    }
    case TaskIpcCommandKind::kBenchmark: {
      if (!payload.benchmark.has_value()) {
        if (error_message != nullptr) {
          *error_message = QStringLiteral("Benchmark command payload is missing.");
        }
        return std::nullopt;
      }
      BenchmarkTaskSpec spec;
      spec.method_value =
          z7::ui::archive_support::to_native_string(
              payload.benchmark->method_value.trimmed());
      spec.dictionary_size =
          z7::ui::archive_support::to_native_string(
              payload.benchmark->dictionary_size.trimmed());
      spec.thread_count =
          z7::ui::archive_support::to_native_string(
              payload.benchmark->thread_count.trimmed());
      for (const QString& value : payload.benchmark->operands) {
        spec.operands.push_back(
            z7::ui::archive_support::to_native_string(value.trimmed()));
      }
      return GuiTaskSpec{std::move(spec)};
    }
    case TaskIpcCommandKind::kOpen: {
      if (!payload.open.has_value()) {
        if (error_message != nullptr) {
          *error_message = QStringLiteral("Open command payload is missing.");
        }
        return std::nullopt;
      }
      OpenTaskSpec spec;
      spec.archive_path =
          z7::ui::archive_support::to_native_string(payload.open->archive_path.trimmed());
      spec.archive_type =
          z7::ui::archive_support::to_native_string(payload.open->archive_type.trimmed());
      spec.entry_path =
          z7::ui::archive_support::to_native_string(payload.open->entry_path.trimmed());
      for (const QString& value : payload.open->nested_archive_entries) {
        spec.nested_archive_entries.push_back(
            z7::ui::archive_support::to_native_string(value.trimmed()));
      }
      return GuiTaskSpec{std::move(spec)};
    }
    case TaskIpcCommandKind::kCli:
      if (error_message != nullptr) {
        *error_message =
            QStringLiteral("CLI task IPC commands are handled by the 7zG entrypoint.");
      }
      return std::nullopt;
    case TaskIpcCommandKind::kNone:
    default:
      if (error_message != nullptr) {
        *error_message = QStringLiteral("Unsupported task IPC command kind.");
      }
      return std::nullopt;
  }
}

}  // namespace z7::ui::gui
