// src/ui/filemanager/src/main_window/archive/archive_open_actions.cpp
// Role: Archive temp-dir/session helpers and drag materialization actions.

#include "main_window/deps.h"
#include "main_window/internal.h"

#include <memory>
#include <optional>

#include <QEventLoop>

#include "archive_delegate_qt.h"
#include "extract_memory_settings.h"
#include "large_pages_settings.h"

namespace z7::ui::filemanager {
namespace {

QString archive_drag_extract_error_message(
    const z7::app::OperationOutcome& outcome,
    const std::optional<z7::app::ExtractResult>& result,
    const QString& fallback_message) {
  QString error_message;
  if (result.has_value()) {
    error_message = z7::ui::archive_support::from_utf8_string(result->summary);
  }
  if (error_message.trimmed().isEmpty()) {
    error_message = z7::ui::archive_support::from_utf8_string(outcome.error.message);
  }
  if (error_message.trimmed().isEmpty()) {
    error_message = fallback_message;
  }
  return error_message.trimmed();
}

z7::app::OperationOutcome run_archive_drag_extract_request_blocking(
    MainWindow* owner,
    z7::app::ArchiveRequest request) {
  QEventLoop wait_loop;
  z7::app::OperationOutcome outcome = z7::app::make_backend_unavailable_outcome();
  bool finished = false;

  auto delegate = std::make_shared<z7::ui::archive_support::OutcomeRelayDelegate>(
      owner,
      [&outcome, &finished, &wait_loop](const z7::app::OperationOutcome& value) {
        outcome = value;
        finished = true;
        if (wait_loop.isRunning()) {
          wait_loop.quit();
        }
      },
      nullptr,
      z7::ui::archive_support::MissingTargetPolicy::kInvokeDirect);

  z7::app::ArchiveEngine engine;
  z7::ui::runtime_support::apply_configured_large_pages_mode();
  const z7::app::ArchiveSession session = engine.start(std::move(request), delegate);
  if (!session.valid()) {
    return outcome;
  }

  if (!finished) {
    wait_loop.exec();
  }
  return outcome;
}

}  // namespace

QSharedPointer<QTemporaryDir> MainWindow::create_temporary_directory_with_prefix(
    const QString& prefix,
    const QString& failure_caption) {
  const QString normalized_root = QDir::cleanPath(QDir::tempPath());
  if (normalized_root.trimmed().isEmpty()) {
    QMessageBox::warning(this,
                         failure_caption,
                         QStringLiteral("Failed to create temporary directory."));
    return {};
  }

  QDir root_dir(normalized_root);
  if (!root_dir.exists() && !root_dir.mkpath(QStringLiteral("."))) {
    QMessageBox::warning(this,
                         failure_caption,
                         QStringLiteral("Failed to create temporary directory."));
    return {};
  }

  const QString pattern = root_dir.filePath(prefix);
  const QSharedPointer<QTemporaryDir> temp_dir(new QTemporaryDir(pattern));
  if (temp_dir == nullptr || !temp_dir->isValid()) {
    QMessageBox::warning(this,
                         failure_caption,
                         QStringLiteral("Failed to create temporary directory."));
    return {};
  }
  return temp_dir;
}

QSharedPointer<QTemporaryDir> MainWindow::create_archive_open_temporary_directory(
    const QString& failure_caption) {
  return create_temporary_directory_with_prefix(
      QStringLiteral("7zO_XXXXXX"),
      failure_caption);
}

QSharedPointer<QTemporaryDir> MainWindow::create_archive_drag_temporary_directory(
    const QString& failure_caption) {
  return create_temporary_directory_with_prefix(
      QStringLiteral("7zE_XXXXXX"),
      failure_caption);
}

void MainWindow::materialize_archive_drag_entries_for_panel(
    int panel_index,
    const QStringList& entries,
    const std::function<void(const QStringList&, const QString&)>& finished_cb) {
  const auto finish = [finished_cb](const QStringList& paths,
                                    const QString& error_message) {
    if (finished_cb) {
      finished_cb(paths, error_message);
    }
  };

  QStringList normalized_entries;
  normalized_entries.reserve(entries.size());
  for (const QString& entry : entries) {
    const QString normalized = z7::ui::archive_support::normalize_virtual_dir(entry);
    if (!normalized.isEmpty()) {
      normalized_entries << normalized;
    }
  }
  normalized_entries.removeDuplicates();
  if (normalized_entries.isEmpty()) {
    finish({}, QString());
    return;
  }

  const PanelController& panel = panel_controller(panel_index);
  if (!in_archive_view_for_panel(panel_index) ||
      panel.archive.source_archive.trimmed().isEmpty()) {
    finish({}, QStringLiteral("Archive drag materialization context is unavailable."));
    return;
  }

  const QSharedPointer<QTemporaryDir> temp_dir =
      create_archive_drag_temporary_directory(z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(540)));
  if (temp_dir == nullptr || !temp_dir->isValid()) {
    finish({}, QStringLiteral("Failed to allocate temporary directory for drag-out."));
    return;
  }

  z7::app::ExtractRequest request;
  if (panel.archive.current_token.is_valid()) {
    request.session_token = panel.archive.current_token;
  } else {
    request.archive_path = z7::ui::archive_support::to_native_string(panel.archive.source_archive);
  }
  request.output_dir = z7::ui::archive_support::to_native_string(temp_dir->path());
  request.archive_type_hint = z7::ui::archive_support::to_utf8_string(panel.archive.type_hint.trimmed());
  request.overwrite_mode = z7::app::OverwriteMode::kOverwrite;
  request.path_mode = z7::app::ExtractPathMode::kFullPaths;
  request.entries.reserve(normalized_entries.size());
  for (const QString& entry : normalized_entries) {
    request.entries.push_back(z7::ui::archive_support::to_utf8_string(entry));
  }
  const uint64_t configured_limit =
      z7::ui::runtime_support::configured_extract_memory_limit_bytes();
  if (configured_limit != 0) {
    request.configured_memory_limit_bytes = configured_limit;
    request.configured_memory_limit_defined = true;
  }

  struct DragMaterializationTask final
      : public std::enable_shared_from_this<DragMaterializationTask> {
    QPointer<MainWindow> owner;
    QSharedPointer<QTemporaryDir> temp_dir;
    QStringList normalized_entries;
    QString archive_path;
    QString archive_type_hint;
    std::function<void(const QStringList&, const QString&)> finished;
    z7::app::ArchiveEngine engine;
    z7::app::ArchiveSession session;
    std::shared_ptr<z7::app::IArchiveDelegate> delegate;

    void begin(z7::app::ArchiveRequest&& request) {
      auto self = shared_from_this();
      delegate = std::make_shared<z7::ui::archive_support::OutcomeRelayDelegate>(
          owner.data(),
          [self](const z7::app::OperationOutcome& outcome) {
            self->on_finished(outcome);
          },
          nullptr,
          z7::ui::archive_support::MissingTargetPolicy::kInvokeDirect);
      z7::ui::runtime_support::apply_configured_large_pages_mode();
      session = engine.start(std::move(request), delegate);
      if (!session.valid()) {
        const z7::app::OperationOutcome unavailable = z7::app::make_backend_unavailable_outcome();
        const QString error = z7::ui::archive_support::from_utf8_string(unavailable.summary);
        complete({}, error.trimmed().isEmpty()
                         ? z7::ui::archive_support::from_utf8_string(unavailable.error.message)
                         : error);
      }
    }

    void on_finished(const z7::app::OperationOutcome& outcome) {
      const auto result = z7::app::outcome_payload_as<z7::app::ExtractResult>(outcome);
      if (!result.has_value() || !result->ok) {
        QString error_message;
        if (result.has_value()) {
          error_message = z7::ui::archive_support::from_utf8_string(result->summary);
        }
        if (error_message.trimmed().isEmpty()) {
          error_message = z7::ui::archive_support::from_utf8_string(outcome.error.message);
        }
        if (error_message.trimmed().isEmpty()) {
          error_message = QStringLiteral("Failed to materialize archive drag entries.");
        }
        complete({}, error_message);
        return;
      }

      QStringList extracted_paths;
      extracted_paths.reserve(normalized_entries.size());
      for (const QString& entry : normalized_entries) {
        const QString rel_path = QDir::fromNativeSeparators(entry);
        const QFileInfo extracted_info(QDir(temp_dir->path()).filePath(rel_path));
        if (!extracted_info.exists()) {
          continue;
        }
        extracted_paths << extracted_info.absoluteFilePath();
      }
      extracted_paths.removeDuplicates();
      if (extracted_paths.isEmpty()) {
        complete({}, QStringLiteral("No extracted paths produced for archive drag entries."));
        return;
      }

      if (!owner.isNull()) {
        QSharedPointer<MainWindow::ArchiveTempSession> drag_session(
            new MainWindow::ArchiveTempSession);
        drag_session->purpose = MainWindow::ArchiveTempSessionPurpose::kDragOut;
        drag_session->temp_dir = temp_dir;
        drag_session->archive_path = archive_path;
        drag_session->archive_type_hint = archive_type_hint;
        drag_session->command_caption = z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(540));
        drag_session->extracted_paths = extracted_paths;
        owner->retain_archive_temp_session(drag_session);
      }
      complete(extracted_paths, QString());
    }

    void complete(const QStringList& paths, const QString& error_message) {
      session = z7::app::ArchiveSession{};
      delegate.reset();
      if (finished) {
        finished(paths, error_message);
      }
    }
  };

  auto task = std::make_shared<DragMaterializationTask>();
  task->owner = this;
  task->temp_dir = temp_dir;
  task->normalized_entries = normalized_entries;
  task->archive_path = panel.archive.source_archive;
  task->archive_type_hint = panel.archive.type_hint.trimmed();
  task->finished = finish;
  task->begin(z7::app::ArchiveRequest{std::move(request)});
}

bool MainWindow::export_archive_drag_entry_to_destination_for_panel(
    int panel_index,
    const QString& archive_entry,
    bool entry_is_dir,
    const QString& destination_path,
    QString* error) {
  struct ExportPrompt {
    int panel_index = -1;
    QString archive_entry;
    bool entry_is_dir = false;
    QString destination_path;
  };
  struct ExportResult {
    bool ok = false;
    QString error;
  };

  const ExportPrompt prompt{
      panel_index,
      z7::ui::archive_support::normalize_virtual_dir(archive_entry),
      entry_is_dir,
      QDir::cleanPath(QDir::fromNativeSeparators(destination_path.trimmed()))
  };
  const ExportResult result = z7::ui::archive_support::call_on_target_blocking<ExportResult>(
      this,
      prompt,
      ExportResult{},
      [this](const ExportPrompt& request) {
        ExportResult out;
        const int panel_count =
            static_cast<int>(sizeof(panels_) / sizeof(panels_[0]));
        if (request.panel_index < 0 || request.panel_index >= panel_count) {
          out.error = QStringLiteral("Archive drag direct export panel is invalid.");
          return out;
        }

        if (request.destination_path.trimmed().isEmpty()) {
          out.error = QStringLiteral("Archive drag direct export destination path is empty.");
          return out;
        }
        const QFileInfo destination_info(request.destination_path);
        const QString destination_directory =
            destination_info.absolutePath().trimmed();
        const QString destination_name = destination_info.fileName().trimmed();
        if (destination_directory.isEmpty() || destination_name.isEmpty()) {
          out.error = QStringLiteral("Archive drag direct export destination path is invalid.");
          return out;
        }

        const PanelController& panel = panel_controller(request.panel_index);
        if (!in_archive_view_for_panel(request.panel_index)) {
          out.error = QStringLiteral("Archive drag direct export context is unavailable.");
          return out;
        }

        if (panel.archive.current_token.is_valid()) {
          if (panel.archive.source_archive.trimmed().isEmpty() &&
              panel.archive.virtual_display_source.trimmed().isEmpty()) {
            out.error = QStringLiteral("Archive drag direct export source is unavailable.");
            return out;
          }
        } else if (panel.archive.source_archive.trimmed().isEmpty()) {
          out.error = QStringLiteral("Archive drag direct export source is unavailable.");
          return out;
        }

        const bool is_virtual_root = request.archive_entry.isEmpty();
        if (is_virtual_root && !panel.archive.virtual_dir.trimmed().isEmpty()) {
          out.error = QStringLiteral(
              "Only the top-level archive root can be exported without an entry path.");
          return out;
        }

        z7::app::ExtractRequest extract_request;
        if (panel.archive.current_token.is_valid()) {
          extract_request.session_token = panel.archive.current_token;
        } else {
          extract_request.archive_path =
              z7::ui::archive_support::to_native_string(panel.archive.source_archive);
        }
        extract_request.archive_type_hint =
            z7::ui::archive_support::to_utf8_string(panel.archive.type_hint.trimmed());
        extract_request.overwrite_mode = z7::app::OverwriteMode::kOverwrite;

        if (!is_virtual_root) {
          extract_request.entries.push_back(
              z7::ui::archive_support::to_utf8_string(request.archive_entry));
        }
        extract_request.output_dir =
            z7::ui::archive_support::to_native_string(destination_directory);
        extract_request.path_mode = z7::app::ExtractPathMode::kFullPaths;
        extract_request.eliminate_root_duplication = false;
        z7::app::ExtractPathRemap remap;
        remap.match_kind = z7::app::ExtractPathRemapMatchKind::kRequestRoot;
        remap.destination_path =
            z7::ui::archive_support::to_native_string(request.destination_path);
        extract_request.path_remaps.push_back(std::move(remap));

        const uint64_t configured_limit =
            z7::ui::runtime_support::configured_extract_memory_limit_bytes();
        if (configured_limit != 0) {
          extract_request.configured_memory_limit_bytes = configured_limit;
          extract_request.configured_memory_limit_defined = true;
        }

        const z7::app::OperationOutcome outcome =
            run_archive_drag_extract_request_blocking(
                this, z7::app::ArchiveRequest{std::move(extract_request)});
        const auto result =
            z7::app::outcome_payload_as<z7::app::ExtractResult>(outcome);
        if (!result.has_value() || !result->ok) {
          out.error = archive_drag_extract_error_message(
              outcome,
              result,
              QStringLiteral("Failed to export archive drag entry to destination."));
          return out;
        }
        const bool expected_dir = is_virtual_root || request.entry_is_dir;
        const bool destination_exists = destination_info.exists();
        if (!destination_exists ||
            (expected_dir && !destination_info.isDir()) ||
            (!expected_dir && destination_info.isDir())) {
          out.error = QStringLiteral("Archive drag direct export did not create the expected destination: %1")
                          .arg(QDir::toNativeSeparators(request.destination_path));
          return out;
        }

        out.ok = true;
        return out;
      });

  if (!result.ok && error != nullptr) {
    *error = result.error;
  }
  return result.ok;
}

}  // namespace z7::ui::filemanager
