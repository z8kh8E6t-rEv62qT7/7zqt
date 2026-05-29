#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QtGlobal>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>

#include "archive_session.h"

class QWidget;

namespace z7::ui::filemanager {

enum class OverwriteMode {
  kAsk,
  kOverwrite,
  kSkip,
  kRenameExisting,
  kRenameExtracted
};

struct ArchiveAddInputItem {
  QString filesystem_path;
  QString archive_entry;
};

struct AddTaskOptions {
  QString archive_path;
  QString format;
  z7::app::ArchiveSessionToken session_token;
  QStringList input_paths;
  QVector<ArchiveAddInputItem> input_items;
  QString directory;
  QString update_mode = QStringLiteral("add");
  QStringList raw_update_switches;
  QString path_mode = QStringLiteral("relative");
  bool create_sfx = false;
  bool share_for_write = false;
  bool delete_after_compressing = false;
  QString compression_level;
  QString method_value;
  QString dictionary_size;
  QString word_size;
  QString solid_block_size;
  QString thread_count;
  QString volume_size;
  QString password;
  bool encrypt_headers_defined = false;
  bool encrypt_headers = false;
  QString encryption_method;
  QStringList extra_parameters;
};

class ArchiveProcessRunner : public QObject {
  Q_OBJECT

 public:
  using OverwritePromptHandler =
      std::function<z7::app::OverwriteDecision(const z7::app::OverwritePrompt&)>;
  using PromptParentProvider = std::function<QWidget*()>;

  explicit ArchiveProcessRunner(QObject* parent = nullptr);

  bool start_compress(const QString& archive_path,
                      const QString& format,
                      const QStringList& input_paths);
  bool start_add_to_archive(const AddTaskOptions& options);

  bool start_extract(const QString& archive_path,
                     const QString& output_dir,
                     OverwriteMode overwrite_mode,
                     const QString& archive_type_hint = QString(),
                     z7::app::ExtractPathMode path_mode = z7::app::ExtractPathMode::kFullPaths,
                     bool eliminate_root_duplication = false,
                     const QString& password = QString(),
                     bool restore_file_security = false);

  bool start_extract_many(const QStringList& archive_paths,
                          const QString& output_dir,
                          OverwriteMode overwrite_mode,
                          const QString& archive_type_hint = QString(),
                          z7::app::ExtractPathMode path_mode = z7::app::ExtractPathMode::kFullPaths,
                          bool eliminate_root_duplication = false,
                          const QString& password = QString(),
                          bool restore_file_security = false);

  bool start_extract_selected(const QString& archive_path,
                              const QString& output_dir,
                              OverwriteMode overwrite_mode,
                              const QStringList& archive_entries,
                              const QString& archive_type_hint = QString(),
                              z7::app::ExtractPathMode path_mode = z7::app::ExtractPathMode::kFullPaths,
                              bool eliminate_root_duplication = false,
                              const QString& password = QString(),
                              bool restore_file_security = false);

  bool start_open_archive(
      const QString& archive_path,
      const QString& virtual_dir = QString(),
      const QString& archive_type_hint = QString(),
      bool recursive_dirs = false,
      bool include_detailed_props = true,
      std::shared_ptr<std::optional<z7::app::ListResult>> out_list_result = {});

  // Open a top-level archive into a session held by the registry. The
  // returned token (via out_session_result) is what subsequent
  // start_list_in_session / start_extract_in_session / start_close_session
  // calls reference.
  bool start_open_from_path(
      const QString& archive_path,
      const QString& archive_type_hint = QString(),
      std::shared_ptr<std::optional<z7::app::OpenArchiveSessionResult>>
          out_session_result = {});

  // Open a nested archive at `entry_index` within the parent session. Tries
  // stream / memory / temp-file strategies in order.
  bool start_open_nested(
      z7::app::ArchiveSessionToken parent,
      uint32_t entry_index,
      const QString& archive_type_hint = QString(),
      size_t size_budget = 0,
      const QString& display_path_hint = QString(),
      std::shared_ptr<std::optional<z7::app::OpenArchiveSessionResult>>
          out_session_result = {});

  // Path-based variant: resolves `entry_path` to an index in the parent
  // archive on the backend side. Prefer this from UI code, which already
  // works in path strings.
  bool start_open_nested_by_path(
      z7::app::ArchiveSessionToken parent,
      const QString& entry_path,
      const QString& archive_type_hint = QString(),
      size_t size_budget = 0,
      const QString& display_path_hint = QString(),
      std::shared_ptr<std::optional<z7::app::OpenArchiveSessionResult>>
          out_session_result = {});

  // Release a session previously obtained via start_open_from_path /
  // start_open_nested.
  bool start_close_session(z7::app::ArchiveSessionToken token);

  // List entries inside an already-opened session (no re-parse).
  bool start_list_in_session(
      z7::app::ArchiveSessionToken token,
      const QString& virtual_dir = QString(),
      bool recursive_dirs = false,
      bool include_detailed_props = true,
      std::shared_ptr<std::optional<z7::app::ListResult>> out_list_result = {});

  // Extract entries via an already-opened session (no re-parse).
  bool start_extract_in_session(z7::app::ArchiveSessionToken token,
                                const QString& output_dir,
                                OverwriteMode overwrite_mode,
                                const QStringList& archive_entries = {},
                                z7::app::ExtractPathMode path_mode =
                                    z7::app::ExtractPathMode::kFullPaths,
                                bool eliminate_root_duplication = false,
                                const QString& password = QString(),
                                bool restore_file_security = false);

  bool start_test(const QString& archive_path);

  bool start_test_many(const QStringList& archive_paths);

  bool start_test_entries(const QString& archive_path,
                          const QStringList& archive_entries);
  bool start_test_in_session(z7::app::ArchiveSessionToken token,
                             const QStringList& archive_entries = {});

  bool start_benchmark(uint32_t iterations = 0,
                       const QString& thread_count = QString(),
                       const QString& dictionary_size = QString(),
                       bool total_mode = false);

  bool start_split(const QString& source_file_path,
                   const QString& output_dir,
                   const QString& volume_size_spec);

  bool start_combine(const QString& source_part_path,
                     const QString& output_dir);

  bool start_hash(const QStringList& input_paths,
                  const QString& hash_method,
                  bool recursive_dirs = true);

  bool start_delete_entries(const QString& archive_path,
                            const QStringList& archive_entries,
                            z7::app::ArchiveSessionToken session_token = {});
  bool start_copy_paths(const QStringList& source_paths,
                        const QString& destination_dir,
                        OverwriteMode overwrite_mode = OverwriteMode::kOverwrite,
                        const QString& destination_path = QString());

  bool start_move_paths(const QStringList& source_paths,
                        const QString& destination_dir,
                        OverwriteMode overwrite_mode = OverwriteMode::kOverwrite,
                        const QString& destination_path = QString());

  bool start_delete_paths(const QStringList& paths,
                          bool to_recycle_bin = true);
  bool start_rename_path(const QString& source_path, const QString& new_name);
  bool start_rename_archive_entry(const QString& archive_path,
                                  z7::app::ArchiveSessionToken session_token,
                                  const QString& archive_entry,
                                  const QString& new_name,
                                  bool entry_is_dir);
  bool start_create_directory(const QString& parent_dir, const QString& name);
  bool start_create_file(const QString& parent_dir, const QString& name);
  bool start_archive_comment(const QString& archive_path,
                             const QString& entry_path,
                             z7::app::ArchiveSessionToken session_token,
                             const QString& comment);
  bool start_filesystem_comment(const QString& directory_path,
                                const QString& item_name,
                                const QString& comment);

  bool is_running() const;
  bool supports_pause() const;
  z7::app::BackendCapabilities backend_capabilities() const;
  static z7::app::BackendCapabilities query_backend_capabilities();
  const z7::app::OperationResult& last_result() const;
  const z7::app::OperationOutcome& last_outcome() const;
  QString last_operation() const;
  void set_overwrite_prompt_handler(OverwritePromptHandler handler);
  void set_prompt_parent_provider(PromptParentProvider provider);
  void on_task_finished(const z7::app::OperationOutcome& outcome);

 public slots:
  void cancel();
  void pause();
  void resume();

 signals:
  void started(const QString& backend, const QString& operation, const QStringList& targets);
  void log_line(const QString& line);
  void stage_changed(const QString& stage_text);
  void progress_changed(int percent);
  void detailed_progress_changed(bool totals_known,
                                 quint64 total_bytes,
                                 quint64 completed_bytes,
                                 quint64 total_files,
                                 quint64 completed_files,
                                 quint64 error_count,
                                 bool ratio_input_size_known,
                                 quint64 ratio_input_size,
                                 bool ratio_output_size_known,
                                 quint64 ratio_output_size,
                                 bool ratio_compressing_mode,
                                 const QString& current_path);
  // Always emitted asynchronously after runner state has been fully cleaned up.
  void finished(bool ok, int exit_code, int error_domain, const QString& summary);

 private:
  bool start_operation(const QString& operation,
                       const QStringList& targets,
                       const z7::app::ArchiveRequest& request,
                       std::shared_ptr<std::optional<z7::app::ListResult>>
                           out_list_result = {},
                       std::shared_ptr<std::optional<z7::app::OpenArchiveSessionResult>>
                           out_session_result = {});
  bool finish_immediately(const z7::app::OperationResult& result);

  z7::app::ArchiveEngine engine_;
  z7::app::ArchiveSession active_task_;
  bool running_ = false;
  bool cancel_requested_ = false;
  z7::app::OperationResult last_result_;
  z7::app::OperationOutcome last_outcome_;
  QString last_operation_;
  OverwritePromptHandler overwrite_prompt_handler_;
  PromptParentProvider prompt_parent_provider_;
  std::shared_ptr<z7::app::IArchiveDelegate> active_delegate_;
  std::shared_ptr<std::optional<z7::app::ListResult>> pending_list_result_;
  std::shared_ptr<std::optional<z7::app::OpenArchiveSessionResult>> pending_session_result_;
};

}  // namespace z7::ui::filemanager
