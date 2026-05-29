// src/ui/gui/src/compress/layout.cpp
// Role: Compress dialog layout and format population.
// This partition is intentionally kept under 1000 lines.

#include "compress_dialog.h"
#include "internal.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QThread>
#include <QVBoxLayout>

#include "app_startup_qt.h"
#include "common/basename_validation.h"
#include "common/archive_type_normalization.h"
#include "compress_format_catalog.h"
#include "custom_localization.h"
#include "official_lang_catalog.h"

namespace z7::ui::gui {

using namespace compress_internal;
using z7::ui::runtime_support::L;
using z7::ui::runtime_support::strip_mnemonic;

namespace {

constexpr char kSfxSuffix[] = "sfx";
constexpr int kFormatKeepNameRole = Qt::UserRole + 1;

struct FormatComboEntry {
  QString id;
  QString display_name;
  bool keep_name = false;
};

int extension_dot_pos(const QString& normalized_path) {
  const int dot_pos = normalized_path.lastIndexOf(QLatin1Char('.'));
  const int separator_pos = normalized_path.lastIndexOf(QLatin1Char('/'));
  return dot_pos > separator_pos + 1 ? dot_pos : -1;
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

QString normalized_archive_name_text(const QComboBox* combo) {
  if (combo == nullptr) {
    return QString();
  }
  return combo->currentText().trimmed();
}

QString archive_path_with_file_name(const QString& dir_prefix,
                                    const QString& file_name) {
  return QDir::toNativeSeparators(dir_prefix + file_name);
}

z7::common::BasenameValidationResult validate_archive_name_text(
    const QString& archive_name) {
  return z7::common::validate_basename_only_name(archive_name.toStdString());
}

}  // namespace

QString CompressDialog::lang_or(uint32_t id) {
  return strip_mnemonic(L(id));
}

CompressDialog::CompressDialog(const CompressCommandOptions& initial,
                               QWidget* parent)
    : QDialog(parent),
      single_file_input_(initial.single_file_input),
      single_file_name_(z7::ui::archive_support::from_native_string(
                            initial.single_file_name)) {
  build_ui();
  populate_from_initial(initial);
  update_memory_labels();
}

bool CompressDialog::is_updating_controls() const {
  return updating_controls_;
}

void CompressDialog::set_archive_fields_from_path(const QString& full_path) {
  const QString normalized_path = QDir::fromNativeSeparators(full_path.trimmed());
  QString file_name;
  QString dir_prefix;
  if (!normalized_path.isEmpty()) {
    const int separator_pos = normalized_path.lastIndexOf(QLatin1Char('/'));
    if (separator_pos >= 0) {
      dir_prefix = normalized_path.left(separator_pos + 1);
      file_name = normalized_path.mid(separator_pos + 1);
    } else {
      file_name = normalized_path;
    }
  }

  archive_dir_prefix_ = dir_prefix;
  if (archive_dir_prefix_label_ != nullptr) {
    archive_dir_prefix_label_->setText(QDir::toNativeSeparators(archive_dir_prefix_));
  }
  if (archive_name_combo_ != nullptr) {
    archive_name_combo_->setEditText(QDir::toNativeSeparators(file_name));
  }
}

bool CompressDialog::is_sfx_enabled() const {
  return create_sfx_checkbox_ != nullptr &&
         create_sfx_checkbox_->isEnabled() &&
         create_sfx_checkbox_->isChecked();
}

QString CompressDialog::current_output_suffix() const {
  if (is_sfx_enabled()) {
    return QString::fromLatin1(kSfxSuffix);
  }

  const std::string suffix =
      z7::common::preferred_archive_output_suffix_copy(current_format_id().toStdString());
  return QString::fromStdString(suffix).trimmed();
}

bool CompressDialog::current_format_keeps_original_name() const {
  if (format_combo_ == nullptr || format_combo_->currentIndex() < 0) {
    return false;
  }
  return format_combo_
      ->itemData(format_combo_->currentIndex(), kFormatKeepNameRole)
      .toBool();
}

void CompressDialog::replace_archive_name_extension_for_current_format() {
  const z7::common::BasenameValidationResult name_result =
      validate_archive_name_text(normalized_archive_name_text(archive_name_combo_));
  if (!name_result.ok) {
    return;
  }

  const QString normalized_path =
      QDir::fromNativeSeparators(compose_archive_path().trimmed());
  const QString suffix = current_output_suffix();
  if (normalized_path.isEmpty() || suffix.isEmpty()) {
    return;
  }

  if (single_file_input_ &&
      current_format_keeps_original_name() &&
      !single_file_name_.isEmpty()) {
    set_archive_fields_from_path(
        archive_path_with_file_name(
            archive_dir_prefix_,
            single_file_name_ + QLatin1Char('.') + suffix));
    generated_archive_extension_ = suffix;
    return;
  }

  QString base_name = normalized_path;
  int dot_pos = extension_dot_pos(base_name);
  if (dot_pos >= 0) {
    const QString existing_suffix = base_name.mid(dot_pos + 1);
    const bool has_generated_suffix =
        !generated_archive_extension_.isEmpty() &&
        existing_suffix.compare(generated_archive_extension_,
                                Qt::CaseInsensitive) == 0;
    if (has_generated_suffix || !keep_archive_name_extension_) {
      base_name = base_name.left(dot_pos);
    }
  }

  if (!keep_archive_name_extension_) {
    dot_pos = extension_dot_pos(base_name);
    if (dot_pos >= 0) {
      base_name = base_name.left(dot_pos);
    }
  }

  set_archive_fields_from_path(base_name + QLatin1Char('.') + suffix);
  generated_archive_extension_ = suffix;
}

QString CompressDialog::archive_name_validation_error() const {
  const z7::common::BasenameValidationResult result =
      validate_archive_name_text(normalized_archive_name_text(archive_name_combo_));
  if (result.ok) {
    return QString();
  }
  return basename_validation_error_message(result.error);
}

QString CompressDialog::compose_archive_path() const {
  if (archive_name_combo_ == nullptr) {
    return QString();
  }

  const QString archive_name =
      QDir::fromNativeSeparators(archive_name_combo_->currentText().trimmed());
  if (archive_name.isEmpty()) {
    return QString();
  }

  if (QDir::isAbsolutePath(archive_name) ||
      archive_name.contains(QLatin1Char('/'))) {
    return QDir::toNativeSeparators(archive_name);
  }

  const QString full_path = archive_dir_prefix_.isEmpty()
                                ? archive_name
                                : archive_dir_prefix_ + archive_name;
  return QDir::toNativeSeparators(full_path);
}

void CompressDialog::build_ui() {
  setWindowTitle(lang_or(4000));
  resize(1160, 760);

  auto* root = new QVBoxLayout(this);

  auto* archive_path_grid = new QGridLayout();
  archive_path_grid->setColumnStretch(1, 1);
  root->addLayout(archive_path_grid);

  auto* archive_label = new QLabel(lang_or(4001), this);
  archive_label->setAlignment(Qt::AlignLeft | Qt::AlignTop);
  archive_path_grid->addWidget(archive_label, 0, 0, 2, 1);

  archive_dir_prefix_label_ = new QLabel(this);
  archive_dir_prefix_label_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  archive_path_grid->addWidget(archive_dir_prefix_label_, 0, 1);

  auto* archive_name_row = new QWidget(this);
  auto* archive_name_row_layout = new QHBoxLayout(archive_name_row);
  archive_name_row_layout->setContentsMargins(0, 0, 0, 0);
  archive_name_combo_ = new QComboBox(archive_name_row);
  archive_name_combo_->setEditable(true);
#ifdef Z7_TESTING
  archive_name_combo_->setObjectName(QStringLiteral("archiveNameCombo"));
#endif
  auto* browse_archive_button = new QPushButton(QStringLiteral("..."), archive_name_row);
  archive_name_row_layout->addWidget(archive_name_combo_, 1);
  archive_name_row_layout->addWidget(browse_archive_button);
  archive_path_grid->addWidget(archive_name_row, 1, 1);

  auto* main_row = new QHBoxLayout();
  root->addLayout(main_row, 1);

  auto* left_panel = new QWidget(this);
  auto* left_layout = new QVBoxLayout(left_panel);
  main_row->addWidget(left_panel, 1);

  auto* archive_grid = new QGridLayout();
  int row = 0;

  auto add_left_combo_row = [&](uint32_t lang_id,
                                QComboBox** combo_out,
                                bool editable = false,
                                const QString& object_name = QString()) {
    auto* label = new QLabel(lang_or(lang_id), left_panel);
    auto* combo = new QComboBox(left_panel);
    combo->setEditable(editable);
#ifdef Z7_TESTING
    if (!object_name.isEmpty()) {
      combo->setObjectName(object_name);
    }
#else
    Q_UNUSED(object_name);
#endif
    archive_grid->addWidget(label, row, 0);
    archive_grid->addWidget(combo, row, 1, 1, 2);
    *combo_out = combo;
    ++row;
  };

  add_left_combo_row(4003,
                     &format_combo_,
                     false,
#ifdef Z7_TESTING
                     QStringLiteral("formatCombo")
#else
                     QString()
#endif
  );
  add_left_combo_row(4004, &level_combo_);
  add_left_combo_row(4005, &method_combo_);
  add_left_combo_row(4006, &dictionary_combo_);
  add_left_combo_row(4007, &word_size_combo_);
  add_left_combo_row(4008, &solid_combo_);

  {
    auto* label =
        new QLabel(lang_or(4009), left_panel);
    auto* thread_row = new QWidget(left_panel);
    auto* thread_row_layout = new QHBoxLayout(thread_row);
    thread_row_layout->setContentsMargins(0, 0, 0, 0);
    threads_combo_ = new QComboBox(thread_row);
    hardware_threads_label_ = new QLabel(thread_row);
    thread_row_layout->addWidget(threads_combo_);
    thread_row_layout->addWidget(hardware_threads_label_);
    thread_row_layout->addStretch(1);
    archive_grid->addWidget(label, row, 0);
    archive_grid->addWidget(thread_row, row, 1, 1, 2);
    ++row;
  }

  {
    compress_memory_title_label_ =
        new QLabel(lang_or(4017), left_panel);
    compress_memory_label_ = new QLabel(left_panel);
    archive_grid->addWidget(compress_memory_title_label_, row, 0);
    archive_grid->addWidget(compress_memory_label_, row, 1, 1, 2);
    ++row;
  }
  {
    decompress_memory_title_label_ =
        new QLabel(lang_or(4018), left_panel);
    decompress_memory_label_ = new QLabel(left_panel);
    archive_grid->addWidget(decompress_memory_title_label_, row, 0);
    archive_grid->addWidget(decompress_memory_label_, row, 1, 1, 2);
    ++row;
  }

  add_left_combo_row(7302,
                     &volume_combo_,
                     true,
#ifdef Z7_TESTING
                     QStringLiteral("volumeCombo")
#else
                     QString()
#endif
  );
  {
    auto* label = new QLabel(lang_or(4010), left_panel);
    parameters_edit_ = new QLineEdit(left_panel);
#ifdef Z7_TESTING
    parameters_edit_->setObjectName(QStringLiteral("compressParametersEdit"));
#endif
    archive_grid->addWidget(label, row, 0);
    archive_grid->addWidget(parameters_edit_, row, 1, 1, 2);
    ++row;
  }

  left_layout->addLayout(archive_grid);

  options_button_ = new QPushButton(lang_or(2100), left_panel);
#ifdef Z7_TESTING
  options_button_->setObjectName(QStringLiteral("compressOptionsButton"));
#endif
  options_button_->setEnabled(true);
  left_layout->addWidget(options_button_, 0, Qt::AlignLeft);
  left_layout->addStretch(1);

  auto* right_panel = new QWidget(this);
  auto* right_layout = new QVBoxLayout(right_panel);
  main_row->addWidget(right_panel, 1);

  auto* right_top_form = new QFormLayout();
  update_mode_combo_ = new QComboBox(right_panel);
  path_mode_combo_ = new QComboBox(right_panel);
#ifdef Z7_TESTING
  update_mode_combo_->setObjectName(QStringLiteral("updateModeCombo"));
  path_mode_combo_->setObjectName(QStringLiteral("pathModeCombo"));
#endif
  right_top_form->addRow(lang_or(4002), update_mode_combo_);
  right_top_form->addRow(lang_or(3410), path_mode_combo_);
  right_layout->addLayout(right_top_form);

  auto* options_group =
      new QGroupBox(lang_or(4011), right_panel);
  auto* options_group_layout = new QVBoxLayout(options_group);
  create_sfx_checkbox_ =
      new QCheckBox(lang_or(4012), options_group);
#ifdef Z7_TESTING
  create_sfx_checkbox_->setObjectName(QStringLiteral("createSfxCheckBox"));
#endif
  compress_shared_checkbox_ =
      new QCheckBox(lang_or(4013), options_group);
  delete_after_checkbox_ =
      new QCheckBox(lang_or(4019),
                    options_group);
  options_group_layout->addWidget(create_sfx_checkbox_);
  options_group_layout->addWidget(compress_shared_checkbox_);
  options_group_layout->addWidget(delete_after_checkbox_);
  right_layout->addWidget(options_group);

  auto* encryption_group =
      new QGroupBox(lang_or(4014), right_panel);
  auto* encryption_layout = new QGridLayout(encryption_group);
  int erow = 0;
  password_label_ = new QLabel(lang_or(3801), encryption_group);
#ifdef Z7_TESTING
  password_label_->setObjectName(QStringLiteral("passwordLabel"));
#endif
  encryption_layout->addWidget(password_label_, erow, 0, 1, 2);
  ++erow;
  password_edit_ = new QLineEdit(encryption_group);
#ifdef Z7_TESTING
  password_edit_->setObjectName(QStringLiteral("passwordEdit"));
#endif
  password_edit_->setEchoMode(QLineEdit::Password);
  encryption_layout->addWidget(password_edit_, erow, 0, 1, 2);
  ++erow;

  reenter_password_label_ =
      new QLabel(lang_or(3802), encryption_group);
#ifdef Z7_TESTING
  reenter_password_label_->setObjectName(QStringLiteral("reenterPasswordLabel"));
#endif
  encryption_layout->addWidget(reenter_password_label_, erow, 0, 1, 2);
  ++erow;
  reenter_password_edit_ = new QLineEdit(encryption_group);
#ifdef Z7_TESTING
  reenter_password_edit_->setObjectName(QStringLiteral("reenterPasswordEdit"));
#endif
  reenter_password_edit_->setEchoMode(QLineEdit::Password);
  encryption_layout->addWidget(reenter_password_edit_, erow, 0, 1, 2);
  ++erow;

  show_password_checkbox_ =
      new QCheckBox(lang_or(3803), encryption_group);
#ifdef Z7_TESTING
  show_password_checkbox_->setObjectName(QStringLiteral("showPasswordCheckBox"));
#endif
  encryption_layout->addWidget(show_password_checkbox_, erow, 0, 1, 2);
  ++erow;

  encryption_method_label_ = new QLabel(lang_or(4015), encryption_group);
#ifdef Z7_TESTING
  encryption_method_label_->setObjectName(
      QStringLiteral("encryptionMethodLabel"));
#endif
  encryption_layout->addWidget(encryption_method_label_, erow, 0);
  encryption_method_combo_ = new QComboBox(encryption_group);
#ifdef Z7_TESTING
  encryption_method_combo_->setObjectName(QStringLiteral("encryptionMethodCombo"));
#endif
  encryption_layout->addWidget(encryption_method_combo_, erow, 1);
  ++erow;

  encrypt_headers_checkbox_ =
      new QCheckBox(lang_or(4016), encryption_group);
#ifdef Z7_TESTING
  encrypt_headers_checkbox_->setObjectName(QStringLiteral("encryptHeadersCheckBox"));
#endif
  encryption_layout->addWidget(encrypt_headers_checkbox_, erow, 0, 1, 2);
  right_layout->addWidget(encryption_group);
  right_layout->addStretch(1);

  error_label_ = new QLabel(this);
#ifdef Z7_TESTING
  error_label_->setObjectName(QStringLiteral("compressErrorLabel"));
#endif
  error_label_->setStyleSheet(QStringLiteral("color: #b00020;"));
  root->addWidget(error_label_);

  auto* buttons = new QDialogButtonBox(
      QDialogButtonBox::Ok | QDialogButtonBox::Cancel, Qt::Horizontal, this);
  ok_button_ = buttons->button(QDialogButtonBox::Ok);
  QPushButton* cancel_button = buttons->button(QDialogButtonBox::Cancel);
  ok_button_->setText(L(401));
  cancel_button->setText(L(402));
  QPushButton* help_button =
      buttons->addButton(lang_or(409), QDialogButtonBox::HelpRole);
#ifdef Z7_TESTING
  help_button->setObjectName(QStringLiteral("compressHelpButton"));
#endif
  help_button->setEnabled(false);
  z7::platform::qt::apply_dialog_button_baseline(buttons);
  connect(buttons, &QDialogButtonBox::accepted, this, &CompressDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  connect(help_button, &QPushButton::clicked, this, &CompressDialog::on_help_clicked);
  root->addWidget(buttons);

  volume_combo_->addItem(QString(), QString());
  volume_combo_->addItem(QStringLiteral("10m"), QStringLiteral("10m"));
  volume_combo_->addItem(QStringLiteral("100m"), QStringLiteral("100m"));
  volume_combo_->addItem(QStringLiteral("700m"), QStringLiteral("700m"));
  volume_combo_->addItem(QStringLiteral("1g"), QStringLiteral("1g"));

  update_mode_combo_->addItem(
      lang_or(4060), QStringLiteral("add"));
  update_mode_combo_->addItem(
      lang_or(4061), QStringLiteral("update"));
  update_mode_combo_->addItem(
      lang_or(4062), QStringLiteral("fresh"));
  update_mode_combo_->addItem(
      lang_or(4063), QStringLiteral("sync"));

  path_mode_combo_->addItem(lang_or(3414),
                            QStringLiteral("relative"));
  path_mode_combo_->addItem(lang_or(3411),
                            QStringLiteral("full"));
  path_mode_combo_->addItem(lang_or(3413),
                            QStringLiteral("absolute"));

  connect(browse_archive_button, &QPushButton::clicked, this, [this]() {
    const QString selected = QFileDialog::getSaveFileName(
        this, lang_or(4070), compose_archive_path());
    if (!selected.isEmpty()) {
      set_archive_fields_from_path(selected);
    }
  });
  connect(archive_name_combo_,
          qOverload<int>(&QComboBox::activated),
          this,
          [this](int index) {
            const QString selected_path =
                archive_name_combo_->itemData(index).toString();
            if (!selected_path.isEmpty()) {
              set_archive_fields_from_path(selected_path);
            }
          });
  connect(options_button_, &QPushButton::clicked, this, &CompressDialog::on_options_clicked);

  connect(format_combo_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) {
    if (!is_updating_controls()) {
      save_current_format_settings();
      recompute_state(false, false, false, false, false);
      apply_persistent_format_options(current_format_id(), nullptr);
      active_format_settings_id_ = current_format_id();
      replace_archive_name_extension_for_current_format();
    }
  });
  connect(level_combo_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) {
    if (!is_updating_controls()) {
      recompute_state(false, false, false, false, false);
    }
  });
  connect(method_combo_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) {
    if (!is_updating_controls()) {
      recompute_state(true, false, false, false, false);
    }
  });
  connect(dictionary_combo_,
          qOverload<int>(&QComboBox::currentIndexChanged),
          this,
          [this](int) {
            if (!is_updating_controls()) {
              recompute_state(true, true, true, false, true);
            }
          });
  connect(threads_combo_,
          qOverload<int>(&QComboBox::currentIndexChanged),
          this,
          [this](int) {
            if (!is_updating_controls()) {
              update_memory_labels();
            }
          });
  connect(create_sfx_checkbox_, &QCheckBox::toggled, this, [this](bool) {
    if (!is_updating_controls()) {
      recompute_state(true, true, true, true, true);
      replace_archive_name_extension_for_current_format();
    }
  });
  connect(show_password_checkbox_, &QCheckBox::toggled, this, [this](bool) {
    update_password_echo_mode();
  });

  populate_format_combo();
  recompute_state(false, false, false, false, false);
}

void CompressDialog::populate_format_combo() {
  const QSignalBlocker blocker(format_combo_);
  format_combo_->clear();

  const std::vector<z7::app::CompressFormatCatalogEntry> catalog =
      z7::app::list_update_archive_formats();

  std::vector<QString> known_ids;
  known_ids.reserve(catalog.size());
  std::vector<FormatComboEntry> entries;
  entries.reserve(catalog.size());

  for (const z7::app::CompressFormatCatalogEntry& entry : catalog) {
    const QString id = normalize_format_id(z7::ui::archive_support::from_native_string(entry.type_id));
    if (id.isEmpty()) {
      continue;
    }
    if (id == QStringLiteral("hash")) {
      continue;
    }
    if (!single_file_input_ && entry.keep_name) {
      continue;
    }
    if (std::find(known_ids.begin(), known_ids.end(), id) != known_ids.end()) {
      continue;
    }

    QString display_name = z7::ui::archive_support::from_native_string(entry.display_name).trimmed();
    if (display_name.isEmpty()) {
      display_name = id;
    }

    entries.push_back({id, display_name, entry.keep_name});
    known_ids.push_back(id);
  }

  std::sort(entries.begin(),
            entries.end(),
            [](const FormatComboEntry& lhs, const FormatComboEntry& rhs) {
              const int display_compare =
                  QString::compare(lhs.display_name,
                                   rhs.display_name,
                                   Qt::CaseInsensitive);
              if (display_compare != 0) {
                return display_compare < 0;
              }
              return lhs.id < rhs.id;
            });

  for (const FormatComboEntry& entry : entries) {
    format_combo_->addItem(entry.display_name, entry.id);
    format_combo_->setItemData(format_combo_->count() - 1,
                               entry.keep_name,
                               kFormatKeepNameRole);
  }

  if (format_combo_->count() > 0) {
    format_combo_->setCurrentIndex(0);
  }
}

}  // namespace z7::ui::gui
