// src/ui/gui/src/gui_task_runner_helpers_dialogs.cpp
// Role: Prompt dialogs for overwrite/password/choice/memory-limit interactions.

#include "gui_task_runner_helpers.h"

#include <QDateTime>
#include <QDir>
#include <QInputDialog>
#include <QMessageBox>
#include <QPushButton>
#include <QTimeZone>

#include <algorithm>
#include <optional>
#include <vector>

#include "archive_string_codec_qt.h"
#include "extract_memory_settings.h"
#include "official_lang_catalog.h"
#include "password_prompt_dialog.h"

namespace z7::ui::gui {
namespace {

using z7::ui::runtime_support::L;
using z7::ui::runtime_support::strip_mnemonic;

constexpr uint64_t kBytesPerMegabyte = 1024ULL * 1024ULL;

QString format_overwrite_size(bool defined, uint64_t size) {
  if (!defined) {
    return QStringLiteral("?");
  }
  return QString::number(size);
}

QString format_overwrite_time(const std::optional<int64_t>& msecs_utc) {
  if (!msecs_utc.has_value()) {
    return QStringLiteral("?");
  }
  const QDateTime utc = QDateTime::fromMSecsSinceEpoch(*msecs_utc, QTimeZone::UTC);
  if (!utc.isValid()) {
    return QStringLiteral("?");
  }
  return utc.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
}

QString memory_limit_dialog_message(const z7::app::MemoryLimitPrompt& prompt,
                                    uint64_t persisted_limit_bytes) {
  if (prompt.required_usage_bytes != 0 || prompt.current_limit_defined ||
      !prompt.archive_path.empty() || !prompt.file_path.empty()) {
    QString message = QStringLiteral("The extraction operation requires a large amount of memory.\nRequired: %1 MB")
                          .arg(prompt.required_usage_bytes / kBytesPerMegabyte);
    if (prompt.current_limit_defined) {
      message += QStringLiteral("\nAllowed limit: %1 MB")
                     .arg(prompt.current_limit_bytes / kBytesPerMegabyte);
    }
    if (persisted_limit_bytes != 0) {
      message += QStringLiteral("\nConfigured limit: %1 MB")
                     .arg(persisted_limit_bytes / kBytesPerMegabyte);
    }
    if (!prompt.archive_path.empty()) {
      message += QStringLiteral("\nArchive: %1")
                     .arg(QDir::toNativeSeparators(
                         z7::ui::archive_support::from_native_string(
                             prompt.archive_path)));
    }
    if (!prompt.file_path.empty()) {
      message += QStringLiteral("\nFile: %1")
                     .arg(QDir::toNativeSeparators(
                         z7::ui::archive_support::from_native_string(
                             prompt.file_path)));
    }
    return message;
  }

  QString message = QStringLiteral("Estimated memory usage is too high:\n%1 MB")
                        .arg(prompt.estimated_usage_bytes / kBytesPerMegabyte);
  if (prompt.safe_limit_defined) {
    message += QStringLiteral("\nSafe limit: %1 MB")
                   .arg(prompt.safe_limit_bytes / kBytesPerMegabyte);
  }
  if (persisted_limit_bytes != 0) {
    message += QStringLiteral("\nConfigured limit: %1 MB")
                   .arg(persisted_limit_bytes / kBytesPerMegabyte);
  }
  return message;
}

bool is_extract_memory_limit_prompt(const z7::app::MemoryLimitPrompt& prompt) {
  return prompt.required_usage_bytes != 0 || prompt.current_limit_defined ||
         !prompt.archive_path.empty() || !prompt.file_path.empty() ||
         prompt.test_mode;
}

uint64_t load_persisted_memory_limit_bytes(
    const z7::app::MemoryLimitPrompt& prompt) {
  if (is_extract_memory_limit_prompt(prompt)) {
    return z7::ui::runtime_support::configured_extract_memory_limit_bytes();
  }
  return z7::ui::runtime_support::load_benchmark_memory_limit_bytes();
}

void save_persisted_memory_limit_bytes(const z7::app::MemoryLimitPrompt& prompt,
                                       uint64_t bytes) {
  if (is_extract_memory_limit_prompt(prompt)) {
    z7::ui::runtime_support::save_extract_memory_limit_bytes_and_enable(bytes);
    return;
  }
  z7::ui::runtime_support::save_benchmark_memory_limit_bytes(bytes);
}

bool prompt_for_updated_memory_limit_bytes(QWidget* parent,
                                           const QString& title,
                                           uint64_t min_required_bytes,
                                           uint64_t seed_bytes,
                                           uint64_t* out_bytes) {
  if (out_bytes == nullptr) {
    return false;
  }
  uint64_t suggested = std::max(min_required_bytes, seed_bytes);
  const uint64_t suggested_mb = (suggested + kBytesPerMegabyte - 1) / kBytesPerMegabyte;
  while (true) {
    bool ok = false;
    const QString entered = QInputDialog::getText(
        parent,
        title,
        QStringLiteral("Set memory limit (MB):"),
        QLineEdit::Normal,
        QString::number(suggested_mb),
        &ok);
    if (!ok) {
      return false;
    }

    bool parsed_ok = false;
    const qulonglong mb = entered.trimmed().toULongLong(&parsed_ok);
    if (!parsed_ok || mb == 0) {
      QMessageBox::warning(parent,
                           title,
                           QStringLiteral("Please enter a positive integer value in MB."));
      continue;
    }

    const uint64_t bytes = static_cast<uint64_t>(mb) * kBytesPerMegabyte;
    if (bytes < min_required_bytes) {
      QMessageBox::warning(
          parent,
          title,
          QStringLiteral("The configured limit must be at least %1 MB for this run.")
              .arg((min_required_bytes + kBytesPerMegabyte - 1) / kBytesPerMegabyte));
      continue;
    }

    *out_bytes = bytes;
    return true;
  }
}

}  // namespace

  z7::app::OverwriteDecision show_overwrite_prompt_dialog(
    QWidget* parent,
    const z7::app::OverwritePrompt& prompt) {
  QMessageBox box(parent);
#ifdef Z7_TESTING
  box.setObjectName(QStringLiteral("overwritePromptDialog"));
#endif
  box.setIcon(QMessageBox::Question);
  box.setWindowTitle(strip_mnemonic(L(3500)));

  const QString existing = QDir::toNativeSeparators(
      z7::ui::archive_support::from_native_string(prompt.existing_path));
  const QString incoming = QDir::toNativeSeparators(
      z7::ui::archive_support::from_native_string(prompt.incoming_path));
  box.setText(strip_mnemonic(L(3501)));
  box.setInformativeText(
      QStringLiteral("Existing: %1\nNew: %2\n\nExisting size: %3\nNew size: %4\n"
                     "Existing modified: %5\nNew modified: %6")
          .arg(existing,
               incoming,
               format_overwrite_size(prompt.existing_size_defined, prompt.existing_size),
               format_overwrite_size(prompt.incoming_size_defined, prompt.incoming_size),
               format_overwrite_time(prompt.existing_mtime_msecs_utc),
               format_overwrite_time(prompt.incoming_mtime_msecs_utc)));

  QPushButton* yes_button = box.addButton(QMessageBox::Yes);
  QPushButton* no_button = box.addButton(QMessageBox::No);
  QPushButton* yes_all_button = box.addButton(L(440), QMessageBox::AcceptRole);
  QPushButton* no_all_button = box.addButton(L(441), QMessageBox::DestructiveRole);
  QPushButton* rename_button = box.addButton(strip_mnemonic(L(3424)), QMessageBox::ActionRole);
  QPushButton* cancel_button = box.addButton(QMessageBox::Cancel);
  box.setDefaultButton(yes_button);
  box.exec();

  const QAbstractButton* clicked = box.clickedButton();
  if (clicked == yes_button) {
    return z7::app::OverwriteDecision::kYes;
  }
  if (clicked == no_button) {
    return z7::app::OverwriteDecision::kNo;
  }
  if (clicked == yes_all_button) {
    return z7::app::OverwriteDecision::kYesToAll;
  }
  if (clicked == no_all_button) {
    return z7::app::OverwriteDecision::kNoToAll;
  }
  if (clicked == rename_button) {
    return z7::app::OverwriteDecision::kAutoRename;
  }
  if (clicked == cancel_button) {
    return z7::app::OverwriteDecision::kCancel;
  }
  return z7::app::OverwriteDecision::kCancel;
}

z7::app::PasswordReply show_password_prompt_dialog(
    QWidget* parent,
    const z7::app::PasswordPrompt& prompt) {
  const std::optional<QString> value =
      z7::ui::runtime_support::show_password_prompt_dialog(
          parent, prompt);
  z7::app::PasswordReply reply;
  if (!value.has_value()) {
    reply.kind = z7::app::PasswordReplyKind::kCancel;
    return reply;
  }
  reply.kind = z7::app::PasswordReplyKind::kProvide;
  reply.password = value->toStdString();
  return reply;
}

z7::app::ChoiceReply show_choice_prompt_dialog(
    QWidget* parent,
    const z7::app::ChoicePrompt& prompt) {
  z7::app::ChoiceReply reply;
  if (prompt.choices.empty()) {
    reply.kind = z7::app::ChoiceReplyKind::kCancel;
    return reply;
  }

  QMessageBox box(parent);
#ifdef Z7_TESTING
  box.setObjectName(QStringLiteral("choicePromptDialog"));
#endif
  box.setIcon(QMessageBox::Question);

  QString title = QString::fromLocal8Bit(prompt.title.data(),
                                         static_cast<int>(prompt.title.size()));
  if (title.trimmed().isEmpty()) {
    title = QStringLiteral("Choose");
  }
  box.setWindowTitle(title);

  QString message = QString::fromLocal8Bit(prompt.message.data(),
                                           static_cast<int>(prompt.message.size()));
  if (message.trimmed().isEmpty()) {
    message = QStringLiteral("Select an option.");
  }
  box.setText(message);

  std::vector<QPushButton*> choice_buttons;
  choice_buttons.reserve(prompt.choices.size());
  for (const std::string& choice : prompt.choices) {
    choice_buttons.push_back(box.addButton(
        QString::fromLocal8Bit(choice.data(), static_cast<int>(choice.size())),
        QMessageBox::ActionRole));
  }
  QPushButton* cancel_button = box.addButton(QMessageBox::Cancel);

  if (prompt.default_index >= 0 &&
      static_cast<size_t>(prompt.default_index) < choice_buttons.size()) {
    box.setDefaultButton(choice_buttons[static_cast<size_t>(prompt.default_index)]);
  } else if (!choice_buttons.empty()) {
    box.setDefaultButton(choice_buttons.front());
  }

  box.exec();
  const QAbstractButton* clicked = box.clickedButton();
  if (clicked == cancel_button) {
    reply.kind = z7::app::ChoiceReplyKind::kCancel;
    return reply;
  }

  for (size_t i = 0; i < choice_buttons.size(); ++i) {
    if (clicked == choice_buttons[i]) {
      reply.kind = z7::app::ChoiceReplyKind::kSelect;
      reply.selected_index = static_cast<int>(i);
      return reply;
    }
  }
  reply.kind = z7::app::ChoiceReplyKind::kCancel;
  return reply;
}

z7::app::MemoryLimitReply show_memory_limit_prompt_dialog(
    QWidget* parent,
    const z7::app::MemoryLimitPrompt& prompt) {
  z7::app::MemoryLimitReply reply;
  reply.action = z7::app::MemoryLimitAction::kSkipOperation;
  const uint64_t required_bytes =
      prompt.required_usage_bytes != 0 ? prompt.required_usage_bytes
                                       : prompt.estimated_usage_bytes;

  const uint64_t persisted_limit_bytes = load_persisted_memory_limit_bytes(prompt);
  if (persisted_limit_bytes != 0 &&
      required_bytes <= persisted_limit_bytes) {
    reply.action = z7::app::MemoryLimitAction::kUpdateLimitAndContinue;
    reply.updated_limit_bytes = persisted_limit_bytes;
    return reply;
  }

  QString title;
  if (prompt.required_usage_bytes != 0 || !prompt.archive_path.empty()) {
    title = strip_mnemonic(L(3400));
  } else {
    title = strip_mnemonic(L(7600));
    if (title.trimmed().isEmpty()) {
      title = QStringLiteral("Benchmark");
    }
  }

  while (true) {
    QMessageBox box(parent);
#ifdef Z7_TESTING
    box.setObjectName(QStringLiteral("memoryLimitPromptDialog"));
#endif
    box.setIcon(QMessageBox::Warning);
    box.setWindowTitle(title);
    box.setText(memory_limit_dialog_message(prompt, persisted_limit_bytes));
    QPushButton* continue_button = box.addButton(QStringLiteral("Continue Once"),
                                                 QMessageBox::AcceptRole);
#ifdef Z7_TESTING
    continue_button->setObjectName(QStringLiteral("memoryLimitContinueOnceButton"));
#endif
    QPushButton* update_button = box.addButton(QStringLiteral("Update Limit..."),
                                               QMessageBox::ActionRole);
#ifdef Z7_TESTING
    update_button->setObjectName(QStringLiteral("memoryLimitUpdateLimitButton"));
#else
    Q_UNUSED(update_button);
#endif
    QPushButton* skip_button = box.addButton(
        prompt.skip_archive_supported ? QStringLiteral("Skip Archive")
                                      : QStringLiteral("Skip"),
        QMessageBox::RejectRole);
#ifdef Z7_TESTING
    skip_button->setObjectName(QStringLiteral("memoryLimitSkipButton"));
#endif
    QPushButton* cancel_button = box.addButton(QMessageBox::Cancel);
#ifdef Z7_TESTING
    cancel_button->setObjectName(QStringLiteral("memoryLimitCancelButton"));
#endif
    box.setDefaultButton(skip_button);
    box.exec();

    if (box.clickedButton() == continue_button) {
      reply.action = z7::app::MemoryLimitAction::kAllowOnce;
      return reply;
    }
    if (box.clickedButton() == skip_button) {
      reply.action = z7::app::MemoryLimitAction::kSkipOperation;
      return reply;
    }
    if (box.clickedButton() == cancel_button || box.clickedButton() == nullptr) {
      reply.action = z7::app::MemoryLimitAction::kCancelOperation;
      return reply;
    }

    uint64_t updated_limit_bytes = 0;
    const uint64_t seed = persisted_limit_bytes != 0
                              ? persisted_limit_bytes
                              : (prompt.current_limit_defined
                                     ? prompt.current_limit_bytes
                                     : (prompt.safe_limit_defined
                                            ? prompt.safe_limit_bytes
                                            : required_bytes));
    if (!prompt_for_updated_memory_limit_bytes(parent,
                                               title,
                                               required_bytes,
                                               seed,
                                               &updated_limit_bytes)) {
      continue;
    }

    save_persisted_memory_limit_bytes(prompt, updated_limit_bytes);
    reply.action = z7::app::MemoryLimitAction::kUpdateLimitAndContinue;
    reply.updated_limit_bytes = updated_limit_bytes;
    return reply;
  }
}

}  // namespace z7::ui::gui
