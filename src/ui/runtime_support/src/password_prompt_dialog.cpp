// src/ui/runtime_support/src/password_prompt_dialog.cpp
// Role: Original-style password prompt dialog shared by GUI task flows.

#include "password_prompt_dialog.h"

#include <QApplication>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
#include <QVariant>

#include "archive_string_codec_qt.h"
#include "custom_localization.h"
#include "official_lang_catalog.h"
#include "portable_settings.h"

namespace z7::ui::runtime_support {
namespace {

constexpr auto kSettingsPasswordPromptShowPassword =
    "Gui/PasswordPrompt/ShowPassword";
constexpr int kDialogWidth = 360;
constexpr int kDialogHeight = 150;
constexpr int kWrongPasswordDialogHeight = 184;

QString localized(uint32_t id) {
  return strip_mnemonic(L(id));
}

bool load_show_password() {
  z7::platform::qt::PortableSettings settings;
  return settings
      .value(QString::fromLatin1(kSettingsPasswordPromptShowPassword), false)
      .toBool();
}

void save_show_password(bool show_password) {
  z7::platform::qt::PortableSettings settings;
  settings.setValue(QString::fromLatin1(kSettingsPasswordPromptShowPassword),
                    show_password);
}

void center_dialog_on_parent(QDialog* dialog, QWidget* parent) {
  if (dialog == nullptr || parent == nullptr) {
    return;
  }
  const QRect parent_geometry = parent->frameGeometry();
  if (!parent_geometry.isValid()) {
    return;
  }
  QRect dialog_geometry = dialog->frameGeometry();
  dialog_geometry.moveCenter(parent_geometry.center());
  dialog->move(dialog_geometry.topLeft());
}

}  // namespace

std::optional<QString> show_password_prompt_dialog(
    QWidget* parent,
    const z7::app::PasswordPrompt& prompt) {
  if (parent == nullptr) {
    parent = QApplication::activeModalWidget();
  }
  if (parent == nullptr) {
    parent = QApplication::activeWindow();
  }

  QDialog dialog(parent);
#ifdef Z7_TESTING
  dialog.setObjectName(QStringLiteral("passwordPromptDialog"));
#endif
  dialog.setWindowTitle(localized(3800));
  const bool wrong_password =
      prompt.reason_kind == z7::app::PasswordPromptReason::kWrongPassword;
  dialog.setFixedSize(
      kDialogWidth, wrong_password ? kWrongPasswordDialogHeight : kDialogHeight);
  const QString archive_path =
      z7::ui::archive_support::from_native_string(prompt.archive_path);
  if (!archive_path.trimmed().isEmpty()) {
    dialog.setToolTip(archive_path.trimmed());
  }

  auto* layout = new QVBoxLayout(&dialog);
  layout->setContentsMargins(16, 12, 16, 12);
  layout->setSpacing(8);

  auto* label = new QLabel(localized(3801), &dialog);
#ifdef Z7_TESTING
  label->setObjectName(QStringLiteral("passwordPromptLabel"));
#endif
  layout->addWidget(label);

  if (wrong_password) {
    auto* warning = new QLabel(
        QStringLiteral("%1 %2")
            .arg(J(QStringLiteral("ui.password_prompt.wrong_password_title")),
                 J(QStringLiteral("ui.password_prompt.wrong_password_retry"))),
        &dialog);
#ifdef Z7_TESTING
    warning->setObjectName(QStringLiteral("passwordPromptWrongPasswordLabel"));
#endif
    warning->setWordWrap(true);
    warning->setStyleSheet(QStringLiteral("color: #b00020; font-weight: 600;"));
    layout->addWidget(warning);
  }

  auto* edit = new QLineEdit(&dialog);
#ifdef Z7_TESTING
  edit->setObjectName(QStringLiteral("passwordPromptEdit"));
#endif
  edit->setEchoMode(QLineEdit::Password);
  layout->addWidget(edit);

  auto* show_password = new QCheckBox(localized(3803), &dialog);
#ifdef Z7_TESTING
  show_password->setObjectName(QStringLiteral("passwordPromptShowPasswordCheckBox"));
#endif
  show_password->setChecked(load_show_password());
  layout->addWidget(show_password);

  const auto update_echo_mode = [edit, show_password]() {
    edit->setEchoMode(show_password->isChecked() ? QLineEdit::Normal
                                                 : QLineEdit::Password);
  };
  update_echo_mode();
  QObject::connect(show_password, &QCheckBox::toggled, &dialog, update_echo_mode);

  auto* buttons = new QDialogButtonBox(
      QDialogButtonBox::Ok | QDialogButtonBox::Cancel, Qt::Horizontal, &dialog);
  if (QPushButton* ok_button = buttons->button(QDialogButtonBox::Ok)) {
    ok_button->setText(L(401));
    ok_button->setDefault(true);
  }
  if (QPushButton* cancel_button = buttons->button(QDialogButtonBox::Cancel)) {
    cancel_button->setText(L(402));
  }
  QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  layout->addStretch(1);
  layout->addWidget(buttons);

  center_dialog_on_parent(&dialog, parent);
  edit->setFocus(Qt::OtherFocusReason);
  QPointer<QWidget> guarded_parent(parent);
  QTimer::singleShot(0, &dialog, [&dialog, guarded_parent]() {
    center_dialog_on_parent(&dialog, guarded_parent.data());
    dialog.raise();
    dialog.activateWindow();
  });
  dialog.raise();
  dialog.activateWindow();
  const int result = dialog.exec();
  save_show_password(show_password->isChecked());
  if (result != QDialog::Accepted) {
    return std::nullopt;
  }
  return edit->text();
}

}  // namespace z7::ui::runtime_support
