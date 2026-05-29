// src/ui/filemanager/src/main_window/state/state_format.cpp
// Role: Language, size-format, summary, and history helper utilities.

#include "main_window/deps.h"
#include "main_window/internal.h"

#include "archive_format.h"
#include "common/basename_validation.h"

namespace z7::ui::filemanager {
namespace {

Qt::CaseSensitivity copy_history_case_sensitivity() {
  return Qt::CaseInsensitive;
}

bool ends_with_path_separator(const QString& value) {
  return value.endsWith(QLatin1Char('/')) ||
         value.endsWith(QLatin1Char('\\'));
}

bool is_root_path(const QString& path) {
  return QDir::cleanPath(path) == QDir::cleanPath(QDir(path).rootPath());
}

QString append_path_separator(QString path) {
  if (!path.isEmpty() && !path.endsWith(QLatin1Char('/')) &&
      !is_root_path(path)) {
    path += QLatin1Char('/');
  }
  return path;
}

QString clean_path_preserving_double_slash(QString value) {
  const bool preserve_double_slash =
      value.startsWith(QStringLiteral("//")) &&
      !value.startsWith(QStringLiteral("///"));
  QString cleaned = QDir::cleanPath(value);
  if (!preserve_double_slash) {
    return cleaned;
  }
  if (cleaned.startsWith(QStringLiteral("//"))) {
    return cleaned;
  }
  if (cleaned.startsWith(QLatin1Char('/'))) {
    cleaned.prepend(QLatin1Char('/'));
    return cleaned;
  }
  cleaned.prepend(QStringLiteral("//"));
  return cleaned;
}

QString normalize_copy_history_entry(QString value) {
  value = QDir::fromNativeSeparators(value.trimmed());
  if (value.isEmpty()) {
    return QString();
  }
  const bool preserve_trailing_separator = ends_with_path_separator(value);
  QString normalized = clean_path_preserving_double_slash(value);
  if (preserve_trailing_separator) {
    normalized = append_path_separator(normalized);
  }
  return normalized;
}

QString copy_history_compare_key(QString value) {
  value = normalize_copy_history_entry(value);
  while (value.size() > 1 && value.endsWith(QLatin1Char('/'))) {
    value.chop(1);
  }
  return value;
}

QString trim_trailing_dots_and_spaces(QString component) {
  while (!component.isEmpty()) {
    const QChar tail = component.back();
    if (tail != QLatin1Char('.') && tail != QLatin1Char(' ')) {
      break;
    }
    component.chop(1);
  }
  return component;
}

QString path_root_prefix(const QString& path) {
  if (path.startsWith(QStringLiteral("//"))) {
    const QString rest = path.mid(2);
    const QStringList parts = rest.split(QLatin1Char('/'));
    if (parts.size() >= 2 && !parts.at(0).isEmpty() &&
        !parts.at(1).isEmpty()) {
      return QStringLiteral("//%1/%2/").arg(parts.at(0), parts.at(1));
    }
    return QStringLiteral("//");
  }
#ifdef Q_OS_WIN
  if (path.size() >= 3 && path.at(1) == QLatin1Char(':') &&
      path.at(2) == QLatin1Char('/')) {
    return path.left(3);
  }
#endif
  if (path.startsWith(QLatin1Char('/'))) {
    return QStringLiteral("/");
  }
  return QString();
}

QString correct_copy_destination_path(QString raw_destination,
                                      const QString& relative_base,
                                      bool* had_trailing_separator) {
  raw_destination = QDir::fromNativeSeparators(raw_destination);
  if (had_trailing_separator != nullptr) {
    *had_trailing_separator = ends_with_path_separator(raw_destination);
  }
  if (raw_destination.trimmed().isEmpty()) {
    return QString();
  }

  QString absolute = raw_destination;
  if (QDir::isRelativePath(absolute)) {
    absolute = QDir(relative_base).absoluteFilePath(absolute);
  }
  const bool trailing_separator = ends_with_path_separator(absolute);
  absolute = clean_path_preserving_double_slash(absolute);
  if (trailing_separator) {
    absolute = append_path_separator(absolute);
  }

  const QString root = path_root_prefix(absolute);
  QString suffix = absolute.mid(root.size());
  const bool corrected_trailing_separator = suffix.endsWith(QLatin1Char('/'));
  while (suffix.endsWith(QLatin1Char('/'))) {
    suffix.chop(1);
  }

  const QStringList components =
      suffix.split(QLatin1Char('/'), Qt::SkipEmptyParts);
  QString result = root;
  bool check_existing = true;
  for (QString component : components) {
    const QString candidate =
        result.endsWith(QLatin1Char('/')) ? result + component
                                          : result + QLatin1Char('/') + component;
    if (check_existing) {
      const QFileInfo info(candidate);
      if (info.exists()) {
        if (!info.isDir()) {
          return corrected_trailing_separator
                     ? append_path_separator(absolute)
                     : absolute;
        }
      } else {
        check_existing = false;
      }
    }
    if (!check_existing) {
      component = trim_trailing_dots_and_spaces(component);
    }
    if (result.isEmpty() || result.endsWith(QLatin1Char('/'))) {
      result += component;
    } else {
      result += QLatin1Char('/') + component;
    }
  }

  if (corrected_trailing_separator) {
    result = append_path_separator(result);
  }
  return result.isEmpty() ? root : result;
}

QString basename_validation_error_message(
    z7::common::BasenameValidationError error) {
  switch (error) {
    case z7::common::BasenameValidationError::kEmpty:
      return z7::ui::runtime_support::J(
          QStringLiteral("ui.state.basename_validation.empty"));
    case z7::common::BasenameValidationError::kAbsolutePath:
      return z7::ui::runtime_support::J(
          QStringLiteral("ui.state.basename_validation.absolute_path"));
    case z7::common::BasenameValidationError::kDotOrDotDot:
      return z7::ui::runtime_support::J(
          QStringLiteral("ui.state.basename_validation.dot_or_dotdot"));
    case z7::common::BasenameValidationError::kContainsPathSeparator:
      return z7::ui::runtime_support::J(
          QStringLiteral("ui.state.basename_validation.contains_path_separator"));
  }
  return z7::ui::runtime_support::J(
      QStringLiteral("ui.state.basename_validation.invalid"));
}

}  // namespace

QStringList ancestor_paths(const QString& path) {
  QStringList out;
  QDir dir(QDir(path).absolutePath());
  if (!dir.exists()) {
    return out;
  }

  out.prepend(dir.absolutePath());
  while (dir.cdUp()) {
    out.prepend(dir.absolutePath());
  }
  return out;
}

QString lang_or(uint32_t id) {
  return z7::ui::runtime_support::L(id);
}

QStringList normalize_copy_history(QStringList history,
                                   const QString& new_item,
                                   int max_items) {
  QStringList out;
  QStringList keys;
  out.reserve(history.size() + 1);
  keys.reserve(history.size() + 1);

  const QString normalized_new = normalize_copy_history_entry(new_item);
  const QString new_key = copy_history_compare_key(normalized_new);
  if (!normalized_new.isEmpty()) {
    out << normalized_new;
    keys << new_key;
  }

  for (const QString& raw : history) {
    const QString normalized = normalize_copy_history_entry(raw);
    const QString key = copy_history_compare_key(normalized);
    if (normalized.isEmpty() ||
        keys.contains(key, copy_history_case_sensitivity())) {
      continue;
    }
    out << normalized;
    keys << key;
  }

  while (out.size() > max_items) {
    out.removeLast();
  }
  return out;
}

QStringList read_copy_history() {
  z7::platform::qt::PortableSettings settings;
  return normalize_copy_history(
      settings.value(QString::fromLatin1(kSettingsFmCopyToHistory)).toStringList(),
      QString(),
      20);
}

void save_copy_history(const QStringList& history) {
  z7::platform::qt::PortableSettings settings;
  settings.setValue(QString::fromLatin1(kSettingsFmCopyToHistory), history);
  settings.sync();
}

CopyTransferDestinationPlan build_copy_transfer_destination_plan(
    const QString& raw_destination,
    const QString& relative_base,
    int source_count,
    bool force_directory_mode) {
  CopyTransferDestinationPlan plan;
  bool ends_with_separator = false;
  const QString corrected_destination =
      correct_copy_destination_path(raw_destination,
                                    relative_base,
                                    &ends_with_separator);
  if (corrected_destination.isEmpty()) {
    return plan;
  }

  const QFileInfo destination_info(corrected_destination);
  const bool directory_mode =
      force_directory_mode || source_count != 1 || ends_with_separator ||
      (destination_info.exists() && destination_info.isDir());
  if (directory_mode) {
    plan.destination_dir = QDir::cleanPath(corrected_destination);
    plan.history_path = append_path_separator(plan.destination_dir);
    plan.display_path = plan.history_path;
  } else {
    plan.destination_path = QDir::cleanPath(corrected_destination);
    plan.history_path = plan.destination_path;
    plan.display_path = plan.destination_path;
  }
  plan.valid = true;
  return plan;
}

bool ensure_copy_transfer_destination_directories(
    const CopyTransferDestinationPlan& plan) {
  if (!plan.valid) {
    return false;
  }
  const QString directory_to_create =
      plan.destination_path.isEmpty()
          ? plan.destination_dir
          : QFileInfo(plan.destination_path).absolutePath();
  if (directory_to_create.isEmpty()) {
    return false;
  }
  return QDir().mkpath(directory_to_create);
}

bool validate_basename_only_name(const QString& raw_name,
                                 QString* normalized_name,
                                 QString* error_message) {
  if (normalized_name != nullptr) {
    normalized_name->clear();
  }
  if (error_message != nullptr) {
    error_message->clear();
  }

  const QByteArray raw_name_utf8 = raw_name.toUtf8();
  const z7::common::BasenameValidationResult validation =
      z7::common::validate_basename_only_name(
          std::string_view(raw_name_utf8.constData(),
                           static_cast<size_t>(raw_name_utf8.size())));
  if (!validation.ok) {
    if (error_message != nullptr) {
      *error_message = basename_validation_error_message(validation.error);
    }
    return false;
  }

  if (normalized_name != nullptr) {
    *normalized_name = QString::fromUtf8(validation.normalized_name.data(),
                                         static_cast<qsizetype>(
                                             validation.normalized_name.size()));
  }
  return true;
}

CopyMoveDialogResult show_copy_move_dialog(QWidget* parent,
                                           const QString& title,
                                           const QString& prompt,
                                           const QString& info_text,
                                           const QString& initial_path) {
  CopyMoveDialogResult result;
  QDialog dialog(parent);
#ifdef Z7_TESTING
  dialog.setObjectName(QStringLiteral("copyMoveDialog"));
#endif
  dialog.setWindowTitle(title);
  dialog.resize(640, 320);
  dialog.setMinimumSize(420, 180);

  auto* root_layout = new QVBoxLayout(&dialog);
  root_layout->setContentsMargins(8, 8, 8, 8);
  root_layout->setSpacing(6);

  auto* prompt_label = new QLabel(prompt, &dialog);
#ifdef Z7_TESTING
  prompt_label->setObjectName(QStringLiteral("copyMovePromptLabel"));
#endif
  root_layout->addWidget(prompt_label);

  auto* row_widget = new QWidget(&dialog);
  auto* row_layout = new QHBoxLayout(row_widget);
  row_layout->setContentsMargins(0, 0, 0, 0);

  auto* destination_combo = new QComboBox(row_widget);
#ifdef Z7_TESTING
  destination_combo->setObjectName(QStringLiteral("copyMoveDestinationCombo"));
#endif
  destination_combo->setEditable(true);
  destination_combo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  const QStringList history = read_copy_history();
  for (const QString& entry : history) {
    if (!entry.trimmed().isEmpty()) {
      destination_combo->addItem(QDir::toNativeSeparators(entry));
    }
  }
  destination_combo->setEditText(QDir::toNativeSeparators(initial_path));

  auto* browse_button = new QPushButton(QStringLiteral("..."), row_widget);
#ifdef Z7_TESTING
  browse_button->setObjectName(QStringLiteral("copyMoveBrowseButton"));
#endif
  browse_button->setMinimumWidth(48);
  browse_button->setMaximumWidth(56);
  row_layout->addWidget(destination_combo, 1);
  row_layout->addWidget(browse_button);
  root_layout->addWidget(row_widget);

  if (!info_text.trimmed().isEmpty()) {
    auto* info_label = new QLabel(info_text, &dialog);
#ifdef Z7_TESTING
    info_label->setObjectName(QStringLiteral("copyMoveInfoLabel"));
#endif
    info_label->setWordWrap(false);
    info_label->setTextInteractionFlags(Qt::NoTextInteraction);
    info_label->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    info_label->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    root_layout->addWidget(info_label, 1);
  } else {
    root_layout->addStretch(1);
  }

  auto* buttons_row = new QWidget(&dialog);
  auto* buttons_layout = new QHBoxLayout(buttons_row);
  buttons_layout->setContentsMargins(0, 0, 0, 0);
  buttons_layout->addStretch(1);
  auto* ok_button = new QPushButton(z7::ui::runtime_support::L(401), buttons_row);
#ifdef Z7_TESTING
  ok_button->setObjectName(QStringLiteral("copyMoveOkButton"));
#endif
  auto* cancel_button = new QPushButton(z7::ui::runtime_support::L(402), buttons_row);
#ifdef Z7_TESTING
  cancel_button->setObjectName(QStringLiteral("copyMoveCancelButton"));
#endif
  ok_button->setMinimumWidth(92);
  cancel_button->setMinimumWidth(92);
  buttons_layout->addWidget(ok_button);
  buttons_layout->addWidget(cancel_button);
  root_layout->addWidget(buttons_row);

  QObject::connect(ok_button, &QPushButton::clicked, &dialog, &QDialog::accept);
  QObject::connect(cancel_button, &QPushButton::clicked, &dialog, &QDialog::reject);
  QObject::connect(browse_button, &QPushButton::clicked, &dialog, [&dialog, destination_combo]() {
    const QString current = QDir::fromNativeSeparators(
        destination_combo->currentText().trimmed());
    QString browse_base = current;
    if (!browse_base.isEmpty()) {
      const QFileInfo info(browse_base);
      if (!info.isDir()) {
        browse_base = info.absolutePath();
      }
    }
    if (browse_base.isEmpty()) {
      browse_base = QDir::homePath();
    }
    const QString selected = QFileDialog::getExistingDirectory(
        &dialog, z7::ui::runtime_support::L(4070), browse_base, QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (!selected.isEmpty()) {
      QString normalized = QDir::fromNativeSeparators(selected.trimmed());
      if (!normalized.isEmpty() && !normalized.endsWith(QLatin1Char('/'))) {
        normalized += QLatin1Char('/');
      }
      destination_combo->setEditText(QDir::toNativeSeparators(normalized));
    }
  });

  ok_button->setDefault(true);
  ok_button->setAutoDefault(true);

  if (dialog.exec() != QDialog::Accepted) {
    return result;
  }

  const QString destination =
      QDir::fromNativeSeparators(destination_combo->currentText());
  if (destination.trimmed().isEmpty()) {
    return result;
  }

  result.accepted = true;
  result.destination_path = destination;
  return result;
}

}  // namespace z7::ui::filemanager
