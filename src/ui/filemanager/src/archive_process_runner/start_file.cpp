// src/ui/filemanager/src/archive_process_runner/start_file.cpp
// Role: Filesystem and utility start_* entry points for runner tasks.

#include "archive_process_runner.h"

#include "archive_error.h"
#include "archive_string_codec_qt.h"
#include "official_lang_catalog.h"
#include "helpers.h"

#include <QDir>
#include <QFileInfo>

namespace z7::ui::filemanager {

using namespace runner_helpers;
using z7::ui::archive_support::to_native_string;
using z7::ui::archive_support::to_native_string_list;
using z7::ui::archive_support::to_utf8_string;

bool ArchiveProcessRunner::start_benchmark(uint32_t iterations,
                                           const QString& thread_count,
                                           const QString& dictionary_size,
                                           bool total_mode) {
  z7::app::BenchmarkRequest request;
  request.iterations = iterations;
  request.thread_count = to_native_string(thread_count.trimmed());
  request.dictionary_size = to_native_string(dictionary_size.trimmed());
  request.total_mode = total_mode;

  return start_operation(
      QStringLiteral("Benchmark"),
      {},
      z7::app::ArchiveRequest{std::move(request)});
}

bool ArchiveProcessRunner::start_split(const QString& source_file_path,
                                       const QString& output_dir,
                                       const QString& volume_size_spec) {
  z7::app::SplitRequest request;
  request.source_file_path = to_native_string(source_file_path);
  request.output_dir = to_native_string(output_dir);
  request.volume_size_spec = to_native_string(volume_size_spec.trimmed());

  return start_operation(
      QStringLiteral("Split"),
      QStringList{source_file_path, output_dir},
      z7::app::ArchiveRequest{std::move(request)});
}

bool ArchiveProcessRunner::start_combine(const QString& source_part_path,
                                         const QString& output_dir) {
  z7::app::CombineRequest request;
  request.source_part_path = to_native_string(source_part_path);
  request.output_dir = to_native_string(output_dir);

  return start_operation(
      QStringLiteral("Combine"),
      QStringList{source_part_path, output_dir},
      z7::app::ArchiveRequest{std::move(request)});
}

bool ArchiveProcessRunner::start_hash(const QStringList& input_paths,
                                      const QString& hash_method,
                                      bool recursive_dirs) {
  if (input_paths.isEmpty()) {
    return finish_immediately(z7::app::make_immediate_result(
        7,
        z7::app::ArchiveErrorDomain::kInvalidArguments,
        to_utf8_string(z7::ui::runtime_support::L(3015))));
  }

  z7::app::HashRequest request;
  request.input_paths = to_native_string_list(input_paths);
  request.hash_method = to_native_string(hash_method);
  request.recursive_dirs = recursive_dirs;

  return start_operation(
      QStringLiteral("Hash"),
      input_paths,
      z7::app::ArchiveRequest{std::move(request)});
}

bool ArchiveProcessRunner::start_copy_paths(const QStringList& source_paths,
                                            const QString& destination_dir,
                                            OverwriteMode overwrite_mode,
                                            const QString& destination_path) {
  if (source_paths.isEmpty()) {
    return finish_immediately(z7::app::make_immediate_result(
        7,
        z7::app::ArchiveErrorDomain::kInvalidArguments,
        to_utf8_string(z7::ui::runtime_support::L(3015))));
  }

  z7::app::CopyRequest request;
  request.source_paths = to_native_string_list(source_paths);
  request.destination_dir = to_native_string(destination_dir);
  request.destination_path = to_native_string(destination_path);
  request.overwrite_mode = to_backend_overwrite_mode(overwrite_mode);

  return start_operation(
      QStringLiteral("Copy"),
      source_paths,
      z7::app::ArchiveRequest{std::move(request)});
}

bool ArchiveProcessRunner::start_move_paths(const QStringList& source_paths,
                                            const QString& destination_dir,
                                            OverwriteMode overwrite_mode,
                                            const QString& destination_path) {
  if (source_paths.isEmpty()) {
    return finish_immediately(z7::app::make_immediate_result(
        7,
        z7::app::ArchiveErrorDomain::kInvalidArguments,
        to_utf8_string(z7::ui::runtime_support::L(3015))));
  }

  z7::app::MoveRequest request;
  request.source_paths = to_native_string_list(source_paths);
  request.destination_dir = to_native_string(destination_dir);
  request.destination_path = to_native_string(destination_path);
  request.overwrite_mode = to_backend_overwrite_mode(overwrite_mode);

  return start_operation(
      QStringLiteral("Move"),
      source_paths,
      z7::app::ArchiveRequest{std::move(request)});
}

bool ArchiveProcessRunner::start_delete_paths(const QStringList& paths,
                                              bool to_recycle_bin) {
  if (paths.isEmpty()) {
    return finish_immediately(z7::app::make_immediate_result(
        7,
        z7::app::ArchiveErrorDomain::kInvalidArguments,
        to_utf8_string(z7::ui::runtime_support::L(3015))));
  }

  z7::app::DeleteRequest request;
  request.filesystem_paths = to_native_string_list(paths);
  request.use_recycle_bin = to_recycle_bin;

  return start_operation(
      QStringLiteral("Delete"),
      paths,
      z7::app::ArchiveRequest{std::move(request)});
}

bool ArchiveProcessRunner::start_rename_path(const QString& source_path,
                                             const QString& new_name) {
  if (source_path.trimmed().isEmpty() || new_name.trimmed().isEmpty()) {
    return finish_immediately(z7::app::make_immediate_result(
        7,
        z7::app::ArchiveErrorDomain::kInvalidArguments,
        to_utf8_string(z7::ui::runtime_support::L(3015))));
  }

  z7::app::RenameRequest request;
  request.source_path = to_native_string(source_path);
  request.new_name = to_native_string(new_name.trimmed());

  return start_operation(
      QStringLiteral("Rename"),
      QStringList{source_path},
      z7::app::ArchiveRequest{std::move(request)});
}

bool ArchiveProcessRunner::start_rename_archive_entry(
    const QString& archive_path,
    z7::app::ArchiveSessionToken session_token,
    const QString& archive_entry,
    const QString& new_name,
    bool entry_is_dir) {
  if (!session_token.is_valid() || archive_entry.trimmed().isEmpty() ||
      new_name.trimmed().isEmpty()) {
    return finish_immediately(z7::app::make_immediate_result(
        7,
        z7::app::ArchiveErrorDomain::kInvalidArguments,
        to_utf8_string(z7::ui::runtime_support::L(3015))));
  }

  z7::app::RenameRequest request;
  request.archive_path = to_native_string(archive_path);
  request.session_token = session_token;
  request.entry_path = to_utf8_string(archive_entry.trimmed());
  request.new_name = to_native_string(new_name.trimmed());
  request.entry_is_dir = entry_is_dir;

  return start_operation(
      QStringLiteral("Rename"),
      QStringList{archive_entry},
      z7::app::ArchiveRequest{std::move(request)});
}

bool ArchiveProcessRunner::start_create_directory(const QString& parent_dir,
                                                  const QString& name) {
  if (parent_dir.trimmed().isEmpty() || name.trimmed().isEmpty()) {
    return finish_immediately(z7::app::make_immediate_result(
        7,
        z7::app::ArchiveErrorDomain::kInvalidArguments,
        to_utf8_string(z7::ui::runtime_support::L(3015))));
  }

  z7::app::CreateRequest request;
  request.parent_dir = to_native_string(parent_dir);
  request.name = to_native_string(name.trimmed());
  request.kind = z7::app::CreateNodeKind::kDirectory;

  return start_operation(
      QStringLiteral("CreateFolder"),
      QStringList{parent_dir},
      z7::app::ArchiveRequest{std::move(request)});
}

bool ArchiveProcessRunner::start_create_file(const QString& parent_dir,
                                             const QString& name) {
  if (parent_dir.trimmed().isEmpty() || name.trimmed().isEmpty()) {
    return finish_immediately(z7::app::make_immediate_result(
        7,
        z7::app::ArchiveErrorDomain::kInvalidArguments,
        to_utf8_string(z7::ui::runtime_support::L(3015))));
  }

  z7::app::CreateRequest request;
  request.parent_dir = to_native_string(parent_dir);
  request.name = to_native_string(name.trimmed());
  request.kind = z7::app::CreateNodeKind::kFile;

  return start_operation(
      QStringLiteral("CreateFile"),
      QStringList{parent_dir},
      z7::app::ArchiveRequest{std::move(request)});
}

bool ArchiveProcessRunner::start_archive_comment(const QString& archive_path,
                                                 const QString& entry_path,
                                                 z7::app::ArchiveSessionToken session_token,
                                                 const QString& comment) {
  const QString trimmed_entry = entry_path.trimmed();
  if (trimmed_entry.isEmpty()) {
    return finish_immediately(z7::app::make_immediate_result(
        7,
        z7::app::ArchiveErrorDomain::kInvalidArguments,
        to_utf8_string(z7::ui::runtime_support::L(3015))));
  }

  z7::app::ArchiveCommentRequest request;
  request.archive_path = to_native_string(archive_path.trimmed());
  request.entry_path = to_utf8_string(trimmed_entry);
  if (session_token.is_valid()) {
    request.session_token = session_token;
  }
  request.comment = to_utf8_string(comment);

  return start_operation(
      QStringLiteral("CommentArchive"),
      QStringList{archive_path, trimmed_entry},
      z7::app::ArchiveRequest{std::move(request)});
}

bool ArchiveProcessRunner::start_filesystem_comment(const QString& directory_path,
                                                    const QString& item_name,
                                                    const QString& comment) {
  const QString trimmed_directory = directory_path.trimmed();
  const QString trimmed_item_name = item_name.trimmed();
  if (trimmed_directory.isEmpty() || trimmed_item_name.isEmpty()) {
    return finish_immediately(z7::app::make_immediate_result(
        7,
        z7::app::ArchiveErrorDomain::kInvalidArguments,
        to_utf8_string(z7::ui::runtime_support::L(3015))));
  }

  z7::app::FilesystemCommentRequest request;
  request.directory_path = to_native_string(trimmed_directory);
  request.entry_name = to_native_string(trimmed_item_name);
  request.comment = to_utf8_string(comment);

  return start_operation(
      QStringLiteral("CommentFilesystem"),
      QStringList{QDir(trimmed_directory).filePath(trimmed_item_name)},
      z7::app::ArchiveRequest{std::move(request)});
}

}  // namespace z7::ui::filemanager
