// src/ui/gui/src/compress/bind.cpp
// Role: Compress dialog state binding and output options.
// This partition is intentionally kept under 1000 lines.

#include "compress_dialog.h"
#include "internal.h"
#include "archive_session.h"

#include <algorithm>
#include <array>
#include <cstdint>

#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QProcess>
#include <QSignalBlocker>
#include <QThread>

namespace z7::ui::gui {

using namespace compress_internal;

void CompressDialog::rebuild_threads_combo(bool keep_current_threads) {
  const FormatRule rule = rule_for_format_id(current_format_id());
  const QString previous_threads = threads_combo_->currentData().toString();

  const QSignalBlocker blocker(threads_combo_);
  threads_combo_->clear();

  if (!rule.multi_thread) {
    set_combo_enabled_if_multiple(threads_combo_);
    return;
  }

  const QString method = method_combo_->currentData().toString();
  const int hardware_threads = std::max(1, QThread::idealThreadCount());

  int method_threads_max = hardware_threads * 2;
  if (rule.id == QStringLiteral("zip")) {
    method_threads_max = sizeof(size_t) > 4 ? 128 : 32;
  } else if (rule.id == QStringLiteral("xz") || method == QStringLiteral("LZMA2")) {
    method_threads_max = 512;
  } else if (method == QStringLiteral("LZMA")) {
    method_threads_max = 2;
  } else if (method == QStringLiteral("BZip2")) {
    method_threads_max = 64;
  } else if (method == QStringLiteral("Copy") || method == QStringLiteral("PPMd") ||
             method == QStringLiteral("PPMdZip") || method == QStringLiteral("Deflate") ||
             method == QStringLiteral("Deflate64") || method == QStringLiteral("GNU") ||
             method == QStringLiteral("POSIX") || method == QStringLiteral("SHA256") ||
             method == QStringLiteral("SHA1")) {
    method_threads_max = 1;
  }

  method_threads_max = std::max(1, method_threads_max);
  const int auto_threads = std::clamp(hardware_threads, 1, method_threads_max);
  add_combo_item(threads_combo_,
                 QStringLiteral("* Auto (%1)").arg(auto_threads),
                 QStringLiteral("auto"));

  if (method_threads_max != auto_threads || auto_threads != 1) {
    const int max_explicit = std::min(method_threads_max, hardware_threads * 2);
    for (int t = 1; t <= max_explicit; ++t) {
      add_combo_item(threads_combo_, QString::number(t), QString::number(t));
    }
  }

  set_combo_data_or_default(threads_combo_,
                            keep_current_threads ? previous_threads : QString(),
                            QStringLiteral("auto"));
  set_combo_enabled_if_multiple(threads_combo_);
}

void CompressDialog::update_sfx_controls() {
  const FormatRule rule = rule_for_format_id(current_format_id());

  bool enabled = rule.sfx;
  if (enabled) {
    const QString method = method_combo_->currentData().toString();
    enabled = method.isEmpty() || supports_sfx_method(method);
  }

  if (!enabled && create_sfx_checkbox_->isChecked()) {
    create_sfx_checkbox_->setChecked(false);
  }
  create_sfx_checkbox_->setEnabled(enabled);
}

void CompressDialog::update_encryption_controls() {
  const FormatRule rule = rule_for_format_id(current_format_id());
  const QString previous_method = encryption_method_combo_->currentData().toString();

  const bool encrypt = rule.encrypt;
  password_label_->setEnabled(encrypt);
  password_edit_->setEnabled(encrypt);
  reenter_password_label_->setEnabled(encrypt);
  reenter_password_edit_->setEnabled(encrypt);
  show_password_checkbox_->setEnabled(encrypt);
  encryption_method_label_->setEnabled(encrypt);

  {
    const QSignalBlocker blocker(encryption_method_combo_);
    encryption_method_combo_->clear();
    if (rule.id == QStringLiteral("7z")) {
      add_combo_item(encryption_method_combo_, QStringLiteral("AES-256"), QStringLiteral("AES-256"));
      set_combo_data_or_default(encryption_method_combo_,
                                previous_method,
                                QStringLiteral("AES-256"));
    } else if (rule.id == QStringLiteral("zip")) {
      add_combo_item(encryption_method_combo_,
                     QStringLiteral("ZipCrypto"),
                     QStringLiteral("ZipCrypto"));
      add_combo_item(encryption_method_combo_,
                     QStringLiteral("AES-256"),
                     QStringLiteral("AES-256"));
      set_combo_data_or_default(encryption_method_combo_,
                                previous_method,
                                QStringLiteral("ZipCrypto"));
    }
    encryption_method_combo_->setEnabled(encrypt && encryption_method_combo_->count() > 0);
  }

  encrypt_headers_checkbox_->setVisible(rule.encrypt_file_names);
  encrypt_headers_checkbox_->setEnabled(encrypt && rule.encrypt_file_names);
  if (!rule.encrypt_file_names) {
    encrypt_headers_checkbox_->setChecked(false);
  }
}

void CompressDialog::update_memory_visibility() {
  const FormatRule rule = rule_for_format_id(current_format_id());
  const bool visible = rule.mem_use;
  compress_memory_title_label_->setVisible(visible);
  compress_memory_label_->setVisible(visible);
  decompress_memory_title_label_->setVisible(visible);
  decompress_memory_label_->setVisible(visible);
}

void CompressDialog::update_memory_labels() {
  z7::app::AddRequest request;
  request.format = z7::ui::archive_support::to_native_string(current_format_id());
  request.compression_level = z7::ui::archive_support::to_native_string(level_combo_->currentData().toString());
  request.method_value = z7::ui::archive_support::to_native_string(method_combo_->currentData().toString());
  request.dictionary_size = z7::ui::archive_support::to_native_string(dictionary_combo_->currentData().toString());
  request.word_size = z7::ui::archive_support::to_native_string(word_size_combo_->currentData().toString());
  request.solid_block_size = z7::ui::archive_support::to_native_string(solid_combo_->currentData().toString());
  request.thread_count = z7::ui::archive_support::to_native_string(threads_combo_->currentData().toString());

  const QStringList params = QProcess::splitCommand(parameters_edit_->text().trimmed());
  request.extra_parameters.reserve(static_cast<size_t>(params.size()));
  for (const QString& token : params) {
    request.extra_parameters.push_back(z7::ui::archive_support::to_native_string(token));
  }

  const z7::app::CompressionResourcesEstimate estimate =
      z7::app::estimate_compression_resources(request);

  if (estimate.ok) {
    compress_memory_label_->setText(format_bytes(estimate.compression_usage_bytes));
    decompress_memory_label_->setText(format_bytes(estimate.decompression_usage_bytes));
  } else {
    compress_memory_label_->setText(QStringLiteral("?"));
    decompress_memory_label_->setText(QStringLiteral("?"));
  }
  hardware_threads_label_->setText(
      QStringLiteral("/ %1").arg(std::max(1, QThread::idealThreadCount())));
}

void CompressDialog::update_password_echo_mode() {
  const bool show_password = show_password_checkbox_->isChecked();
  const bool show_reenter = !show_password;

  const QLineEdit::EchoMode mode =
      show_password ? QLineEdit::Normal : QLineEdit::Password;
  password_edit_->setEchoMode(mode);
  reenter_password_edit_->setEchoMode(mode);

  if (reenter_password_label_ != nullptr) {
    reenter_password_label_->setVisible(show_reenter);
  }
  reenter_password_edit_->setVisible(show_reenter);
}

void CompressDialog::accept() {
  const QString p1 = password_edit_->text();
  const QString p2 = reenter_password_edit_->text();
  const bool enforce_reenter = show_password_checkbox_->isEnabled() &&
                               !show_password_checkbox_->isChecked();
  if (enforce_reenter && (!p1.isEmpty() || !p2.isEmpty()) && p1 != p2) {
    error_label_->setText(lang_or(3804));
    return;
  }

  const QString archive_name_error = archive_name_validation_error();
  if (!archive_name_error.isEmpty()) {
    error_label_->setText(archive_name_error);
    return;
  }

  error_label_->clear();
  save_persistent_settings();
  QDialog::accept();
}

CompressCommandOptions CompressDialog::options() const {
  CompressCommandOptions out;
  out.archive_path = z7::ui::archive_support::to_native_string(compose_archive_path().trimmed());
  out.archive_type = z7::ui::archive_support::to_native_string(current_format_id());
  out.keep_archive_name_extension = keep_archive_name_extension_;
  out.single_file_input = single_file_input_;
  out.single_file_name =
      z7::ui::archive_support::to_native_string(single_file_name_);
  out.update_mode = z7::ui::archive_support::to_native_string(update_mode_combo_->currentData().toString());
  out.path_mode = z7::ui::archive_support::to_native_string(path_mode_combo_->currentData().toString());
  out.create_sfx = create_sfx_checkbox_->isEnabled() && create_sfx_checkbox_->isChecked();
  out.share_for_write = compress_shared_checkbox_->isChecked();
  out.delete_after_compressing = delete_after_checkbox_->isChecked();

  out.compression_level = z7::ui::archive_support::to_native_string(level_combo_->currentData().toString());
  if (method_combo_->isEnabled()) {
    out.method = z7::ui::archive_support::to_native_string(method_combo_->currentData().toString());
  }
  if (dictionary_combo_->isEnabled()) {
    out.dictionary_size = z7::ui::archive_support::to_native_string(dictionary_combo_->currentData().toString());
  }
  if (word_size_combo_->isEnabled()) {
    out.word_size = z7::ui::archive_support::to_native_string(word_size_combo_->currentData().toString());
  }
  if (solid_combo_->isEnabled()) {
    out.solid_block_size = z7::ui::archive_support::to_native_string(solid_combo_->currentData().toString());
  }
  if (threads_combo_->isEnabled()) {
    out.thread_count = z7::ui::archive_support::to_native_string(threads_combo_->currentData().toString());
  }
  out.volume_size = z7::ui::archive_support::to_native_string(volume_combo_->currentText().trimmed());

  const FormatRule rule = rule_for_format_id(current_format_id());
  const QStringList params = QProcess::splitCommand(parameters_edit_->text().trimmed());
  out.extra_parameters.reserve(static_cast<size_t>(params.size()));
  for (const QString& token : params) {
    out.extra_parameters.push_back(z7::ui::archive_support::to_native_string(token));
  }

  if (rule.encrypt) {
    out.password = z7::ui::archive_support::to_native_string(password_edit_->text());
    if (!out.password.empty() && encryption_method_combo_->count() > 0) {
      out.encryption_method =
          z7::ui::archive_support::to_native_string(selected_encryption_method_spec());
    }
    out.encrypt_headers = !out.password.empty() &&
                          rule.encrypt_file_names &&
                          encrypt_headers_checkbox_->isChecked();
  }
  out.opaque_add_task = opaque_add_task_;
  return out;
}

}  // namespace z7::ui::gui

// End of bind.cpp
