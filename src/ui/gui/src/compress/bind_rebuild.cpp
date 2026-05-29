// src/ui/gui/src/compress/bind_rebuild.cpp
// Role: CompressDialog combo rebuild and state recompute methods.

#include "compress_dialog.h"
#include "internal.h"

#include <algorithm>
#include <array>
#include <cstdint>

#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QSignalBlocker>
#include <QThread>

namespace z7::ui::gui {

using namespace compress_internal;

void CompressDialog::populate_from_initial(const CompressCommandOptions& initial) {
  load_archive_path_history();
  keep_archive_name_extension_ = initial.keep_archive_name_extension;
  set_archive_fields_from_path(z7::ui::archive_support::from_native_string(initial.archive_path));

  set_format_combo_data_or_text(format_combo_,
                                initial_or_saved_archive_type(initial));
  set_combo_data_or_text(update_mode_combo_, z7::ui::archive_support::from_native_string(initial.update_mode));
  set_combo_data_or_text(path_mode_combo_, z7::ui::archive_support::from_native_string(initial.path_mode));
  create_sfx_checkbox_->setChecked(initial.create_sfx);
  compress_shared_checkbox_->setChecked(initial.share_for_write);
  delete_after_checkbox_->setChecked(initial.delete_after_compressing);

  recompute_state(false, false, false, false, false);
  apply_persistent_format_options(current_format_id(), &initial);
  set_combo_data_or_text(volume_combo_, z7::ui::archive_support::from_native_string(initial.volume_size));

  password_edit_->setText(z7::ui::archive_support::from_native_string(initial.password));
  reenter_password_edit_->setText(z7::ui::archive_support::from_native_string(initial.password));
  show_password_checkbox_->setChecked(saved_show_password());
  encrypt_headers_checkbox_->setChecked(initial_or_saved_encrypt_headers(initial));

  opaque_add_task_ = initial.opaque_add_task;

  recompute_state(true, true, true, true, true);
  active_format_settings_id_ = current_format_id();
  replace_archive_name_extension_for_current_format();
}

QString CompressDialog::current_format_id() const {
  return normalize_format_id(format_combo_->currentData().toString());
}

void CompressDialog::set_combo_enabled_if_multiple(QComboBox* combo) {
  if (combo == nullptr) {
    return;
  }
  combo->setEnabled(combo->count() > 1);
}

void CompressDialog::recompute_state(bool keep_current_method,
                                     bool keep_current_dictionary,
                                     bool keep_current_word_size,
                                     bool keep_current_solid,
                                     bool keep_current_threads) {
  const bool prev_updating = updating_controls_;
  updating_controls_ = true;

  rebuild_level_combo();
  rebuild_method_combo(keep_current_method);
  rebuild_dictionary_combo(keep_current_dictionary);
  rebuild_word_size_combo(keep_current_word_size);
  rebuild_solid_combo(keep_current_solid);
  rebuild_threads_combo(keep_current_threads);
  update_sfx_controls();
  update_encryption_controls();
  update_memory_visibility();
  update_memory_labels();
  update_password_echo_mode();

  updating_controls_ = prev_updating;
}

void CompressDialog::rebuild_level_combo() {
  const FormatRule rule = rule_for_format_id(current_format_id());
  const QString previous_level = level_combo_->currentData().toString();

  const QSignalBlocker blocker(level_combo_);
  level_combo_->clear();

  const QStringList level_values = level_values_for_mask(rule.levels_mask);
  for (const QString& level : level_values) {
    add_combo_item(level_combo_, level_caption(level), level);
  }

  if (level_combo_->count() == 0) {
    set_combo_enabled_if_multiple(level_combo_);
    return;
  }

  const QString default_level = level_values.contains(QStringLiteral("5"))
                                    ? QStringLiteral("5")
                                    : level_values.front();
  set_combo_data_or_default(level_combo_, previous_level, default_level);
  set_combo_enabled_if_multiple(level_combo_);
}

void CompressDialog::rebuild_method_combo(bool keep_current_method) {
  const FormatRule rule = rule_for_format_id(current_format_id());
  const QString previous_method = method_combo_->currentData().toString();
  const int level = method_level_or_default(level_combo_);

  QString default_method = rule.default_method;
  QStringList methods =
      method_list_for_rule(rule, level, rule.sfx && create_sfx_checkbox_->isChecked());

  const QSignalBlocker blocker(method_combo_);
  method_combo_->clear();
  for (const QString& method : methods) {
    add_combo_item(method_combo_, method, method);
  }

  if (method_combo_->count() == 0) {
    set_combo_enabled_if_multiple(method_combo_);
    return;
  }

  if (default_method.isEmpty()) {
    default_method = methods.front();
  }

  set_combo_data_or_default(method_combo_,
                            keep_current_method ? previous_method : QString(),
                            default_method);
  set_combo_enabled_if_multiple(method_combo_);
}

void CompressDialog::rebuild_dictionary_combo(bool keep_current_dictionary) {
  const QString previous_dictionary = dictionary_combo_->currentData().toString();
  const QString format_id = current_format_id();
  const QString method = method_combo_->currentData().toString();
  const QString effective_method = effective_method_for_format(format_id, method);
  const int level = method_level_or_default(level_combo_);

  const QSignalBlocker blocker(dictionary_combo_);
  dictionary_combo_->clear();

  auto add_dict = [this](uint64_t bytes, bool is_default = false) {
    add_combo_item(dictionary_combo_,
                   dictionary_size_label(bytes, is_default),
                   dictionary_size_data(bytes));
  };

  if (effective_method == QStringLiteral("LZMA") ||
      effective_method == QStringLiteral("LZMA2")) {
    add_dict(lzma_auto_dict_for_level(level), true);
    static const std::array<uint64_t, 23> kLzmaDictionaries = {
        static_cast<uint64_t>(64) << 10,
        static_cast<uint64_t>(256) << 10,
        static_cast<uint64_t>(1) << 20,
        static_cast<uint64_t>(2) << 20,
        static_cast<uint64_t>(3) << 20,
        static_cast<uint64_t>(4) << 20,
        static_cast<uint64_t>(6) << 20,
        static_cast<uint64_t>(8) << 20,
        static_cast<uint64_t>(12) << 20,
        static_cast<uint64_t>(16) << 20,
        static_cast<uint64_t>(24) << 20,
        static_cast<uint64_t>(32) << 20,
        static_cast<uint64_t>(48) << 20,
        static_cast<uint64_t>(64) << 20,
        static_cast<uint64_t>(96) << 20,
        static_cast<uint64_t>(128) << 20,
        static_cast<uint64_t>(192) << 20,
        static_cast<uint64_t>(256) << 20,
        static_cast<uint64_t>(384) << 20,
        static_cast<uint64_t>(512) << 20,
        static_cast<uint64_t>(768) << 20,
        static_cast<uint64_t>(1024) << 20,
        static_cast<uint64_t>(1536) << 20};
    for (uint64_t bytes : kLzmaDictionaries) {
      add_dict(bytes);
    }
  } else if (effective_method == QStringLiteral("PPMd") ||
             effective_method == QStringLiteral("PPMdZip")) {
    const uint64_t auto_dict = static_cast<uint64_t>(1)
                               << (std::clamp(level, 0, 9) + 19);
    add_dict(auto_dict, true);
    if (effective_method == QStringLiteral("PPMdZip")) {
      for (int mb : {1, 2, 4, 8, 16, 32, 64, 128, 256}) {
        add_dict(static_cast<uint64_t>(mb) << 20);
      }
    } else {
      for (int mb : {1, 2, 4, 8, 16, 32, 64, 128, 256, 512}) {
        add_dict(static_cast<uint64_t>(mb) << 20);
      }
    }
  } else if (effective_method == QStringLiteral("Deflate")) {
    add_dict(static_cast<uint64_t>(32) << 10);
  } else if (effective_method == QStringLiteral("Deflate64")) {
    add_dict(static_cast<uint64_t>(64) << 10);
  } else if (effective_method == QStringLiteral("BZip2")) {
    add_dict(bzip2_auto_dict_for_level(level), true);
    for (int kb : {100, 200, 300, 400, 500, 600, 700, 800, 900}) {
      add_dict(static_cast<uint64_t>(kb) << 10);
    }
  } else if (effective_method == QStringLiteral("Copy")) {
    add_dict(0);
  }

  if (dictionary_combo_->count() > 0) {
    set_combo_data_or_default(dictionary_combo_,
                              keep_current_dictionary ? previous_dictionary
                                                      : QString(),
                              QString());
  }
  set_combo_enabled_if_multiple(dictionary_combo_);
}

void CompressDialog::rebuild_word_size_combo(bool keep_current_word_size) {
  const QString previous_order = word_size_combo_->currentData().toString();
  const QString format_id = current_format_id();
  const QString method = method_combo_->currentData().toString();
  const QString effective_method = effective_method_for_format(format_id, method);
  const int level = method_level_or_default(level_combo_);

  const QSignalBlocker blocker(word_size_combo_);
  word_size_combo_->clear();

  auto add_order = [this](int value, bool is_default = false) {
    const QString text = is_default ? QStringLiteral("* %1").arg(value)
                                    : QString::number(value);
    add_combo_item(word_size_combo_, text, QString::number(value));
  };

  if (effective_method == QStringLiteral("LZMA") ||
      effective_method == QStringLiteral("LZMA2")) {
    add_order(level < 7 ? 32 : 64, true);
    for (int v : {16, 24, 32, 48, 64, 96, 128, 192, 256, 273}) {
      add_order(v);
    }
  } else if (effective_method == QStringLiteral("Deflate") ||
             effective_method == QStringLiteral("Deflate64")) {
    int auto_order = 32;
    if (level >= 9) {
      auto_order = 128;
    } else if (level >= 7) {
      auto_order = 64;
    }
    add_order(auto_order, true);
    for (int v : {32,
                  48,
                  64,
                  96,
                  128,
                  192,
                  effective_method == QStringLiteral("Deflate64") ? 257 : 258}) {
      add_order(v);
    }
  } else if (effective_method == QStringLiteral("PPMd")) {
    int auto_order = 4;
    if (level >= 9) {
      auto_order = 32;
    } else if (level >= 7) {
      auto_order = 16;
    } else if (level >= 5) {
      auto_order = 6;
    }
    add_order(auto_order, true);
    for (int v : {2, 3, 4, 5, 6, 8, 12, 16, 24, 32}) {
      add_order(v);
    }
  } else if (effective_method == QStringLiteral("PPMdZip")) {
    add_order(level + 3, true);
    for (int v = 2; v <= 16; ++v) {
      add_order(v);
    }
  }

  if (word_size_combo_->count() > 0) {
    set_combo_data_or_default(word_size_combo_,
                              keep_current_word_size ? previous_order : QString(),
                              QString());
  }
  set_combo_enabled_if_multiple(word_size_combo_);
}

void CompressDialog::rebuild_solid_combo(bool keep_current_solid) {
  const FormatRule rule = rule_for_format_id(current_format_id());
  const QString previous_solid = solid_combo_->currentData().toString();
  const int level = method_level_or_default(level_combo_);

  const QSignalBlocker blocker(solid_combo_);
  solid_combo_->clear();

  if (!rule.solid || level == 0) {
    set_combo_enabled_if_multiple(solid_combo_);
    return;
  }

  const QString method = method_combo_->currentData().toString();
  const QString effective_method = effective_method_for_format(rule.id, method);
  const uint64_t dictionary_size =
      parse_size_to_bytes(dictionary_combo_->currentData().toString());
  const uint64_t auto_solid =
      auto_solid_size_bytes(rule, effective_method, level, dictionary_size);

  if (auto_solid > 0) {
    add_combo_item(solid_combo_,
                   QStringLiteral("* %1").arg(size_label_for_bytes(auto_solid)),
                   size_token_for_bytes(auto_solid));
  }

  if (rule.id == QStringLiteral("7z")) {
    add_combo_item(
        solid_combo_, lang_or(4072), QStringLiteral("off"));
  }

  for (int log_size = 20; log_size <= 36; ++log_size) {
    const uint64_t size = static_cast<uint64_t>(1) << log_size;
    add_combo_item(
        solid_combo_, size_label_for_bytes(size), size_token_for_bytes(size));
  }
  add_combo_item(solid_combo_, lang_or(4073), QStringLiteral("on"));

  set_combo_data_or_default(solid_combo_,
                            keep_current_solid ? previous_solid : QString(),
                            QString());
  set_combo_enabled_if_multiple(solid_combo_);
}


}  // namespace z7::ui::gui
