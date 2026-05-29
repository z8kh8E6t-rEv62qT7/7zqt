#include <QAbstractButton>
#include <QApplication>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QList>
#include <QMessageBox>
#include <QMetaObject>
#include <QProgressDialog>
#include <QPushButton>
#include <QThread>
#include <QVBoxLayout>

#include <cstdio>
#include <functional>
#include <optional>
#include <string>
#include <utility>

#include "archive_session.h"

namespace {

std::string to_native_path_string(const QString& path) {
  const QByteArray encoded = QFile::encodeName(path);
  return std::string(encoded.constData(), static_cast<size_t>(encoded.size()));
}

QString from_summary(const z7::app::OperationOutcome& outcome) {
  QString summary = QString::fromStdString(outcome.summary).trimmed();
  if (summary.isEmpty()) {
    summary = QObject::tr("Extraction failed.");
  }
  return summary;
}

std::string self_7z_type_hint(const QString& archive_path) {
  const qint64 archive_size = QFileInfo(archive_path).size();
  if (archive_size <= 0) {
    return "7z";
  }
  return "7z:s" + std::to_string(archive_size);
}

bool is_switch_prefix(const QString& arg, QLatin1Char key) {
  if (arg.size() < 2) {
    return false;
  }
  const QChar prefix = arg.at(0);
  if (prefix != QLatin1Char('-')) {
    return false;
  }
  return arg.at(1).toLower() == key;
}

struct SfxOptions {
  QString output_dir;
  QString password;
  bool password_defined = false;
  bool yes_to_all = false;
#ifdef Z7_TESTING
  bool test_auto_extract = false;
#endif
};

bool parse_options(const QStringList& args, SfxOptions* options, QString* error) {
  if (options == nullptr) {
    return false;
  }
  if (error != nullptr) {
    error->clear();
  }

  for (int i = 1; i < args.size(); ++i) {
    const QString arg = args.at(i);
    if (arg.compare(QStringLiteral("-y"), Qt::CaseInsensitive) == 0) {
      options->yes_to_all = true;
      continue;
    }
    if (is_switch_prefix(arg, QLatin1Char('o'))) {
      options->output_dir = arg.mid(2);
      continue;
    }
    if (is_switch_prefix(arg, QLatin1Char('p'))) {
      options->password = arg.mid(2);
      options->password_defined = true;
      continue;
    }
#ifdef Z7_TESTING
    if (arg == QStringLiteral("--z7-sfx-test-auto-extract")) {
      options->test_auto_extract = true;
      continue;
    }
    if (arg.startsWith(QStringLiteral("--z7-sfx-test-output="))) {
      options->output_dir =
          arg.mid(QStringLiteral("--z7-sfx-test-output=").size());
      continue;
    }
    if (arg.startsWith(QStringLiteral("--z7-sfx-test-password="))) {
      options->password =
          arg.mid(QStringLiteral("--z7-sfx-test-password=").size());
      options->password_defined = true;
      continue;
    }
    if (arg == QStringLiteral("--z7-sfx-test-yes")) {
      options->yes_to_all = true;
      continue;
    }
#endif
    if (error != nullptr) {
      *error = QObject::tr("Unsupported switch: %1").arg(arg);
    }
    return false;
  }
  return true;
}

class SfxExtractDialog final : public QDialog {
 public:
  explicit SfxExtractDialog(QString output_dir, QWidget* parent = nullptr)
      : QDialog(parent) {
    setWindowTitle(QStringLiteral("7-Zip self-extracting archive"));
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);
#ifdef Z7_TESTING
    setObjectName(QStringLiteral("z7SfxDialog"));
#endif

    auto* root = new QVBoxLayout(this);
    auto* form = new QFormLayout();
    root->addLayout(form);

    output_edit_ = new QLineEdit(std::move(output_dir), this);
#ifdef Z7_TESTING
    output_edit_->setObjectName(QStringLiteral("z7SfxOutputEdit"));
#endif

    auto* path_row = new QWidget(this);
    auto* path_layout = new QHBoxLayout(path_row);
    path_layout->setContentsMargins(0, 0, 0, 0);
    path_layout->addWidget(output_edit_, 1);
    auto* browse = new QPushButton(QStringLiteral("..."), path_row);
#ifdef Z7_TESTING
    browse->setObjectName(QStringLiteral("z7SfxBrowseButton"));
#endif
    path_layout->addWidget(browse);
    form->addRow(new QLabel(QStringLiteral("Extract to:"), this), path_row);

    auto* buttons = new QDialogButtonBox(this);
    extract_button_ =
        buttons->addButton(QStringLiteral("Extract"), QDialogButtonBox::AcceptRole);
    buttons->addButton(QStringLiteral("Cancel"), QDialogButtonBox::RejectRole);
#ifdef Z7_TESTING
    extract_button_->setObjectName(QStringLiteral("z7SfxExtractButton"));
    buttons->setObjectName(QStringLiteral("z7SfxButtons"));
#endif
    root->addWidget(buttons);

    connect(browse, &QPushButton::clicked, this, [this]() {
      const QString selected = QFileDialog::getExistingDirectory(
          this,
          QStringLiteral("Select Folder"),
          output_edit_->text().trimmed());
      if (!selected.isEmpty()) {
        output_edit_->setText(QDir::toNativeSeparators(selected));
      }
    });
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    resize(520, sizeHint().height());
  }

  QString output_dir() const {
    return output_edit_ != nullptr ? output_edit_->text().trimmed() : QString();
  }

 private:
  QLineEdit* output_edit_ = nullptr;
  QPushButton* extract_button_ = nullptr;
};

class SfxArchiveDelegate final : public QObject, public z7::app::IArchiveDelegate {
 public:
  SfxArchiveDelegate(QString password,
                     bool password_defined,
                     bool yes_to_all,
                     bool test_auto_extract)
      : password_(std::move(password)),
        password_defined_(password_defined),
        yes_to_all_(yes_to_all),
        test_auto_extract_(test_auto_extract) {}

  void set_progress_dialog(QProgressDialog* progress_dialog) {
    progress_dialog_ = progress_dialog;
  }

  void set_finished_callback(std::function<void()> callback) {
    finished_callback_ = std::move(callback);
  }

  const std::optional<z7::app::OperationOutcome>& outcome() const {
    return outcome_;
  }

  std::optional<z7::app::OverwriteDecision> request_overwrite(
      const z7::app::OverwritePrompt& prompt) override {
    if (yes_to_all_) {
      return z7::app::OverwriteDecision::kYesToAll;
    }
    if (test_auto_extract_) {
      return z7::app::OverwriteDecision::kCancel;
    }

    z7::app::OverwriteDecision decision = z7::app::OverwriteDecision::kCancel;
    invoke_on_ui_thread_blocking([&]() {
      QMessageBox box(QMessageBox::Question,
                      QStringLiteral("Confirm File Replace"),
                      QStringLiteral("The destination already contains:\n%1")
                          .arg(QString::fromStdString(prompt.existing_path)),
                      QMessageBox::NoButton,
                      nullptr);
      QAbstractButton* yes =
          box.addButton(QStringLiteral("Yes"), QMessageBox::YesRole);
      QAbstractButton* yes_to_all =
          box.addButton(QStringLiteral("Yes to All"), QMessageBox::YesRole);
      QAbstractButton* no =
          box.addButton(QStringLiteral("No"), QMessageBox::NoRole);
      QAbstractButton* no_to_all =
          box.addButton(QStringLiteral("No to All"), QMessageBox::NoRole);
      QAbstractButton* rename =
          box.addButton(QStringLiteral("Auto Rename"), QMessageBox::ActionRole);
      QAbstractButton* cancel =
          box.addButton(QStringLiteral("Cancel"), QMessageBox::RejectRole);
      box.setDefaultButton(qobject_cast<QPushButton*>(no));
      box.exec();
      QAbstractButton* clicked = box.clickedButton();
      if (clicked == yes) {
        decision = z7::app::OverwriteDecision::kYes;
      } else if (clicked == yes_to_all) {
        decision = z7::app::OverwriteDecision::kYesToAll;
      } else if (clicked == no) {
        decision = z7::app::OverwriteDecision::kNo;
      } else if (clicked == no_to_all) {
        decision = z7::app::OverwriteDecision::kNoToAll;
      } else if (clicked == rename) {
        decision = z7::app::OverwriteDecision::kAutoRename;
      } else if (clicked == cancel) {
        decision = z7::app::OverwriteDecision::kCancel;
      }
    });
    return decision;
  }

  std::optional<z7::app::PasswordReply> request_password(
      const z7::app::PasswordPrompt& prompt) override {
    if (password_defined_ && !password_used_ &&
        prompt.reason_kind != z7::app::PasswordPromptReason::kWrongPassword) {
      password_used_ = true;
      z7::app::PasswordReply reply;
      reply.kind = z7::app::PasswordReplyKind::kProvide;
      reply.password = to_native_path_string(password_);
      return reply;
    }
    if (test_auto_extract_) {
      z7::app::PasswordReply reply;
      reply.kind = z7::app::PasswordReplyKind::kCancel;
      return reply;
    }

    z7::app::PasswordReply reply;
    reply.kind = z7::app::PasswordReplyKind::kCancel;
    invoke_on_ui_thread_blocking([&]() {
      QInputDialog dialog;
      dialog.setWindowTitle(QStringLiteral("Password"));
      dialog.setLabelText(
          prompt.reason_kind == z7::app::PasswordPromptReason::kWrongPassword
              ? QStringLiteral("Wrong password. Enter password:")
              : QStringLiteral("Enter password:"));
      dialog.setTextEchoMode(QLineEdit::Password);
      if (dialog.exec() == QDialog::Accepted) {
        reply.kind = z7::app::PasswordReplyKind::kProvide;
        reply.password = to_native_path_string(dialog.textValue());
      }
    });
    return reply;
  }

  std::optional<z7::app::ChoiceReply> request_choice(
      const z7::app::ChoicePrompt& prompt) override {
    if (test_auto_extract_) {
      return z7::app::ChoiceReply{};
    }

    z7::app::ChoiceReply reply;
    invoke_on_ui_thread_blocking([&]() {
      QMessageBox box(QMessageBox::Question,
                      QString::fromStdString(prompt.title),
                      QString::fromStdString(prompt.message),
                      QMessageBox::NoButton,
                      nullptr);
      QList<QAbstractButton*> buttons;
      for (const std::string& choice : prompt.choices) {
        buttons.push_back(box.addButton(QString::fromStdString(choice),
                                        QMessageBox::ActionRole));
      }
      if (prompt.default_index >= 0 &&
          prompt.default_index < buttons.size()) {
        box.setDefaultButton(qobject_cast<QPushButton*>(
            buttons.at(prompt.default_index)));
      }
      box.exec();
      const int index = buttons.indexOf(box.clickedButton());
      if (index >= 0) {
        reply.kind = z7::app::ChoiceReplyKind::kSelect;
        reply.selected_index = index;
      }
    });
    return reply;
  }

  std::optional<z7::app::MemoryLimitReply> request_memory_limit(
      const z7::app::MemoryLimitPrompt& prompt) override {
    (void)prompt;
    z7::app::MemoryLimitReply reply;
    reply.action = z7::app::MemoryLimitAction::kAllowOnce;
    return reply;
  }

  void on_progress(const z7::app::ProgressSnapshot& progress) override {
    QMetaObject::invokeMethod(this, [this, progress]() {
      if (progress_dialog_ == nullptr) {
        return;
      }
      if (progress.percent >= 0) {
        progress_dialog_->setRange(0, 100);
        progress_dialog_->setValue(progress.percent);
      }
      if (!progress.current_path.empty()) {
        progress_dialog_->setLabelText(
            QStringLiteral("Extracting %1")
                .arg(QString::fromStdString(progress.current_path)));
      }
    }, Qt::QueuedConnection);
  }

  void on_finished(const z7::app::OperationOutcome& outcome) override {
    QMetaObject::invokeMethod(this, [this, outcome]() {
      outcome_ = outcome;
      if (progress_dialog_ != nullptr) {
        progress_dialog_->setValue(progress_dialog_->maximum());
      }
      if (finished_callback_) {
        finished_callback_();
      }
    }, Qt::QueuedConnection);
  }

 private:
  template <typename Callback>
  void invoke_on_ui_thread_blocking(Callback&& callback) {
    if (QThread::currentThread() == thread()) {
      callback();
      return;
    }
    QMetaObject::invokeMethod(this,
                              std::forward<Callback>(callback),
                              Qt::BlockingQueuedConnection);
  }

  QString password_;
  bool password_defined_ = false;
  bool password_used_ = false;
  bool yes_to_all_ = false;
  bool test_auto_extract_ = false;
  QProgressDialog* progress_dialog_ = nullptr;
  std::function<void()> finished_callback_;
  std::optional<z7::app::OperationOutcome> outcome_;
};

int run_extract(const QString& archive_path,
                const SfxOptions& options,
                QWidget* parent) {
  const QString output_dir =
      options.output_dir.isEmpty()
          ? QFileInfo(archive_path).absolutePath()
          : options.output_dir;
  if (!QDir().mkpath(output_dir)) {
    const QString message =
        QObject::tr("Cannot create output directory:\n%1").arg(output_dir);
#ifdef Z7_TESTING
    if (options.test_auto_extract) {
      std::fprintf(stderr, "%s\n", message.toLocal8Bit().constData());
      return 2;
    }
#endif
    QMessageBox::critical(parent,
                          QStringLiteral("7-Zip self-extracting archive"),
                          message);
    return 2;
  }

  z7::app::ExtractRequest extract;
  extract.archive_path = to_native_path_string(archive_path);
  extract.archive_type_hint = self_7z_type_hint(archive_path);
  extract.output_dir = to_native_path_string(output_dir);
  extract.path_mode = z7::app::ExtractPathMode::kFullPaths;
  extract.overwrite_mode = options.yes_to_all
                               ? z7::app::OverwriteMode::kOverwrite
                               : z7::app::OverwriteMode::kAsk;
  if (options.password_defined) {
    extract.password = to_native_path_string(options.password);
  }

  z7::app::ArchiveRequest request;
  request.payload = std::move(extract);

  auto delegate = std::make_shared<SfxArchiveDelegate>(
      options.password,
      options.password_defined,
      options.yes_to_all,
#ifdef Z7_TESTING
      options.test_auto_extract
#else
      false
#endif
  );

  QProgressDialog progress(QStringLiteral("Extracting..."),
                           QStringLiteral("Cancel"),
                           0,
                           100,
                           parent);
  progress.setWindowTitle(QStringLiteral("7-Zip self-extracting archive"));
  progress.setMinimumDuration(0);
  progress.setAutoClose(false);
  progress.setAutoReset(false);
#ifdef Z7_TESTING
  progress.setObjectName(QStringLiteral("z7SfxProgressDialog"));
#endif
  delegate->set_progress_dialog(&progress);

  QEventLoop loop;
  delegate->set_finished_callback([&loop]() { loop.quit(); });

  z7::app::ArchiveEngine engine;
  z7::app::ArchiveSession session = engine.start(request, delegate);
  if (!session.valid()) {
    return 2;
  }

  QObject::connect(&progress, &QProgressDialog::canceled, [&session]() {
    session.cancel();
  });

#ifdef Z7_TESTING
  if (!options.test_auto_extract) {
    progress.show();
  }
#else
  progress.show();
#endif
  loop.exec();

  const std::optional<z7::app::OperationOutcome>& outcome = delegate->outcome();
  if (!outcome.has_value()) {
    return 2;
  }
  if (outcome->ok) {
    return 0;
  }

  const QString message = from_summary(*outcome);
#ifdef Z7_TESTING
  if (options.test_auto_extract) {
    std::fprintf(stderr, "%s\n", message.toLocal8Bit().constData());
    return outcome->native_code != 0 ? outcome->native_code : 2;
  }
#endif
  if (outcome->status != z7::app::OperationStatus::kCanceled) {
    QMessageBox::critical(parent,
                          QStringLiteral("7-Zip self-extracting archive"),
                          message);
  }
  return outcome->native_code != 0 ? outcome->native_code : 2;
}

}  // namespace

int main(int argc, char* argv[]) {
  QApplication app(argc, argv);
  const QString self_path = QCoreApplication::applicationFilePath();

  SfxOptions options;
  options.output_dir = QFileInfo(self_path).absolutePath();
  QString parse_error;
  if (!parse_options(QCoreApplication::arguments(), &options, &parse_error)) {
#ifdef Z7_TESTING
    if (options.test_auto_extract) {
      std::fprintf(stderr, "%s\n", parse_error.toLocal8Bit().constData());
      return 7;
    }
#endif
    QMessageBox::critical(nullptr,
                          QStringLiteral("7-Zip self-extracting archive"),
                          parse_error);
    return 7;
  }

#ifdef Z7_TESTING
  if (options.test_auto_extract) {
    return run_extract(self_path, options, nullptr);
  }
#endif

  SfxExtractDialog dialog(options.output_dir);
  if (dialog.exec() != QDialog::Accepted) {
    return 1;
  }
  options.output_dir = dialog.output_dir();
  return run_extract(self_path, options, &dialog);
}
