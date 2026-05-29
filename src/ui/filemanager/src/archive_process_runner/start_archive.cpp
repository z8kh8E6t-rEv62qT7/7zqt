// src/ui/filemanager/src/archive_process_runner/start_archive.cpp
// Role: Archive-oriented start_* entry points for runner tasks.

#include "archive_process_runner.h"

#include "archive_error.h"
#include "archive_string_codec_qt.h"
#include "extract_memory_settings.h"
#include "official_lang_catalog.h"
#include "helpers.h"

#include <QCoreApplication>
#include <QDir>

namespace z7::ui::filemanager {

using namespace runner_helpers;
using z7::ui::archive_support::to_native_string;
using z7::ui::archive_support::to_native_string_list;
using z7::ui::archive_support::to_utf8_string;
using z7::ui::archive_support::to_utf8_string_list;

namespace {

void apply_configured_extract_memory_limit(z7::app::ExtractRequest* request) {
  if (request == nullptr) {
    return;
  }
  const uint64_t bytes =
      z7::ui::runtime_support::configured_extract_memory_limit_bytes();
  if (bytes == 0) {
    return;
  }
  request->configured_memory_limit_bytes = bytes;
  request->configured_memory_limit_defined = true;
}

void apply_configured_extract_memory_limit(z7::app::TestRequest* request) {
  if (request == nullptr) {
    return;
  }
  const uint64_t bytes =
      z7::ui::runtime_support::configured_extract_memory_limit_bytes();
  if (bytes == 0) {
    return;
  }
  request->configured_memory_limit_bytes = bytes;
  request->configured_memory_limit_defined = true;
}

QString side_by_side_sfx_module_path() {
  return QDir(QCoreApplication::applicationDirPath()).filePath(
      QStringLiteral("7z.sfx"));
}

}  // namespace

bool ArchiveProcessRunner::start_compress(const QString& archive_path,
                                          const QString& format,
                                          const QStringList& input_paths) {
  AddTaskOptions options;
  options.archive_path = archive_path;
  options.format = format;
  options.input_paths = input_paths;
  return start_add_to_archive(options);
}

bool ArchiveProcessRunner::start_add_to_archive(const AddTaskOptions& options) {
  if (options.input_paths.isEmpty() && options.input_items.isEmpty()) {
    return finish_immediately(z7::app::make_immediate_result(
        7,
        z7::app::ArchiveErrorDomain::kInvalidArguments,
        to_utf8_string(z7::ui::runtime_support::L(3015))));
  }

  z7::app::AddRequest request;
  request.archive_path = to_native_string(options.archive_path);
  request.format = to_native_string(options.format.trimmed());
  if (options.session_token.is_valid()) {
    request.session_token = options.session_token;
  }
  request.directory = to_utf8_string(options.directory.trimmed());
  request.update_mode = to_native_string(options.update_mode.trimmed());
  request.raw_update_switches = to_native_string_list(options.raw_update_switches);
  request.path_mode = to_native_string(options.path_mode.trimmed());
  request.create_sfx = options.create_sfx;
  if (request.create_sfx) {
    request.sfx_module_path = to_native_string(side_by_side_sfx_module_path());
  }
  request.share_for_write = options.share_for_write;
  request.delete_after_compressing = options.delete_after_compressing;
  request.compression_level = to_native_string(options.compression_level.trimmed());
  request.method_value = to_native_string(options.method_value.trimmed());
  request.dictionary_size = to_native_string(options.dictionary_size.trimmed());
  request.word_size = to_native_string(options.word_size.trimmed());
  request.solid_block_size = to_native_string(options.solid_block_size.trimmed());
  request.thread_count = to_native_string(options.thread_count.trimmed());
  request.volume_size = to_native_string(options.volume_size.trimmed());
  request.password = to_native_string(options.password);
  request.encrypt_headers_defined = options.encrypt_headers_defined;
  request.encrypt_headers = options.encrypt_headers;
  request.encryption_method = to_native_string(options.encryption_method.trimmed());
  request.extra_parameters = to_native_string_list(options.extra_parameters);
  request.input_paths = to_native_string_list(options.input_paths);
  request.input_items.reserve(options.input_items.size());
  for (const ArchiveAddInputItem& item : options.input_items) {
    z7::app::AddInputItem backend_item;
    backend_item.filesystem_path = to_native_string(item.filesystem_path);
    backend_item.archive_entry = to_utf8_string(item.archive_entry);
    request.input_items.push_back(std::move(backend_item));
  }

  return start_operation(
      QStringLiteral("Add"),
      QStringList{options.archive_path},
      z7::app::ArchiveRequest{std::move(request)});
}

bool ArchiveProcessRunner::start_extract(const QString& archive_path,
                                         const QString& output_dir,
                                         OverwriteMode overwrite_mode,
                                         const QString& archive_type_hint,
                                         z7::app::ExtractPathMode path_mode,
                                         bool eliminate_root_duplication,
                                         const QString& password,
                                         bool restore_file_security) {
  z7::app::ExtractRequest request;
  request.archive_path = to_native_string(archive_path);
  request.output_dir = to_native_string(output_dir);
  request.archive_type_hint = to_native_string(archive_type_hint);
  request.overwrite_mode = to_backend_overwrite_mode(overwrite_mode);
  request.path_mode = path_mode;
  request.eliminate_root_duplication = eliminate_root_duplication;
  request.password = to_native_string(password);
  request.restore_file_security = restore_file_security;
  apply_configured_extract_memory_limit(&request);

  return start_operation(
      QStringLiteral("Extract"),
      QStringList{archive_path, output_dir},
      z7::app::ArchiveRequest{std::move(request)});
}

bool ArchiveProcessRunner::start_extract_many(const QStringList& archive_paths,
                                              const QString& output_dir,
                                              OverwriteMode overwrite_mode,
                                              const QString& archive_type_hint,
                                              z7::app::ExtractPathMode path_mode,
                                              bool eliminate_root_duplication,
                                              const QString& password,
                                              bool restore_file_security) {
  if (archive_paths.isEmpty()) {
    return finish_immediately(z7::app::make_immediate_result(
        7,
        z7::app::ArchiveErrorDomain::kInvalidArguments,
        to_utf8_string(z7::ui::runtime_support::L(3015))));
  }

  z7::app::ExtractRequest request;
  request.archive_paths = to_native_string_list(archive_paths);
  request.output_dir = to_native_string(output_dir);
  request.archive_type_hint = to_native_string(archive_type_hint);
  request.overwrite_mode = to_backend_overwrite_mode(overwrite_mode);
  request.path_mode = path_mode;
  request.eliminate_root_duplication = eliminate_root_duplication;
  request.password = to_native_string(password);
  request.restore_file_security = restore_file_security;
  apply_configured_extract_memory_limit(&request);

  return start_operation(
      QStringLiteral("Extract"),
      QStringList{output_dir} + archive_paths,
      z7::app::ArchiveRequest{std::move(request)});
}

bool ArchiveProcessRunner::start_extract_selected(const QString& archive_path,
                                                  const QString& output_dir,
                                                  OverwriteMode overwrite_mode,
                                                  const QStringList& archive_entries,
                                                  const QString& archive_type_hint,
                                                  z7::app::ExtractPathMode path_mode,
                                                  bool eliminate_root_duplication,
                                                  const QString& password,
                                                  bool restore_file_security) {
  if (archive_entries.isEmpty()) {
    return finish_immediately(z7::app::make_immediate_result(
        7,
        z7::app::ArchiveErrorDomain::kInvalidArguments,
        to_utf8_string(z7::ui::runtime_support::L(3015))));
  }

  z7::app::ExtractRequest request;
  request.archive_path = to_native_string(archive_path);
  request.output_dir = to_native_string(output_dir);
  request.archive_type_hint = to_native_string(archive_type_hint);
  request.overwrite_mode = to_backend_overwrite_mode(overwrite_mode);
  request.path_mode = path_mode;
  request.eliminate_root_duplication = eliminate_root_duplication;
  request.password = to_native_string(password);
  request.restore_file_security = restore_file_security;
  request.entries = to_utf8_string_list(archive_entries);
  apply_configured_extract_memory_limit(&request);

  return start_operation(
      QStringLiteral("ExtractSelected"),
      QStringList{archive_path, output_dir},
      z7::app::ArchiveRequest{std::move(request)});
}

bool ArchiveProcessRunner::start_open_archive(
    const QString& archive_path,
    const QString& virtual_dir,
    const QString& archive_type_hint,
    bool recursive_dirs,
    bool include_detailed_props,
    std::shared_ptr<std::optional<z7::app::ListResult>> out_list_result) {
  if (archive_path.trimmed().isEmpty()) {
    return finish_immediately(z7::app::make_immediate_result(
        7,
        z7::app::ArchiveErrorDomain::kInvalidArguments,
        to_utf8_string(z7::ui::runtime_support::L(3015))));
  }

  z7::app::ListRequest request;
  request.archive_path = to_native_string(archive_path);
  request.directory = to_utf8_string(virtual_dir);
  request.archive_type_hint = to_native_string(archive_type_hint.trimmed());
  request.recursive_dirs = recursive_dirs;
  request.include_detailed_props = include_detailed_props;

  return start_operation(
      QStringLiteral("OpenArchive"),
      QStringList{archive_path},
      z7::app::ArchiveRequest{std::move(request)},
      std::move(out_list_result));
}

bool ArchiveProcessRunner::start_test(const QString& archive_path) {
  z7::app::TestRequest request;
  request.archive_path = to_native_string(archive_path);
  apply_configured_extract_memory_limit(&request);

  return start_operation(
      QStringLiteral("Test"),
      QStringList{archive_path},
      z7::app::ArchiveRequest{std::move(request)});
}

bool ArchiveProcessRunner::start_test_many(const QStringList& archive_paths) {
  if (archive_paths.isEmpty()) {
    return finish_immediately(z7::app::make_immediate_result(
        7,
        z7::app::ArchiveErrorDomain::kInvalidArguments,
        to_utf8_string(z7::ui::runtime_support::L(3015))));
  }

  z7::app::TestRequest request;
  request.archive_paths = to_native_string_list(archive_paths);
  apply_configured_extract_memory_limit(&request);

  return start_operation(
      QStringLiteral("Test"),
      archive_paths,
      z7::app::ArchiveRequest{std::move(request)});
}

bool ArchiveProcessRunner::start_test_entries(const QString& archive_path,
                                              const QStringList& archive_entries) {
  if (archive_path.isEmpty() || archive_entries.isEmpty()) {
    return finish_immediately(z7::app::make_immediate_result(
        7,
        z7::app::ArchiveErrorDomain::kInvalidArguments,
        to_utf8_string(z7::ui::runtime_support::L(3015))));
  }

  z7::app::TestRequest request;
  request.archive_path = to_native_string(archive_path);
  request.entries = to_utf8_string_list(archive_entries);
  apply_configured_extract_memory_limit(&request);

  return start_operation(
      QStringLiteral("Test"),
      QStringList{archive_path},
      z7::app::ArchiveRequest{std::move(request)});
}

bool ArchiveProcessRunner::start_test_in_session(z7::app::ArchiveSessionToken token,
                                                 const QStringList& archive_entries) {
  if (!token.is_valid()) {
    return finish_immediately(z7::app::make_immediate_result(
        7,
        z7::app::ArchiveErrorDomain::kInvalidArguments,
        to_utf8_string(z7::ui::runtime_support::L(3015))));
  }

  z7::app::TestRequest request;
  request.session_token = token;
  request.entries = to_utf8_string_list(archive_entries);
  apply_configured_extract_memory_limit(&request);

  return start_operation(
      QStringLiteral("TestInSession"),
      QStringList{},
      z7::app::ArchiveRequest{std::move(request)});
}

bool ArchiveProcessRunner::start_delete_entries(
    const QString& archive_path,
    const QStringList& archive_entries,
    z7::app::ArchiveSessionToken session_token) {
  if (archive_entries.isEmpty()) {
    return finish_immediately(z7::app::make_immediate_result(
        7,
        z7::app::ArchiveErrorDomain::kInvalidArguments,
        to_utf8_string(z7::ui::runtime_support::L(3015))));
  }

  z7::app::DeleteRequest request;
  request.archive_path = to_native_string(archive_path);
  if (session_token.is_valid()) {
    request.session_token = session_token;
  }
  request.entries = to_utf8_string_list(archive_entries);

  return start_operation(
      QStringLiteral("Delete"),
      QStringList{archive_path},
      z7::app::ArchiveRequest{std::move(request)});
}

bool ArchiveProcessRunner::start_open_from_path(
    const QString& archive_path,
    const QString& archive_type_hint,
    std::shared_ptr<std::optional<z7::app::OpenArchiveSessionResult>> out_session_result) {
  if (archive_path.trimmed().isEmpty()) {
    return finish_immediately(z7::app::make_immediate_result(
        7,
        z7::app::ArchiveErrorDomain::kInvalidArguments,
        to_utf8_string(z7::ui::runtime_support::L(3015))));
  }

  z7::app::OpenArchiveFromPathRequest request;
  request.archive_path = to_native_string(archive_path);
  request.archive_type_hint = to_native_string(archive_type_hint.trimmed());

  return start_operation(
      QStringLiteral("OpenArchiveFromPath"),
      QStringList{archive_path},
      z7::app::ArchiveRequest{std::move(request)},
      /*out_list_result=*/{},
      std::move(out_session_result));
}

bool ArchiveProcessRunner::start_open_nested(
    z7::app::ArchiveSessionToken parent,
    uint32_t entry_index,
    const QString& archive_type_hint,
    size_t size_budget,
    const QString& display_path_hint,
    std::shared_ptr<std::optional<z7::app::OpenArchiveSessionResult>> out_session_result) {
  if (!parent.is_valid()) {
    return finish_immediately(z7::app::make_immediate_result(
        7,
        z7::app::ArchiveErrorDomain::kInvalidArguments,
        to_utf8_string(z7::ui::runtime_support::L(3015))));
  }

  z7::app::OpenArchiveFromParentRequest request;
  request.parent = parent;
  request.entry_index = entry_index;
  request.archive_type_hint = to_native_string(archive_type_hint.trimmed());
  request.size_budget = size_budget;
  request.display_path_hint = to_utf8_string(display_path_hint);

  return start_operation(
      QStringLiteral("OpenArchiveFromParent"),
      QStringList{display_path_hint},
      z7::app::ArchiveRequest{std::move(request)},
      /*out_list_result=*/{},
      std::move(out_session_result));
}

bool ArchiveProcessRunner::start_open_nested_by_path(
    z7::app::ArchiveSessionToken parent,
    const QString& entry_path,
    const QString& archive_type_hint,
    size_t size_budget,
    const QString& display_path_hint,
    std::shared_ptr<std::optional<z7::app::OpenArchiveSessionResult>> out_session_result) {
  if (!parent.is_valid() || entry_path.trimmed().isEmpty()) {
    return finish_immediately(z7::app::make_immediate_result(
        7,
        z7::app::ArchiveErrorDomain::kInvalidArguments,
        to_utf8_string(z7::ui::runtime_support::L(3015))));
  }

  z7::app::OpenArchiveFromParentRequest request;
  request.parent = parent;
  request.entry_path = to_utf8_string(entry_path);
  request.archive_type_hint = to_native_string(archive_type_hint.trimmed());
  request.size_budget = size_budget;
  request.display_path_hint =
      to_utf8_string(display_path_hint.isEmpty() ? entry_path : display_path_hint);

  return start_operation(
      QStringLiteral("OpenArchiveFromParent"),
      QStringList{display_path_hint.isEmpty() ? entry_path : display_path_hint},
      z7::app::ArchiveRequest{std::move(request)},
      /*out_list_result=*/{},
      std::move(out_session_result));
}

bool ArchiveProcessRunner::start_close_session(z7::app::ArchiveSessionToken token) {
  if (!token.is_valid()) {
    return finish_immediately(z7::app::make_immediate_result(
        7,
        z7::app::ArchiveErrorDomain::kInvalidArguments,
        to_utf8_string(z7::ui::runtime_support::L(3015))));
  }

  z7::app::CloseArchiveSessionRequest request;
  request.token = token;

  return start_operation(
      QStringLiteral("CloseArchiveSession"),
      QStringList{},
      z7::app::ArchiveRequest{std::move(request)});
}

bool ArchiveProcessRunner::start_list_in_session(
    z7::app::ArchiveSessionToken token,
    const QString& virtual_dir,
    bool recursive_dirs,
    bool include_detailed_props,
    std::shared_ptr<std::optional<z7::app::ListResult>> out_list_result) {
  if (!token.is_valid()) {
    return finish_immediately(z7::app::make_immediate_result(
        7,
        z7::app::ArchiveErrorDomain::kInvalidArguments,
        to_utf8_string(z7::ui::runtime_support::L(3015))));
  }

  z7::app::ListRequest request;
  request.session_token = token;
  request.directory = to_utf8_string(virtual_dir);
  request.recursive_dirs = recursive_dirs;
  request.include_detailed_props = include_detailed_props;

  return start_operation(
      QStringLiteral("ListInSession"),
      QStringList{},
      z7::app::ArchiveRequest{std::move(request)},
      std::move(out_list_result));
}

bool ArchiveProcessRunner::start_extract_in_session(
    z7::app::ArchiveSessionToken token,
    const QString& output_dir,
    OverwriteMode overwrite_mode,
    const QStringList& archive_entries,
    z7::app::ExtractPathMode path_mode,
    bool eliminate_root_duplication,
    const QString& password,
    bool restore_file_security) {
  if (!token.is_valid()) {
    return finish_immediately(z7::app::make_immediate_result(
        7,
        z7::app::ArchiveErrorDomain::kInvalidArguments,
        to_utf8_string(z7::ui::runtime_support::L(3015))));
  }

  z7::app::ExtractRequest request;
  request.session_token = token;
  request.output_dir = to_native_string(output_dir);
  request.overwrite_mode = to_backend_overwrite_mode(overwrite_mode);
  request.path_mode = path_mode;
  request.eliminate_root_duplication = eliminate_root_duplication;
  request.password = to_native_string(password);
  request.restore_file_security = restore_file_security;
  if (!archive_entries.isEmpty()) {
    request.entries = to_utf8_string_list(archive_entries);
  }
  apply_configured_extract_memory_limit(&request);

  return start_operation(
      QStringLiteral("ExtractInSession"),
      QStringList{output_dir},
      z7::app::ArchiveRequest{std::move(request)});
}

}  // namespace z7::ui::filemanager
