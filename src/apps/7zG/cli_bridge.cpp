#include "cli_bridge.h"

#include <algorithm>
#include <cstdio>
#include <memory>
#include <optional>
#include <thread>
#include <vector>

#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QDialog>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QMessageBox>
#include <QProcess>
#include <QThread>
#include <QUuid>

#include "Common/MyException.h"
#include "Common/StdOutStream.h"
#include "Windows/ErrorMsg.h"
#include "7zip/UI/Common/ArchiveCommandLine.h"
#include "7zip/UI/Common/ExitCode.h"
#include "gui_app_controller.h"

#if defined(Q_OS_WIN)
#include <fcntl.h>
#include <io.h>
#define Z7_DUP _dup
#define Z7_DUP2 _dup2
#define Z7_FILENO _fileno
#define Z7_PIPE(fds) _pipe((fds), 4096, _O_BINARY)
#define Z7_READ _read
#define Z7_CLOSE _close
#else
#include <unistd.h>
#define Z7_DUP dup
#define Z7_DUP2 dup2
#define Z7_FILENO fileno
#define Z7_PIPE(fds) pipe(fds)
#define Z7_READ read
#define Z7_CLOSE close
#endif

extern CStdOutStream* g_StdStream;
extern CStdOutStream* g_ErrStream;
extern int Main2(int numArgs, char* args[]);

namespace z7::apps::gui {
namespace {

QString from_ustring(const UString& value) {
  return QString::fromWCharArray(value.Ptr(), static_cast<int>(value.Len()));
}

QString from_astring(const AString& value) {
  return QString::fromLocal8Bit(value.Ptr(), static_cast<int>(value.Len()));
}

UString to_ustring(const QString& value) {
  const std::wstring wide = value.toStdWString();
  return UString(wide.c_str());
}

UStringVector to_command_strings(const QStringList& argv) {
  UStringVector out;
  for (const QString& arg : argv) {
    out.Add(to_ustring(arg));
  }
  return out;
}

std::string to_native_string(const QString& value) {
  const QByteArray encoded = QFile::encodeName(value);
  return std::string(encoded.constData(), static_cast<size_t>(encoded.size()));
}

std::string to_native_string(const UString& value) {
  return to_native_string(from_ustring(value));
}

QString from_fstring(const FString& value) {
  return from_ustring(fs2us(value));
}

std::string to_extract_path_mode(NExtract::NPathMode::EEnum mode) {
  switch (mode) {
    case NExtract::NPathMode::kNoPaths:
    case NExtract::NPathMode::kNoPathsAlt:
      return "no";
    case NExtract::NPathMode::kAbsPaths:
      return "absolute";
    case NExtract::NPathMode::kCurPaths:
    case NExtract::NPathMode::kFullPaths:
    default:
      return "full";
  }
}

std::string to_add_path_mode(NWildcard::ECensorPathMode mode) {
  switch (mode) {
    case NWildcard::k_AbsPath:
      return "absolute";
    case NWildcard::k_FullPath:
      return "full";
    case NWildcard::k_RelatPath:
    default:
      return "relative";
  }
}

std::string to_overwrite_switch(NExtract::NOverwriteMode::EEnum mode) {
  switch (mode) {
    case NExtract::NOverwriteMode::kOverwrite:
      return "-aoa";
    case NExtract::NOverwriteMode::kSkip:
      return "-aos";
    case NExtract::NOverwriteMode::kRename:
      return "-aou";
    case NExtract::NOverwriteMode::kRenameExisting:
      return "-aot";
    case NExtract::NOverwriteMode::kAsk:
    default:
      return {};
  }
}

std::string to_zone_id_mode(NExtract::NZoneIdMode::EEnum mode) {
  switch (mode) {
    case NExtract::NZoneIdMode::kAll:
      return "all";
    case NExtract::NZoneIdMode::kOffice:
      return "office";
    case NExtract::NZoneIdMode::kNone:
    default:
      return "none";
  }
}

QString unsupported_email_message() {
  return QStringLiteral(
      "Unsupported command-line mode: 7zG direct CLI does not support email "
      "archive sending. Use file-based archive commands.");
}

QString unsupported_show_dialog_message() {
  return QStringLiteral(
      "Unsupported command-line mode: -ad show-dialog direct CLI currently "
      "supports only add/update and extract commands with explicit file paths.");
}

QString original_gui_error_dialog_title() {
  return QStringLiteral("7-Zip");
}

Qt::WindowFlags original_gui_error_dialog_flags() {
  return Qt::Dialog | Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint |
         Qt::WindowSystemMenuHint | Qt::WindowCloseButtonHint;
}

QString unsupported_gui_command_message() {
  return QStringLiteral("Unsupported command");
}

QString validation_exception_message(const QString& header,
                                     const QString& detail);

#ifdef Z7_TESTING
constexpr const char* kSuppressGuiErrorDialogsForTestsEnv =
    "Z7_SUPPRESS_GUI_RESULT_DIALOGS_FOR_TESTS";
#endif

bool parse_cli_options(const QStringList& argv,
                       CArcCmdLineOptions* options,
                       QString* error_message) {
  if (error_message != nullptr) {
    error_message->clear();
  }
  if (options == nullptr) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("Missing CLI parser output.");
    }
    return false;
  }
  try {
    CArcCmdLineParser parser;
    UStringVector command_strings = to_command_strings(argv);
    parser.Parse1(command_strings, *options);
    parser.Parse2(*options);
    return true;
  } catch (const CNewException&) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("ERROR: Can't allocate required memory!");
    }
  } catch (const CMessagePathException& error) {
    if (error_message != nullptr) {
      *error_message = from_ustring(error).trimmed();
    }
  } catch (const CSystemException& error) {
    if (error_message != nullptr) {
      *error_message =
          validation_exception_message(
              QStringLiteral("System ERROR:"),
              from_ustring(NWindows::NError::MyFormatMessage(error.ErrorCode)));
    }
  } catch (const UString& error) {
    if (error_message != nullptr) {
      *error_message =
          validation_exception_message(QStringLiteral("ERROR:"), from_ustring(error));
    }
  } catch (const AString& error) {
    if (error_message != nullptr) {
      *error_message =
          validation_exception_message(QStringLiteral("ERROR:"), from_astring(error));
    }
  } catch (const char* error) {
    if (error_message != nullptr) {
      *error_message = validation_exception_message(
          QStringLiteral("ERROR:"),
          QString::fromLocal8Bit(error != nullptr ? error : "Unknown Error"));
    }
  } catch (...) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("Unknown Error");
    }
  }
  return false;
}

QString validation_exception_message(const QString& header,
                                     const QString& detail = QString()) {
  if (detail.trimmed().isEmpty()) {
    return header;
  }
  return header + QStringLiteral("\n") + detail.trimmed();
}

#ifdef Z7_TESTING
void write_stderr_line(const QString& text) {
  const QByteArray encoded = text.toLocal8Bit();
  if (!encoded.isEmpty()) {
    std::fwrite(encoded.constData(), 1, static_cast<size_t>(encoded.size()), stderr);
  }
  std::fwrite("\n", 1, 1, stderr);
  std::fflush(stderr);
}
#endif

int map_exception_to_exit_code(const CSystemException& error) {
  if (error.ErrorCode == E_OUTOFMEMORY) {
    return NExitCode::kMemoryError;
  }
  if (error.ErrorCode == E_ABORT) {
    return NExitCode::kUserBreak;
  }
  return NExitCode::kFatalError;
}

class ScopedStderrCapture final {
 public:
  ScopedStderrCapture() = default;

  ScopedStderrCapture(const ScopedStderrCapture&) = delete;
  ScopedStderrCapture& operator=(const ScopedStderrCapture&) = delete;

  ~ScopedStderrCapture() {
    restore();
  }

  bool start(QString* error_message) {
    std::fflush(stderr);
    saved_fd_ = Z7_DUP(Z7_FILENO(stderr));
    if (saved_fd_ < 0) {
      if (error_message != nullptr) {
        *error_message = QStringLiteral("Failed to duplicate stderr stream.");
      }
      return false;
    }

    int pipe_fds[2] = {-1, -1};
    if (Z7_PIPE(pipe_fds) != 0) {
      restore();
      if (error_message != nullptr) {
        *error_message = QStringLiteral("Failed to create stderr capture pipe.");
      }
      return false;
    }

    read_fd_ = pipe_fds[0];
    const int write_fd = pipe_fds[1];
    if (Z7_DUP2(write_fd, Z7_FILENO(stderr)) < 0) {
      Z7_CLOSE(write_fd);
      restore();
      if (error_message != nullptr) {
        *error_message = QStringLiteral("Failed to redirect stderr stream.");
      }
      return false;
    }
    Z7_CLOSE(write_fd);

    reader_ = std::thread([this]() {
      char buffer[4096];
      for (;;) {
        const int n = Z7_READ(read_fd_, buffer, sizeof(buffer));
        if (n <= 0) {
          break;
        }
        const qsizetype remaining =
            kMaxCapturedBytes - captured_.size();
        if (remaining > 0) {
          captured_.append(buffer, std::min<qsizetype>(remaining, n));
        }
        if (n > remaining) {
          truncated_ = true;
        }
      }
    });

    capturing_ = true;
    return true;
  }

  void restore() {
    std::fflush(stderr);
    if (saved_fd_ >= 0) {
      static_cast<void>(Z7_DUP2(saved_fd_, Z7_FILENO(stderr)));
      Z7_CLOSE(saved_fd_);
      saved_fd_ = -1;
    }
    if (reader_.joinable()) {
      reader_.join();
    }
    if (read_fd_ >= 0) {
      Z7_CLOSE(read_fd_);
      read_fd_ = -1;
    }
    capturing_ = false;
  }

  QString captured_text() const {
    QString text = QString::fromLocal8Bit(captured_);
    if (truncated_) {
      if (!text.endsWith(QLatin1Char('\n')) && !text.isEmpty()) {
        text.append(QLatin1Char('\n'));
      }
      text.append(QStringLiteral("[stderr output truncated]"));
    }
    return text;
  }

 private:
  static constexpr qsizetype kMaxCapturedBytes = 64 * 1024;
  int saved_fd_ = -1;
  int read_fd_ = -1;
  std::thread reader_;
  QByteArray captured_;
  bool truncated_ = false;
  bool capturing_ = false;
};

int run_original_console_main_caught(const QStringList& argv) {
  g_StdStream = &g_StdOut;
  g_ErrStream = &g_StdErr;

  std::vector<QByteArray> encoded_args;
  encoded_args.reserve(static_cast<size_t>(argv.size()) + 1U);
  encoded_args.push_back(QFile::encodeName(QCoreApplication::applicationFilePath()));
  for (const QString& arg : argv) {
    encoded_args.push_back(arg.toLocal8Bit());
  }

  std::vector<char*> raw_args;
  raw_args.reserve(encoded_args.size());
  for (QByteArray& encoded : encoded_args) {
    raw_args.push_back(encoded.data());
  }

  try {
    return Main2(static_cast<int>(raw_args.size()), raw_args.data());
  } catch (const CNewException&) {
    std::fputs("\n\nERROR: Can't allocate required memory!\n", stderr);
    return NExitCode::kMemoryError;
  } catch (const CMessagePathException& error) {
    std::fputs("\n\nCommand Line Error:\n", stderr);
    const QByteArray text = from_ustring(error).toLocal8Bit();
    std::fwrite(text.constData(), 1, static_cast<size_t>(text.size()), stderr);
    std::fputc('\n', stderr);
    return NExitCode::kUserError;
  } catch (const CSystemException& error) {
    if (error.ErrorCode == E_ABORT) {
      std::fputs("\n\nBreak signaled\n", stderr);
    } else {
      std::fputs("\n\nSystem ERROR:\n", stderr);
      const UString message =
          NWindows::NError::MyFormatMessage(error.ErrorCode);
      const QByteArray text = from_ustring(message).toLocal8Bit();
      std::fwrite(text.constData(), 1, static_cast<size_t>(text.size()), stderr);
      std::fputc('\n', stderr);
    }
    return map_exception_to_exit_code(error);
  } catch (NExitCode::EEnum exit_code) {
    return exit_code;
  } catch (const UString& error) {
    std::fputs("\n\nERROR:\n", stderr);
    const QByteArray text = from_ustring(error).toLocal8Bit();
    std::fwrite(text.constData(), 1, static_cast<size_t>(text.size()), stderr);
    std::fputc('\n', stderr);
    return NExitCode::kFatalError;
  } catch (const AString& error) {
    std::fputs("\n\nERROR:\n", stderr);
    std::fwrite(error.Ptr(), 1, static_cast<size_t>(error.Len()), stderr);
    std::fputc('\n', stderr);
    return NExitCode::kFatalError;
  } catch (const char* error) {
    std::fputs("\n\nERROR:\n", stderr);
    std::fputs(error != nullptr ? error : "Unknown Error", stderr);
    std::fputc('\n', stderr);
    return NExitCode::kFatalError;
  } catch (...) {
    std::fputs("\n\nUnknown Error\n", stderr);
    return NExitCode::kFatalError;
  }
}

bool has_unsupported_email_mode(const CArcCmdLineOptions& options) {
  return options.UpdateOptions.EMailMode;
}

bool is_original_7zg_gui_command_supported(const CArcCmdLineOptions& options) {
  return options.Command.CommandType == NCommandType::kBenchmark ||
         options.Command.CommandType == NCommandType::kHash ||
         options.Command.IsFromExtractGroup() ||
         options.Command.IsFromUpdateGroup();
}

bool show_cli_error_dialog(const QString& message, bool suppress_by_yes_to_all) {
  if (message.trimmed().isEmpty() || suppress_by_yes_to_all) {
    return false;
  }
#ifdef Z7_TESTING
  if (qEnvironmentVariable(kSuppressGuiErrorDialogsForTestsEnv)
          .trimmed() == QStringLiteral("1")) {
    write_stderr_line(
        QStringLiteral("[Z7_TESTING] GUI error dialog suppressed: title=%1\n%2")
            .arg(original_gui_error_dialog_title(), message.trimmed()));
    return true;
  }
#endif
  if (!QGuiApplication::screens().isEmpty()) {
    QMessageBox box(QMessageBox::Critical,
                    original_gui_error_dialog_title(),
                    message.trimmed(),
                    QMessageBox::Ok,
                    nullptr,
                    original_gui_error_dialog_flags());
    static_cast<QDialog&>(box).setWindowTitle(original_gui_error_dialog_title());
    box.exec();
    return true;
  }
  return false;
}

CliWorkerResult make_cli_worker_error(int exit_code,
                                      const QString& message,
                                      bool suppress_gui_error) {
  CliWorkerResult result;
  result.exit_code = exit_code;
  result.summary = message.trimmed();
  result.error_dialog_shown =
      show_cli_error_dialog(result.summary, suppress_gui_error);
  return result;
}

QString console_error_dialog_message(const QString& stderr_text,
                                     const QString& stdout_text) {
  const QString stderr_trimmed = stderr_text.trimmed();
  if (!stderr_trimmed.isEmpty()) {
    return stderr_trimmed;
  }
  const QString stdout_trimmed = stdout_text.trimmed();
  if (!stdout_trimmed.isEmpty()) {
    return stdout_trimmed;
  }
  return QStringLiteral("7zG task failed.");
}

std::optional<z7::ui::gui::GuiTaskSpec> build_show_dialog_spec(
    const CArcCmdLineOptions& options,
    QString* error_message) {
  if (error_message != nullptr) {
    error_message->clear();
  }

  if (options.Command.CommandType == NCommandType::kExtract ||
      options.Command.CommandType == NCommandType::kExtractFull) {
    if (options.ArchiveName.IsEmpty()) {
      if (error_message != nullptr) {
        *error_message = unsupported_show_dialog_message();
      }
      return std::nullopt;
    }
    z7::ui::gui::ExtractTaskSpec spec;
    spec.show_dialog = true;
    spec.archive_inputs.push_back(to_native_string(options.ArchiveName));
    spec.output_dir = to_native_string(from_fstring(options.ExtractOptions.OutputDir));
    spec.path_mode = to_extract_path_mode(options.ExtractOptions.PathMode);
    spec.eliminate_root_duplication =
        options.ExtractOptions.ElimDup.Def && options.ExtractOptions.ElimDup.Val;
    spec.zone_id_mode = to_zone_id_mode(options.ExtractOptions.ZoneMode);
    spec.overwrite_switch =
        to_overwrite_switch(options.ExtractOptions.OverwriteMode);
    return z7::ui::gui::GuiTaskSpec{std::move(spec)};
  }

  if (options.Command.CommandType == NCommandType::kAdd ||
      options.Command.CommandType == NCommandType::kUpdate) {
    if (options.ArchiveName.IsEmpty()) {
      if (error_message != nullptr) {
        *error_message = unsupported_show_dialog_message();
      }
      return std::nullopt;
    }
    z7::ui::gui::AddTaskSpec spec;
    spec.show_dialog = true;
    spec.archive_path = to_native_string(options.ArchiveName);
    spec.archive_type = to_native_string(options.ArcType);
    spec.update_mode =
        options.Command.CommandType == NCommandType::kUpdate ? "update" : "add";
    spec.path_mode = to_add_path_mode(options.UpdateOptions.PathMode);
    spec.create_sfx = options.UpdateOptions.SfxMode;
    spec.delete_after_compressing = options.UpdateOptions.DeleteAfterCompressing;
    for (unsigned i = 0; i < options.Properties.Size(); ++i) {
      const CProperty& property = options.Properties[i];
      std::string parameter = to_native_string(from_ustring(property.Name));
      if (!property.Value.IsEmpty()) {
        parameter += '=';
        parameter += to_native_string(from_ustring(property.Value));
      }
      spec.extra_parameters.push_back(std::move(parameter));
    }
    for (unsigned i = 0; i < options.Censor.CensorPaths.Size(); ++i) {
      const NWildcard::CCensorPath& path = options.Censor.CensorPaths[i];
      if (!path.Include) {
        continue;
      }
      spec.input_paths.push_back(to_native_string(path.Path));
    }
    return z7::ui::gui::GuiTaskSpec{std::move(spec)};
  }

  if (error_message != nullptr) {
    *error_message = unsupported_show_dialog_message();
  }
  return std::nullopt;
}

CliWorkerResult run_show_dialog_spec_blocking(
    const z7::ui::gui::GuiTaskSpec& spec) {
  z7::ui::gui::GuiAppController controller;
  QEventLoop loop;
  int exit_code = NExitCode::kFatalError;
  QString summary;
  controller.run_task_spec_async(
      spec,
      QString(),
      z7::ui::gui::TaskCancellation::create(),
      [&loop, &exit_code, &summary](
          const z7::ui::gui::GuiTaskCompletion& completion) {
        exit_code = completion.exit_code;
        summary = completion.summary;
        loop.quit();
      });
  loop.exec();
  CliWorkerResult result;
  result.exit_code = exit_code;
  result.summary = summary.trimmed();
  if (exit_code != 0 && !summary.trimmed().isEmpty()) {
    result.error_dialog_shown =
        show_cli_error_dialog(summary.trimmed(), false);
  }
  return result;
}

}  // namespace

CliValidationResult validate_cli_arguments(const QStringList& argv) {
  CliValidationResult result;
  if (argv.isEmpty()) {
    result.no_command = true;
    result.error_message = QStringLiteral("Specify command");
    return result;
  }

  CArcCmdLineOptions options;
  QString parse_error;
  if (!parse_cli_options(argv, &options, &parse_error)) {
    result.error_message = parse_error;
    return result;
  }
  if (has_unsupported_email_mode(options)) {
    result.error_message = unsupported_email_message();
    result.suppress_gui_error = options.YesToAll;
    return result;
  }
  result.ok = true;
  return result;
}

CliWorkerResult run_cli_worker_payload(
    const z7::task_ipc_runtime::TaskIpcCliPayload& payload) {
  const QString working_dir = payload.working_dir.trimmed();
  if (!working_dir.isEmpty()) {
    QDir::setCurrent(working_dir);
  }

  const CliValidationResult validation = validate_cli_arguments(payload.argv);
  if (!validation.ok) {
    return make_cli_worker_error(
        NExitCode::kUserError,
        validation.error_message.trimmed().isEmpty()
            ? unsupported_email_message()
            : validation.error_message.trimmed(),
        validation.suppress_gui_error);
  }

  CArcCmdLineOptions options;
  QString parse_error;
  if (!parse_cli_options(payload.argv, &options, &parse_error)) {
    return make_cli_worker_error(
        NExitCode::kUserError,
        parse_error.trimmed().isEmpty()
            ? QStringLiteral("Failed to parse 7zG CLI arguments.")
            : parse_error.trimmed(),
        false);
  }
  if (!is_original_7zg_gui_command_supported(options)) {
    return make_cli_worker_error(NExitCode::kFatalError,
                                 unsupported_gui_command_message(),
                                 options.YesToAll);
  }
  if (options.ShowDialog) {
    QString spec_error;
    const std::optional<z7::ui::gui::GuiTaskSpec> spec =
        build_show_dialog_spec(options, &spec_error);
    if (!spec.has_value()) {
      return make_cli_worker_error(
          NExitCode::kUserError,
          spec_error.trimmed().isEmpty() ? unsupported_show_dialog_message()
                                         : spec_error.trimmed(),
          options.YesToAll);
    }
    return run_show_dialog_spec_blocking(*spec);
  }

  QString capture_error;
  ScopedStderrCapture stderr_capture;
  if (!stderr_capture.start(&capture_error)) {
    return make_cli_worker_error(NExitCode::kFatalError,
                                 capture_error,
                                 false);
  }
  const int exit_code = run_original_console_main_caught(payload.argv);
  std::fflush(stdout);
  std::fflush(stderr);
  stderr_capture.restore();

  CliWorkerResult result;
  result.exit_code = exit_code;
  if (exit_code != 0) {
    result.summary = console_error_dialog_message(stderr_capture.captured_text(),
                                                  QString());
    result.error_dialog_shown =
        show_cli_error_dialog(result.summary, options.YesToAll);
  }
  return result;
}

int run_cli_launcher(const QStringList& argv) {
  const CliValidationResult validation = validate_cli_arguments(argv);
  if (validation.no_command) {
    show_cli_error_dialog(validation.error_message, false);
    return 0;
  }
  if (!validation.ok) {
    show_cli_error_dialog(validation.error_message,
                          validation.suppress_gui_error);
    return NExitCode::kUserError;
  }

  z7::task_ipc_runtime::TaskIpcPayload payload;
  payload.command = z7::task_ipc_runtime::TaskIpcCommandKind::kCli;
  payload.refresh_after_finish = false;
  payload.cli = z7::task_ipc_runtime::TaskIpcCliPayload{};
  payload.cli->argv = argv;
  payload.cli->working_dir = QDir::currentPath();

  QString ipc_error;
  if (!z7::task_ipc_runtime::ensure_task_ipc_bootstrap_ready(&ipc_error)) {
    show_cli_error_dialog(ipc_error.trimmed().isEmpty()
                              ? QStringLiteral("Failed to initialize task IPC.")
                              : ipc_error.trimmed(),
                          false);
    return NExitCode::kFatalError;
  }

  const QString owner_instance_id =
      QUuid::createUuid().toString(QUuid::WithoutBraces);
  z7::task_ipc_runtime::TaskIpcDispatchResult dispatch;
  z7::task_ipc_runtime::TaskIpcManagedProcessOptions process_options;
  CArcCmdLineOptions launch_options;
  QString launch_parse_error;
  if (!parse_cli_options(argv, &launch_options, &launch_parse_error)) {
    show_cli_error_dialog(launch_parse_error.trimmed().isEmpty()
                              ? QStringLiteral("Failed to parse 7zG CLI arguments.")
                              : launch_parse_error.trimmed(),
                          false);
    return NExitCode::kUserError;
  }
  process_options.forward_stdin = launch_options.StdInMode;
  process_options.forward_stdout = launch_options.StdOutMode;
  QProcess* started_process = nullptr;
  if (!z7::task_ipc_runtime::dispatch_task_ipc_task_managed_process(
          QCoreApplication::applicationFilePath(),
          payload.cli->working_dir,
          owner_instance_id,
          payload,
          process_options,
          &dispatch,
          &started_process,
          &ipc_error)) {
    show_cli_error_dialog(ipc_error.trimmed().isEmpty()
                              ? QStringLiteral("Failed to start 7zG task IPC worker.")
                              : ipc_error.trimmed(),
                          false);
    return NExitCode::kFatalError;
  }
  std::unique_ptr<QProcess> worker_process(started_process);

  z7::task_ipc_runtime::TaskIpcEvent completion;
  bool completed = false;
  while (!completed) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    QVector<z7::task_ipc_runtime::TaskIpcEvent> events;
    QString collect_error;
    if (!z7::task_ipc_runtime::collect_task_ipc_events(
            owner_instance_id, &events, &collect_error)) {
      show_cli_error_dialog(
          collect_error.trimmed().isEmpty()
              ? QStringLiteral("Failed to collect 7zG task IPC events.")
              : collect_error.trimmed(),
          false);
      return NExitCode::kFatalError;
    }
    for (const z7::task_ipc_runtime::TaskIpcEvent& event : events) {
      if (event.session_id == dispatch.session_id &&
          event.generation == dispatch.generation &&
          event.event_kind ==
              z7::task_ipc_runtime::TaskIpcEventKind::kCompleted) {
        completion = event;
        completed = true;
      }
      z7::task_ipc_runtime::acknowledge_task_ipc_event(event, nullptr);
    }
    if (!completed) {
      if (worker_process && worker_process->state() == QProcess::NotRunning) {
        show_cli_error_dialog(
            QStringLiteral("7zG task worker exited before publishing completion."),
            false);
        return worker_process->exitCode() != 0 ? worker_process->exitCode()
                                               : NExitCode::kFatalError;
      }
      QThread::msleep(20);
    }
  }

  if (worker_process && worker_process->state() != QProcess::NotRunning) {
    static_cast<void>(worker_process->waitForFinished(5000));
  }
  if (worker_process && completion.result_code == 0 &&
      !launch_options.StdOutMode) {
    const QByteArray child_stdout = worker_process->readAllStandardOutput();
    if (!child_stdout.isEmpty()) {
      std::fwrite(child_stdout.constData(),
                  1,
                  static_cast<size_t>(child_stdout.size()),
                  stdout);
      std::fflush(stdout);
    }
  }
#ifdef Z7_TESTING
  if (qEnvironmentVariable(kSuppressGuiErrorDialogsForTestsEnv).trimmed() ==
      QStringLiteral("1")) {
    const QByteArray child_stderr = worker_process
                                        ? worker_process->readAllStandardError()
                                        : QByteArray();
    if (!child_stderr.isEmpty()) {
      std::fwrite(child_stderr.constData(),
                  1,
                  static_cast<size_t>(child_stderr.size()),
                  stderr);
      std::fflush(stderr);
    }
  }
#endif

  return completion.result_code;
}

}  // namespace z7::apps::gui
