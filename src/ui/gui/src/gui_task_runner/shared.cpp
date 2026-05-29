// src/ui/gui/src/gui_task_runner/shared.cpp
// Role: Shared private helpers for GuiTaskRunner blocking and async flows.

#include "shared.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

#include <optional>
#include <sstream>
#include <string>
#include <utility>

#include "../gui_task_runner_helpers.h"
#include "archive_delegate_qt.h"
#include "archive_error.h"
#include "archive_format.h"
#include "archive_string_codec_qt.h"
#include "common/archive_type_normalization.h"
#include "extract_memory_settings.h"
#include "official_lang_catalog.h"
#include "platform_support.h"

namespace z7::ui::gui::gui_task_runner_shared {
namespace {

std::optional<QString> original_style_empty_archive_open_message() {
  QString text = z7::ui::runtime_support::LF(3005, {QString()}).trimmed();
  text.replace(QStringLiteral(" ''"), QString());
  text.replace(QStringLiteral("''"), QString());
  return text;
}

std::optional<QString> localized_failure_reason(QString reason) {
  reason = reason.trimmed();
  if (reason.isEmpty()) {
    return QString();
  }

  if (reason.compare(QStringLiteral("Archive format is unsupported"),
                     Qt::CaseInsensitive) == 0) {
    return original_style_empty_archive_open_message();
  }
  if (reason.compare(QStringLiteral("Password required or incorrect"),
                     Qt::CaseInsensitive) == 0) {
    QString text = z7::ui::runtime_support::LF(3006, {QString()}).trimmed();
    text.replace(QStringLiteral(" ''"), QString());
    text.replace(QStringLiteral("''"), QString());
    return text;
  }
  if (reason.compare(QStringLiteral("Unsupported method"),
                     Qt::CaseInsensitive) == 0 ||
      reason.compare(QStringLiteral("Unsupported compression method"),
                     Qt::CaseInsensitive) == 0) {
    return z7::ui::runtime_support::L(3721);
  }
  if (reason.compare(QStringLiteral("Data error"), Qt::CaseInsensitive) == 0) {
    return z7::ui::runtime_support::L(3722);
  }
  if (reason.compare(QStringLiteral("CRC failed"), Qt::CaseInsensitive) == 0) {
    return z7::ui::runtime_support::L(3723);
  }
  if (reason.compare(QStringLiteral("Unavailable data"), Qt::CaseInsensitive) == 0) {
    return z7::ui::runtime_support::L(3724);
  }
  if (reason.compare(QStringLiteral("Unexpected end of data"),
                     Qt::CaseInsensitive) == 0) {
    return z7::ui::runtime_support::L(3725);
  }
  if (reason.compare(QStringLiteral("There are some data after the end of payload data"),
                     Qt::CaseInsensitive) == 0) {
    return z7::ui::runtime_support::L(3726);
  }
  if (reason.compare(QStringLiteral("Is not archive"), Qt::CaseInsensitive) == 0) {
    return z7::ui::runtime_support::L(3727);
  }
  if (reason.compare(QStringLiteral("Headers error"), Qt::CaseInsensitive) == 0 ||
      reason.compare(QStringLiteral("Headers Error"), Qt::CaseInsensitive) == 0) {
    return z7::ui::runtime_support::L(3728);
  }
  if (reason.compare(QStringLiteral("Wrong password"), Qt::CaseInsensitive) == 0) {
    return z7::ui::runtime_support::L(3729);
  }
  if (reason.compare(QStringLiteral("Operation canceled"), Qt::CaseInsensitive) == 0) {
    return z7::ui::runtime_support::L(402);
  }
  return std::nullopt;
}

QString localize_failure_line(QString line) {
  const QString trimmed = line.trimmed();
  if (trimmed.isEmpty()) {
    return QString();
  }

  if (std::optional<QString> direct = localized_failure_reason(trimmed);
      direct.has_value()) {
    return *direct;
  }

  constexpr QLatin1StringView kPathReasonSeparator(" : ");
  const qsizetype separator = trimmed.lastIndexOf(kPathReasonSeparator);
  if (separator > 0) {
    const QString path = trimmed.left(separator).trimmed();
    const QString reason =
        trimmed.mid(separator + kPathReasonSeparator.size()).trimmed();
    if (!path.isEmpty()) {
      if (std::optional<QString> localized = localized_failure_reason(reason);
          localized.has_value()) {
        return path + QLatin1Char('\n') + *localized;
      }
    }
  }

  return trimmed;
}

QString localize_failure_message_impl(QString message) {
  QStringList lines = message.split(QLatin1Char('\n'));
  for (QString& line : lines) {
    line = localize_failure_line(line);
  }
  return lines.join(QLatin1Char('\n'));
}

std::optional<z7::ui::runtime_support::TaskProgressRatioInfo> to_task_progress_ratio_info(
    const std::optional<z7::app::ProgressRatioInfo>& ratio_info) {
  if (!ratio_info.has_value()) {
    return std::nullopt;
  }
  z7::ui::runtime_support::TaskProgressRatioInfo out;
  out.input_size_known = ratio_info->input_size_known;
  out.input_size = static_cast<quint64>(ratio_info->input_size);
  out.output_size_known = ratio_info->output_size_known;
  out.output_size = static_cast<quint64>(ratio_info->output_size);
  out.compressing_mode = ratio_info->compressing_mode;
  return out;
}

void apply_log_to_dialog(z7::ui::runtime_support::TaskProgressDialogBase* dialog,
                         GuiTaskRunResult* out,
                         const z7::app::ArchiveLog& log) {
  if (dialog == nullptr) {
    return;
  }
  const QString stage_text =
      z7::ui::archive_support::stage_label_for(log.stage);
  const QString log_line =
      z7::ui::archive_support::from_local8_string(log.message);
  if (!stage_text.isEmpty()) {
    dialog->set_stage(stage_text);
  }
  if (out != nullptr && !log_line.isEmpty()) {
    const QString display_log_line =
        log.channel == z7::app::OutputChannel::kStdErr
            ? localize_failure_message_impl(log_line)
            : log_line;
    out->log_lines.push_back(display_log_line);
    if (log.channel == z7::app::OutputChannel::kStdErr) {
      out->failure_messages.push_back(display_log_line);
      dialog->append_failure_result_message(display_log_line);
    }
    dialog->append_log(display_log_line);
  }
}

void apply_progress_to_dialog(z7::ui::runtime_support::TaskProgressDialogBase* dialog,
                              GuiTaskRunResult* out,
                              const z7::app::ProgressSnapshot& progress) {
  if (dialog == nullptr) {
    return;
  }
  const QString stage_text =
      z7::ui::archive_support::stage_label_for(progress.stage);
  const QString log_line =
      z7::ui::archive_support::from_local8_string(progress.message);
  const QString current_path =
      z7::ui::archive_support::from_local8_string(progress.current_path);
  if (!stage_text.isEmpty()) {
    dialog->set_stage(stage_text);
  }
  if (out != nullptr && !log_line.isEmpty()) {
    out->log_lines.push_back(log_line);
    dialog->append_log(log_line);
  }
  dialog->set_percent(progress.percent);
  dialog->set_detailed_progress(progress.totals_known,
                                static_cast<quint64>(progress.total_bytes),
                                static_cast<quint64>(progress.completed_bytes),
                                static_cast<quint64>(progress.total_files),
                                static_cast<quint64>(progress.completed_files),
                                static_cast<quint64>(progress.error_count),
                                to_task_progress_ratio_info(progress.ratio_info),
                                current_path);
}

class ProgressDialogArchiveDelegate final
    : public z7::ui::archive_support::OwnerRelayDelegate<
          z7::ui::runtime_support::TaskProgressDialogBase> {
 public:
  using FinishedCallback = std::function<void(const z7::app::OperationOutcome&)>;

  ProgressDialogArchiveDelegate(
      z7::ui::runtime_support::TaskProgressDialogBase* dialog,
      GuiTaskRunResult* out,
      FinishedCallback on_finished,
      PasswordPromptParentProvider password_prompt_parent_provider)
      : z7::ui::archive_support::OwnerRelayDelegate<
            z7::ui::runtime_support::TaskProgressDialogBase>(
            dialog,
            std::move(on_finished),
            QCoreApplication::instance(),
            z7::ui::archive_support::MissingTargetPolicy::kInvokeDirect),
        out_(out),
        password_prompt_parent_provider_(
            std::move(password_prompt_parent_provider)) {}

  void on_log(const z7::app::ArchiveLog& log) override {
    const z7::app::ArchiveLog log_copy = log;
    post_to_owner([out = out_, log_copy = std::move(log_copy)](
                      z7::ui::runtime_support::TaskProgressDialogBase* dialog) {
      apply_log_to_dialog(dialog, out, log_copy);
    });
  }

  void on_progress(const z7::app::ProgressSnapshot& progress) override {
    z7::app::ProgressSnapshot progress_copy = progress;
    post_to_owner([out = out_, progress_copy = std::move(progress_copy)](
                      z7::ui::runtime_support::TaskProgressDialogBase* dialog) {
      apply_progress_to_dialog(dialog, out, progress_copy);
    });
  }

  std::optional<z7::app::OverwriteDecision> request_overwrite(
      const z7::app::OverwritePrompt& prompt) override {
    z7::ui::runtime_support::TaskProgressDialogBase* dialog = owner();
    return z7::ui::archive_support::call_on_target_blocking<
        z7::app::OverwriteDecision>(
        dialog,
        prompt,
        z7::app::OverwriteDecision::kCancel,
        [dialog](const z7::app::OverwritePrompt& prompt_value) {
          if (dialog == nullptr) {
            return z7::app::OverwriteDecision::kCancel;
          }
          QWidget* prompt_dialog = dialog;
          return show_overwrite_prompt_dialog(prompt_dialog, prompt_value);
        });
  }

  std::optional<z7::app::PasswordReply> request_password(
      const z7::app::PasswordPrompt& prompt) override {
    z7::ui::runtime_support::TaskProgressDialogBase* dialog = owner();
    return z7::ui::archive_support::call_on_target_blocking<
        z7::app::PasswordReply>(
        dialog,
        prompt,
        z7::app::PasswordReply{},
        [this, dialog](const z7::app::PasswordPrompt& prompt_value) {
          QWidget* prompt_dialog = nullptr;
          if (password_prompt_parent_provider_) {
            prompt_dialog = password_prompt_parent_provider_();
          }
          if (prompt_dialog == nullptr) {
            prompt_dialog = dialog;
          }
          if (prompt_dialog == nullptr) {
            return z7::app::PasswordReply{};
          }
          return show_password_prompt_dialog(prompt_dialog, prompt_value);
        });
  }

  std::optional<z7::app::ChoiceReply> request_choice(
      const z7::app::ChoicePrompt& prompt) override {
    z7::ui::runtime_support::TaskProgressDialogBase* dialog = owner();
    return z7::ui::archive_support::call_on_target_blocking<
        z7::app::ChoiceReply>(
        dialog,
        prompt,
        z7::app::ChoiceReply{},
        [dialog](const z7::app::ChoicePrompt& prompt_value) {
          if (dialog == nullptr) {
            return z7::app::ChoiceReply{};
          }
          QWidget* prompt_dialog = dialog;
          return show_choice_prompt_dialog(prompt_dialog, prompt_value);
        });
  }

  std::optional<z7::app::MemoryLimitReply> request_memory_limit(
      const z7::app::MemoryLimitPrompt& prompt) override {
    z7::ui::runtime_support::TaskProgressDialogBase* dialog = owner();
    return z7::ui::archive_support::call_on_target_blocking<
        z7::app::MemoryLimitReply>(
        dialog,
        prompt,
        z7::app::MemoryLimitReply{},
        [dialog](const z7::app::MemoryLimitPrompt& prompt_value) {
          if (dialog == nullptr) {
            return z7::app::MemoryLimitReply{};
          }
          QWidget* prompt_dialog = dialog;
          return show_memory_limit_prompt_dialog(prompt_dialog, prompt_value);
        });
  }

 private:
  GuiTaskRunResult* out_ = nullptr;
  PasswordPromptParentProvider password_prompt_parent_provider_;
};

}  // namespace

QString localize_failure_message(QString message) {
  return localize_failure_message_impl(std::move(message));
}

std::shared_ptr<z7::app::IArchiveDelegate> make_progress_dialog_delegate(
    z7::ui::runtime_support::TaskProgressDialogBase* dialog,
    GuiTaskRunResult* out,
    std::function<void(const z7::app::OperationOutcome&)> on_finished,
    PasswordPromptParentProvider password_prompt_parent_provider) {
  return std::make_shared<ProgressDialogArchiveDelegate>(
      dialog,
      out,
      std::move(on_finished),
      std::move(password_prompt_parent_provider));
}

void prepare_progress_dialog(
    z7::ui::runtime_support::TaskProgressDialogBase* dialog,
    const QString& title,
    bool test_mode) {
  if (dialog == nullptr) {
    return;
  }
  dialog->set_header(title);
  dialog->set_test_mode(test_mode);
  dialog->set_pause_available(true);
  dialog->set_running(true);
}

SessionControlBindings bind_session_controls(
    z7::ui::runtime_support::TaskProgressDialogBase* dialog,
    const z7::app::ArchiveSession& session,
    SharedTaskCancellation cancel_requested) {
  SessionControlBindings bindings;
  if (dialog == nullptr || !session.valid()) {
    return bindings;
  }

  bindings.cancel_connection = QObject::connect(
      dialog,
      &z7::ui::runtime_support::TaskProgressDialogBase::cancel_requested,
      [session]() mutable { session.cancel(); });
  bindings.pause_connection = QObject::connect(
      dialog,
      &z7::ui::runtime_support::TaskProgressDialogBase::pause_requested,
      [session]() mutable { session.pause(); });
  bindings.resume_connection = QObject::connect(
      dialog,
      &z7::ui::runtime_support::TaskProgressDialogBase::resume_requested,
      [session]() mutable { session.resume(); });
  if (cancel_requested) {
    if (cancel_requested->is_canceled()) {
      session.cancel();
    } else {
      bindings.remote_cancel_connection = QObject::connect(
          cancel_requested.data(),
          &TaskCancellation::cancel_requested,
          dialog,
          [session]() mutable { session.cancel(); },
          Qt::QueuedConnection);
    }
  }
  return bindings;
}

void release_session_controls(SessionControlBindings* bindings) {
  if (bindings == nullptr) {
    return;
  }
  if (bindings->remote_cancel_connection) {
    QObject::disconnect(bindings->remote_cancel_connection);
  }
  QObject::disconnect(bindings->cancel_connection);
  QObject::disconnect(bindings->pause_connection);
  QObject::disconnect(bindings->resume_connection);
  *bindings = SessionControlBindings{};
}

QString canceled_export_message() {
  return QStringLiteral("Operation canceled.");
}

bool is_export_canceled(const SharedTaskCancellation& cancel_requested) {
  return cancel_requested && cancel_requested->is_canceled();
}

QString normalize_virtual_entry_path(QString entry) {
  QString out = QDir::fromNativeSeparators(entry.trimmed());
  while (out.startsWith(QLatin1Char('/'))) {
    out.remove(0, 1);
  }
  while (out.endsWith(QLatin1Char('/'))) {
    out.chop(1);
  }
  while (out.contains(QStringLiteral("//"))) {
    out.replace(QStringLiteral("//"), QStringLiteral("/"));
  }
  return out;
}

QString infer_archive_format(const QString& archive_path, const QString& type_hint) {
  const QString trimmed_hint = type_hint.trimmed();
  if (!trimmed_hint.isEmpty()) {
    return QString::fromStdString(
        z7::common::normalize_archive_type_token_copy(
            trimmed_hint.toStdString()));
  }
  const std::string suffix =
      QFileInfo(archive_path).suffix().toLower().toStdString();
  const std::string format =
      z7::common::canonical_archive_type_from_filename_suffix_copy(suffix);
  return format.empty() ? QStringLiteral("7z")
                        : QString::fromStdString(format);
}

QString normalized_mode_token(QString value) {
  value = value.trimmed().toLower();
  while (value.startsWith(QLatin1Char('-'))) {
    value.remove(0, 1);
  }
  value.replace(QLatin1Char('-'), QLatin1Char('_'));
  value.replace(QLatin1Char(' '), QLatin1Char('_'));
  return value;
}

z7::app::OverwriteMode parse_archive_export_overwrite_mode(
    const QString& overwrite_mode) {
  const QString token = normalized_mode_token(overwrite_mode);
  if (token.isEmpty() || token == QStringLiteral("ask") ||
      token == QStringLiteral("ask_before")) {
    return z7::app::OverwriteMode::kAsk;
  }
  if (token == QStringLiteral("overwrite") ||
      token == QStringLiteral("replace") ||
      token == QStringLiteral("aoa") ||
      token == QStringLiteral("-aoa")) {
    return z7::app::OverwriteMode::kOverwrite;
  }
  if (token == QStringLiteral("skip") || token == QStringLiteral("aos") ||
      token == QStringLiteral("-aos")) {
    return z7::app::OverwriteMode::kSkip;
  }
  if (token == QStringLiteral("rename_existing") ||
      token == QStringLiteral("rename_old") ||
      token == QStringLiteral("aot") ||
      token == QStringLiteral("-aot")) {
    return z7::app::OverwriteMode::kRenameExisting;
  }
  if (token == QStringLiteral("rename_extracted") ||
      token == QStringLiteral("rename_new") ||
      token == QStringLiteral("rename") ||
      token == QStringLiteral("aou") ||
      token == QStringLiteral("-aou")) {
    return z7::app::OverwriteMode::kRenameExtracted;
  }
  return z7::app::OverwriteMode::kAsk;
}

z7::app::ExtractPathMode parse_archive_export_path_mode(
    const QString& path_mode) {
  const QString token = normalized_mode_token(path_mode);
  if (token == QStringLiteral("no") ||
      token == QStringLiteral("no_paths") ||
      token == QStringLiteral("nopaths")) {
    return z7::app::ExtractPathMode::kNoPaths;
  }
  if (token == QStringLiteral("absolute") ||
      token == QStringLiteral("abs") ||
      token == QStringLiteral("absolute_paths")) {
    return z7::app::ExtractPathMode::kAbsolutePaths;
  }
  return z7::app::ExtractPathMode::kFullPaths;
}

z7::app::ExtractZoneIdMode parse_extract_zone_id_mode(
    const QString& zone_id_mode) {
  const QString token = normalized_mode_token(zone_id_mode);
  if (token == QStringLiteral("all") ||
      token == QStringLiteral("yes") ||
      token == QStringLiteral("1")) {
    return z7::app::ExtractZoneIdMode::kAll;
  }
  if (token == QStringLiteral("office") ||
      token == QStringLiteral("2")) {
    return z7::app::ExtractZoneIdMode::kOffice;
  }
  return z7::app::ExtractZoneIdMode::kNone;
}

z7::app::ExtractPathRemapMatchKind to_archive_extract_remap_match_kind(
    z7::ui::gui::ExtractPathRemapMatchKind kind) {
  switch (kind) {
    case z7::ui::gui::ExtractPathRemapMatchKind::kExactArchivePath:
      return z7::app::ExtractPathRemapMatchKind::kExactArchivePath;
    case z7::ui::gui::ExtractPathRemapMatchKind::kArchivePrefix:
      return z7::app::ExtractPathRemapMatchKind::kArchivePrefix;
    case z7::ui::gui::ExtractPathRemapMatchKind::kRequestRoot:
    default:
      return z7::app::ExtractPathRemapMatchKind::kRequestRoot;
  }
}

QString outcome_summary(const z7::app::OperationOutcome& outcome) {
  QString summary =
      z7::ui::archive_support::from_utf8_string(outcome.summary).trimmed();
  if (!summary.isEmpty()) {
    return summary;
  }
  return z7::ui::archive_support::from_utf8_string(
             z7::app::describe_archive_error(outcome.error))
      .trimmed();
}

int result_code_for_failure(const z7::app::ArchiveErrorDomain domain) {
  switch (domain) {
    case z7::app::ArchiveErrorDomain::kInvalidArguments:
      return 1;
    case z7::app::ArchiveErrorDomain::kIo:
      return 3;
    case z7::app::ArchiveErrorDomain::kCanceled:
      return 5;
    default:
      return 2;
  }
}

GuiTaskRunResult make_failure_result(int code,
                                     z7::app::ArchiveErrorDomain domain,
                                     const QString& message) {
  GuiTaskRunResult result;
  result.result = z7::app::make_immediate_result(
      code, domain, z7::ui::archive_support::to_utf8_string(message));
  return result;
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

bool build_archive_export_task_options(
    const z7::ui::gui::ArchiveExportTaskSpec& spec,
    ArchiveExportTaskOptions* out,
    QString* error_message) {
  if (error_message != nullptr) {
    error_message->clear();
  }
  if (out == nullptr) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("Archive export task output is null.");
    }
    return false;
  }

  ArchiveExportTaskOptions options;
  const QString archive_path =
      z7::ui::archive_support::from_native_string(spec.root_archive_path)
          .trimmed();
  if (archive_path.isEmpty()) {
    if (error_message != nullptr) {
      *error_message =
          QStringLiteral("Archive export task root archive path is empty.");
    }
    return false;
  }
  const QFileInfo archive_info(archive_path);
  if (!archive_info.exists() || !archive_info.isFile()) {
    if (error_message != nullptr) {
      *error_message =
          QStringLiteral("Archive export task root archive path does not exist.");
    }
    return false;
  }
  options.archive_path = archive_info.absoluteFilePath();
  options.archive_type_hint = QString::fromStdString(
      z7::common::normalize_archive_type_token_copy(spec.root_archive_type));

  QString output_dir =
      z7::ui::archive_support::from_native_string(spec.output_dir).trimmed();
  if (output_dir.isEmpty()) {
    if (error_message != nullptr) {
      *error_message =
          QStringLiteral("Archive export task output directory is empty.");
    }
    return false;
  }
  output_dir = QDir::fromNativeSeparators(output_dir);
  if (QDir::isRelativePath(output_dir)) {
    output_dir = QDir::current().absoluteFilePath(output_dir);
  }
  options.output_dir = QDir::cleanPath(output_dir);

  QStringList entry_paths;
  for (const std::string& value : spec.archive_entry_paths) {
    entry_paths << z7::ui::archive_support::from_native_string(value).trimmed();
  }
  for (const QString& raw_entry : entry_paths) {
    const QString normalized = normalize_virtual_entry_path(raw_entry);
    if (normalized.isEmpty() ||
        QDir::isAbsolutePath(QDir::fromNativeSeparators(raw_entry))) {
      if (error_message != nullptr) {
        *error_message =
            QStringLiteral("Archive export task entry path is invalid.");
      }
      return false;
    }
    options.entry_paths << normalized;
  }
  options.entry_paths.removeDuplicates();
  if (options.entry_paths.isEmpty()) {
    if (error_message != nullptr) {
      *error_message =
          QStringLiteral("Archive export task requires entry paths.");
    }
    return false;
  }

  for (const std::string& value : spec.nested_archive_entries) {
    const QString raw_nested_entry =
        z7::ui::archive_support::from_native_string(value).trimmed();
    const QString normalized = normalize_virtual_entry_path(raw_nested_entry);
    if (normalized.isEmpty() ||
        QDir::isAbsolutePath(QDir::fromNativeSeparators(raw_nested_entry))) {
      if (error_message != nullptr) {
        *error_message =
            QStringLiteral("Archive export task nested archive entry is invalid.");
      }
      return false;
    }
    options.nested_archive_entries << normalized;
  }

  options.overwrite_mode = parse_archive_export_overwrite_mode(
      z7::ui::archive_support::from_native_string(spec.overwrite_mode));
  options.path_mode = parse_archive_export_path_mode(
      z7::ui::archive_support::from_native_string(spec.path_mode));
  options.eliminate_root_duplication = spec.eliminate_root_duplication;
  options.restore_file_security =
      spec.restore_file_security &&
      z7::ui::runtime_support::is_platform_supported(
          z7::ui::runtime_support::PlatformSupport::kWindowsOnly);
  options.zone_id_mode = parse_extract_zone_id_mode(
      z7::ui::archive_support::from_native_string(spec.zone_id_mode));
  options.password =
      z7::ui::archive_support::from_native_string(spec.password).trimmed();

  options.path_remaps.reserve(spec.path_remaps.size());
  for (const z7::ui::gui::ExtractPathRemap& remap : spec.path_remaps) {
    z7::app::ExtractPathRemap out_remap;
    out_remap.match_kind =
        to_archive_extract_remap_match_kind(remap.match_kind);
    const QString source_path =
        z7::ui::archive_support::from_native_string(remap.source_path)
            .trimmed();
    const QString destination_path =
        z7::ui::archive_support::from_native_string(remap.destination_path)
            .trimmed();
    if (destination_path.isEmpty()) {
      if (error_message != nullptr) {
        *error_message =
            QStringLiteral("Archive export task path remap destination is empty.");
      }
      return false;
    }
    out_remap.source_path =
        z7::ui::archive_support::to_utf8_string(
            normalize_virtual_entry_path(source_path));
    QString normalized_destination =
        QDir::fromNativeSeparators(destination_path);
    if (!normalized_destination.isEmpty() &&
        QDir::isRelativePath(normalized_destination)) {
      normalized_destination =
          QDir(options.output_dir).absoluteFilePath(normalized_destination);
    }
    out_remap.destination_path =
        z7::ui::archive_support::to_native_string(
            QDir::cleanPath(normalized_destination));
    options.path_remaps.push_back(std::move(out_remap));
  }

  *out = std::move(options);
  return true;
}

bool build_archive_hash_task_options(
    const z7::ui::gui::ArchiveHashTaskSpec& spec,
    ArchiveHashTaskOptions* out,
    QString* error_message) {
  if (error_message != nullptr) {
    error_message->clear();
  }
  if (out == nullptr) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("Archive hash task output is null.");
    }
    return false;
  }

  ArchiveHashTaskOptions options;
  const QString archive_path =
      z7::ui::archive_support::from_native_string(spec.archive_path).trimmed();
  if (archive_path.isEmpty()) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("Archive hash task archive path is empty.");
    }
    return false;
  }

  const QFileInfo archive_info(archive_path);
  if (!archive_info.exists() || !archive_info.isFile()) {
    if (error_message != nullptr) {
      *error_message =
          QStringLiteral("Archive hash task archive path does not exist.");
    }
    return false;
  }

  options.archive_path = archive_info.absoluteFilePath();
  options.archive_type_hint = QString::fromStdString(
      z7::common::normalize_archive_type_token_copy(spec.archive_type));
  options.hash_method =
      z7::ui::archive_support::from_native_string(spec.hash_method).trimmed();
  if (options.hash_method.isEmpty()) {
    options.hash_method = QStringLiteral("CRC32");
  }

  QStringList entry_paths;
  for (const std::string& value : spec.archive_entry_paths) {
    entry_paths << z7::ui::archive_support::from_native_string(value).trimmed();
  }
  for (const QString& raw_entry : entry_paths) {
    const QString normalized = normalize_virtual_entry_path(raw_entry);
    if (normalized.isEmpty() ||
        QDir::isAbsolutePath(QDir::fromNativeSeparators(raw_entry))) {
      if (error_message != nullptr) {
        *error_message =
            QStringLiteral("Archive hash task entry path is invalid.");
      }
      return false;
    }
    options.entry_paths << normalized;
  }
  options.entry_paths.removeDuplicates();
  if (options.entry_paths.isEmpty()) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("Archive hash task requires entry paths.");
    }
    return false;
  }

  for (const std::string& value : spec.nested_archive_entries) {
    const QString raw_nested_entry =
        z7::ui::archive_support::from_native_string(value).trimmed();
    const QString normalized = normalize_virtual_entry_path(raw_nested_entry);
    if (normalized.isEmpty() ||
        QDir::isAbsolutePath(QDir::fromNativeSeparators(raw_nested_entry))) {
      if (error_message != nullptr) {
        *error_message =
            QStringLiteral("Archive hash task nested archive entry is invalid.");
      }
      return false;
    }
    options.nested_archive_entries << normalized;
  }

  *out = std::move(options);
  return true;
}

bool build_archive_test_task_options(
    const z7::ui::gui::ArchiveTestTaskSpec& spec,
    ArchiveTestTaskOptions* out,
    QString* error_message) {
  if (error_message != nullptr) {
    error_message->clear();
  }
  if (out == nullptr) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("Archive test task output is null.");
    }
    return false;
  }

  ArchiveTestTaskOptions options;
  const QString archive_path =
      z7::ui::archive_support::from_native_string(spec.archive_path).trimmed();
  if (archive_path.isEmpty()) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("Archive test task archive path is empty.");
    }
    return false;
  }
  const QFileInfo archive_info(archive_path);
  if (!archive_info.exists() || !archive_info.isFile()) {
    if (error_message != nullptr) {
      *error_message =
          QStringLiteral("Archive test task archive path does not exist.");
    }
    return false;
  }

  options.archive_path = archive_info.absoluteFilePath();
  options.archive_type_hint = QString::fromStdString(
      z7::common::normalize_archive_type_token_copy(spec.archive_type));
  QStringList entry_paths;
  for (const std::string& value : spec.archive_entry_paths) {
    entry_paths << z7::ui::archive_support::from_native_string(value).trimmed();
  }
  for (const QString& raw_entry : entry_paths) {
    const QString normalized = normalize_virtual_entry_path(raw_entry);
    if (normalized.isEmpty() ||
        QDir::isAbsolutePath(QDir::fromNativeSeparators(raw_entry))) {
      if (error_message != nullptr) {
        *error_message =
            QStringLiteral("Archive test task entry path is invalid.");
      }
      return false;
    }
    options.entry_paths << normalized;
  }
  options.entry_paths.removeDuplicates();

  for (const std::string& value : spec.nested_archive_entries) {
    const QString raw_nested_entry =
        z7::ui::archive_support::from_native_string(value).trimmed();
    const QString normalized = normalize_virtual_entry_path(raw_nested_entry);
    if (normalized.isEmpty() ||
        QDir::isAbsolutePath(QDir::fromNativeSeparators(raw_nested_entry))) {
      if (error_message != nullptr) {
        *error_message =
            QStringLiteral("Archive test task nested archive entry is invalid.");
      }
      return false;
    }
    options.nested_archive_entries << normalized;
  }

  *out = std::move(options);
  return true;
}

}  // namespace z7::ui::gui::gui_task_runner_shared
