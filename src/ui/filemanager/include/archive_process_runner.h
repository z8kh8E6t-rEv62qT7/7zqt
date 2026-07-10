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
#include <string>

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
        using OverwritePromptHandler = std::function<z7::app::OverwriteDecision(z7::app::OverwritePrompt const&)>;
        using PromptParentProvider = std::function<QWidget*()>;

        explicit ArchiveProcessRunner(QObject* parent = nullptr);

        bool start_compress(QString const& archive_path, QString const& format, QStringList const& input_paths);
        bool start_add_to_archive(AddTaskOptions const& options);

        bool start_extract(QString const& archive_path,
                           QString const& output_dir,
                           OverwriteMode overwrite_mode,
                           QString const& archive_type_hint = QString(),
                           z7::app::ExtractPathMode path_mode = z7::app::ExtractPathMode::kFullPaths,
                           bool eliminate_root_duplication = false,
                           QString const& password = QString(),
                           bool restore_file_security = false);

        bool start_extract_many(QStringList const& archive_paths,
                                QString const& output_dir,
                                OverwriteMode overwrite_mode,
                                QString const& archive_type_hint = QString(),
                                z7::app::ExtractPathMode path_mode = z7::app::ExtractPathMode::kFullPaths,
                                bool eliminate_root_duplication = false,
                                QString const& password = QString(),
                                bool restore_file_security = false);

        bool start_extract_selected(QString const& archive_path,
                                    QString const& output_dir,
                                    OverwriteMode overwrite_mode,
                                    QStringList const& archive_entries,
                                    QString const& archive_type_hint = QString(),
                                    z7::app::ExtractPathMode path_mode = z7::app::ExtractPathMode::kFullPaths,
                                    bool eliminate_root_duplication = false,
                                    QString const& password = QString(),
                                    bool restore_file_security = false);

        bool start_open_archive(QString const& archive_path,
                                QString const& virtual_dir = QString(),
                                QString const& archive_type_hint = QString(),
                                bool recursive_dirs = false,
                                bool include_detailed_props = true,
                                std::shared_ptr<std::optional<z7::app::ListResult>> out_list_result = {});

        // Open a top-level archive into a session held by the registry. The
        // returned token (via out_session_result) is what subsequent
        // start_list_in_session / start_extract_in_session / start_close_session
        // calls reference.
        bool
        start_open_from_path(QString const& archive_path,
                             QString const& archive_type_hint = QString(),
                             std::shared_ptr<std::optional<z7::app::OpenArchiveSessionResult>> out_session_result = {});

        // Open a nested archive at `entry_index` within the parent session. Tries
        // stream / memory / temp-file strategies in order.
        bool
        start_open_nested(z7::app::ArchiveSessionToken parent,
                          uint32_t entry_index,
                          QString const& archive_type_hint = QString(),
                          size_t size_budget = 0,
                          QString const& display_path_hint = QString(),
                          std::shared_ptr<std::optional<z7::app::OpenArchiveSessionResult>> out_session_result = {});

        // Path-based variant: resolves `entry_path` to an index in the parent
        // archive on the backend side. Prefer this from UI code, which already
        // works in path strings.
        bool start_open_nested_by_path(
            z7::app::ArchiveSessionToken parent,
            QString const& entry_path,
            QString const& archive_type_hint = QString(),
            size_t size_budget = 0,
            QString const& display_path_hint = QString(),
            std::shared_ptr<std::optional<z7::app::OpenArchiveSessionResult>> out_session_result = {});

        // Release a session previously obtained via start_open_from_path /
        // start_open_nested.
        bool start_close_session(z7::app::ArchiveSessionToken token);

        // List entries inside an already-opened session (no re-parse).
        bool start_list_in_session(z7::app::ArchiveSessionToken token,
                                   QString const& virtual_dir = QString(),
                                   bool recursive_dirs = false,
                                   bool include_detailed_props = true,
                                   std::shared_ptr<std::optional<z7::app::ListResult>> out_list_result = {});

        // Extract entries via an already-opened session (no re-parse).
        bool start_extract_in_session(z7::app::ArchiveSessionToken token,
                                      QString const& output_dir,
                                      OverwriteMode overwrite_mode,
                                      QStringList const& archive_entries = {},
                                      z7::app::ExtractPathMode path_mode = z7::app::ExtractPathMode::kFullPaths,
                                      bool eliminate_root_duplication = false,
                                      QString const& password = QString(),
                                      bool restore_file_security = false);

        bool start_test(QString const& archive_path);

        bool start_test_many(QStringList const& archive_paths);

        bool start_test_entries(QString const& archive_path, QStringList const& archive_entries);
        bool start_test_in_session(z7::app::ArchiveSessionToken token, QStringList const& archive_entries = {});

        bool start_benchmark(uint32_t iterations = 0,
                             QString const& thread_count = QString(),
                             QString const& dictionary_size = QString(),
                             bool total_mode = false);

        bool start_split(QString const& source_file_path, QString const& output_dir, QString const& volume_size_spec);

        bool start_combine(QString const& source_part_path, QString const& output_dir);

        bool start_hash(QStringList const& input_paths, QString const& hash_method, bool recursive_dirs = true);

        bool start_delete_entries(QString const& archive_path,
                                  QStringList const& archive_entries,
                                  z7::app::ArchiveSessionToken session_token = {});
        bool start_copy_paths(QStringList const& source_paths,
                              QString const& destination_dir,
                              OverwriteMode overwrite_mode = OverwriteMode::kOverwrite,
                              QString const& destination_path = QString());

        bool start_move_paths(QStringList const& source_paths,
                              QString const& destination_dir,
                              OverwriteMode overwrite_mode = OverwriteMode::kOverwrite,
                              QString const& destination_path = QString());

        bool start_delete_paths(QStringList const& paths, bool to_recycle_bin = true);
        bool start_rename_path(QString const& source_path, QString const& new_name);
        bool start_rename_archive_entry(QString const& archive_path,
                                        z7::app::ArchiveSessionToken session_token,
                                        QString const& archive_entry,
                                        QString const& new_name,
                                        bool entry_is_dir);
        bool start_create_directory(QString const& parent_dir, QString const& name);
        bool start_create_file(QString const& parent_dir, QString const& name);
        bool start_archive_comment(QString const& archive_path,
                                   QString const& entry_path,
                                   z7::app::ArchiveSessionToken session_token,
                                   QString const& comment);
        bool start_archive_properties(QString const& archive_path,
                                      QStringList const& archive_entries,
                                      QString const& directory,
                                      bool flat_view,
                                      QString const& archive_type_hint,
                                      z7::app::ArchiveSessionToken session_token = {});
        bool start_filesystem_comment(QString const& directory_path, QString const& item_name, QString const& comment);

        bool is_running() const;
        bool supports_pause() const;
        z7::app::BackendCapabilities backend_capabilities() const;
        static z7::app::BackendCapabilities query_backend_capabilities();
        z7::app::OperationResult const& last_result() const;
        z7::app::OperationOutcome const& last_outcome() const;
        QString last_operation() const;
        void set_overwrite_prompt_handler(OverwritePromptHandler handler);
        void set_prompt_parent_provider(PromptParentProvider provider);
        void on_task_finished(z7::app::OperationOutcome const& outcome);

    public slots:
        void cancel();
        void pause();
        void resume();

    signals:
        void started(QString const& backend, QString const& operation, QStringList const& targets);
        void log_line(QString const& line);
        void stage_changed(QString const& stage_text);
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
                                       QString const& current_path);
        void failure_message(QString const& message);
        // Always emitted asynchronously after runner state has been fully cleaned up.
        void finished(bool ok, int exit_code, int error_domain, QString const& summary);

    private:
        bool start_operation(QString const& operation,
                             QStringList const& targets,
                             z7::app::ArchiveRequest request,
                             std::shared_ptr<std::optional<z7::app::ListResult>> out_list_result = {},
                             std::shared_ptr<std::optional<z7::app::OpenArchiveSessionResult>> out_session_result = {});
        bool start_active_request_attempt();
        void finalize_outcome(z7::app::OperationOutcome const& outcome);
        bool finish_immediately(z7::app::OperationResult const& result);

        z7::app::ArchiveEngine engine_;
        z7::app::ArchiveSession active_task_;
        bool running_ = false;
        bool cancel_requested_ = false;
        z7::app::OperationResult last_result_;
        z7::app::OperationOutcome last_outcome_;
        QString last_operation_;
        QStringList active_targets_;
        OverwritePromptHandler overwrite_prompt_handler_;
        PromptParentProvider prompt_parent_provider_;
        std::shared_ptr<z7::app::IArchiveDelegate> active_delegate_;
        std::shared_ptr<std::optional<z7::app::ListResult>> pending_list_result_;
        std::shared_ptr<std::optional<z7::app::OpenArchiveSessionResult>> pending_session_result_;
        std::optional<z7::app::ArchiveRequest> active_request_;
        std::optional<std::string> retry_next_password_;
        bool password_prompt_canceled_ = false;
    };

} // namespace z7::ui::filemanager
