// src/ui/gui/src/compress/options.cpp
// Role: CompressDialog options/help actions and advanced options popup.

#include "compress_dialog.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

#include "app_startup_qt.h"
#include "archive_string_codec_qt.h"
#include "internal.h"
#include "official_lang_catalog.h"
#include "path_history_utils.h"
#include "platform_support.h"
#include "portable_settings.h"

namespace z7::ui::gui {

using namespace compress_internal;
using z7::ui::runtime_support::L;

namespace {

constexpr int kCompressHistoryMax = 20;
constexpr auto kSettingsCompressArchiver = "Compression/Archiver";
constexpr auto kSettingsCompressArchiveHistory = "Compression/ArcHistory";
constexpr auto kSettingsCompressShowPassword = "Compression/ShowPassword";
constexpr auto kSettingsCompressEncryptHeaders = "Compression/EncryptHeaders";
constexpr auto kSettingsCompressLevel = "Compression/Level";
constexpr auto kSettingsCompressSecurity = "Compression/Security";
constexpr auto kSettingsCompressAltStreams = "Compression/AltStreams";
constexpr auto kSettingsCompressHardLinks = "Compression/HardLinks";
constexpr auto kSettingsCompressSymLinks = "Compression/SymLinks";
constexpr auto kSettingsCompressPreserveATime = "Compression/PreserveATime";
constexpr auto kFormatSettingsPrefix = "Compression/Options/";
constexpr auto kFormatLevel = "Level";
constexpr auto kFormatMethod = "Method";
constexpr auto kFormatDictionary = "Dictionary";
constexpr auto kFormatOrder = "Order";
constexpr auto kFormatBlockSize = "BlockSize";
constexpr auto kFormatNumThreads = "NumThreads";
constexpr auto kFormatEncryptionMethod = "EncryptionMethod";
constexpr auto kFormatOptions = "Options";
constexpr auto kFormatMemUse = sizeof(void*) == 4 ? "MemUse32" : "MemUse64";
constexpr auto kFormatTimePrec = "TimePrec";
constexpr auto kFormatMTime = "MTime";
constexpr auto kFormatATime = "ATime";
constexpr auto kFormatCTime = "CTime";
constexpr auto kFormatSetArcMTime = "SetArcMTime";

QString settings_key(const char* key) {
  return QString::fromLatin1(key);
}

QString format_settings_key(const QString& format_id, const char* leaf) {
  const QString normalized = normalize_format_id(format_id);
  if (normalized.isEmpty()) {
    return QString();
  }
  return QString::fromLatin1(kFormatSettingsPrefix) + normalized +
         QLatin1Char('/') + QString::fromLatin1(leaf);
}

QString saved_string(const z7::platform::qt::PortableSettings& settings,
                     const QString& key) {
  if (key.isEmpty() || !settings.contains(key)) {
    return QString();
  }
  return settings.value(key).toString().trimmed();
}

void set_string_or_remove(z7::platform::qt::PortableSettings* settings,
                          const QString& key,
                          const QString& value) {
  if (settings == nullptr || key.isEmpty()) {
    return;
  }
  const QString trimmed = value.trimmed();
  if (trimmed.isEmpty()) {
    settings->remove(key);
    return;
  }
  settings->setValue(key, trimmed);
}

QString explicit_or_saved_string(
    const z7::platform::qt::PortableSettings& settings,
    const QString& key,
    const CompressCommandOptions* explicit_options,
    const std::string CompressCommandOptions::*member) {
  if (explicit_options != nullptr) {
    const QString explicit_value =
        z7::ui::archive_support::from_native_string(
            (*explicit_options).*member)
            .trimmed();
    if (!explicit_value.isEmpty()) {
      return explicit_value;
    }
  }
  return saved_string(settings, key);
}

QString joined_extra_parameters(
    const std::vector<std::string>& extra_parameters) {
  QString joined;
  for (const std::string& token : extra_parameters) {
    const QString value =
        z7::ui::archive_support::from_native_string(token).trimmed();
    if (value.isEmpty()) {
      continue;
    }
    if (!joined.isEmpty()) {
      joined += QLatin1Char(' ');
    }
    joined += value;
  }
  return joined;
}

QString current_combo_data(const QComboBox* combo) {
  if (combo == nullptr) {
    return QString();
  }
  return combo->currentData().toString().trimmed();
}

QString default_encryption_method_for_format(const QString& format_id) {
  const QString normalized = normalize_format_id(format_id);
  if (normalized == QStringLiteral("7z")) {
    return QStringLiteral("AES-256");
  }
  if (normalized == QStringLiteral("zip")) {
    return QStringLiteral("ZipCrypto");
  }
  return QString();
}

QStringList normalized_archive_history(QStringList history,
                                       const QString& new_item = QString()) {
  return z7::ui::common::normalized_path_history(history,
                                                 new_item,
                                                 kCompressHistoryMax);
}

QString normalize_advanced_options_text(const QString& text) {
  QString normalized = text;
  normalized.replace(QLatin1Char('\r'), QLatin1Char(' '));
  normalized.replace(QLatin1Char('\n'), QLatin1Char(' '));
  return normalized.simplified();
}

QString canonical_switch_token(QString token) {
  return token.trimmed().toLower();
}

bool is_bool_switch_token(const QString& token, const QString& short_name) {
  const QString lower = canonical_switch_token(token);
  const QString prefix = QStringLiteral("-%1").arg(short_name.toLower());
  if (!lower.startsWith(prefix)) {
    return false;
  }
  if (lower.size() == prefix.size()) {
    return true;
  }
  const QChar next = lower.at(prefix.size());
  return next == QLatin1Char('-') || next == QLatin1Char('=');
}

bool bool_switch_enabled(const QString& token, const QString& short_name) {
  const QString lower = canonical_switch_token(token);
  const QString prefix = QStringLiteral("-%1").arg(short_name.toLower());
  if (!lower.startsWith(prefix)) {
    return false;
  }

  if (lower.size() == prefix.size()) {
    return true;
  }

  const QString suffix = lower.mid(prefix.size());
  if (suffix == QStringLiteral("-")) {
    return false;
  }
  if (suffix.startsWith(QLatin1Char('='))) {
    const QString value = suffix.mid(1).trimmed();
    if (value == QStringLiteral("off") || value == QStringLiteral("0") ||
        value == QStringLiteral("no") || value == QStringLiteral("false")) {
      return false;
    }
    return true;
  }

  return true;
}

bool has_enabled_switch(const QStringList& tokens, const QString& short_name) {
  bool found = false;
  bool enabled = false;
  for (const QString& token : tokens) {
    if (!is_bool_switch_token(token, short_name)) {
      continue;
    }
    found = true;
    enabled = bool_switch_enabled(token, short_name);
  }
  return found && enabled;
}

void remove_switch_tokens(QStringList* tokens, const QString& short_name) {
  if (tokens == nullptr) {
    return;
  }
  tokens->erase(
      std::remove_if(tokens->begin(), tokens->end(), [&short_name](const QString& token) {
        return is_bool_switch_token(token, short_name);
      }),
      tokens->end());
}

void set_bool_switch(QStringList* tokens, const QString& short_name, bool enabled) {
  if (tokens == nullptr) {
    return;
  }
  remove_switch_tokens(tokens, short_name);
  if (enabled) {
    tokens->append(QStringLiteral("-%1").arg(short_name));
  }
}

enum class SwitchState {
  kAuto,
  kEnabled,
  kDisabled
};

SwitchState switch_state_from_tokens(const QStringList& tokens,
                                     const QString& short_name) {
  SwitchState state = SwitchState::kAuto;
  for (const QString& token : tokens) {
    if (!is_bool_switch_token(token, short_name)) {
      continue;
    }
    state = bool_switch_enabled(token, short_name) ? SwitchState::kEnabled
                                                    : SwitchState::kDisabled;
  }
  return state;
}

void set_switch_state(QStringList* tokens,
                      const QString& short_name,
                      SwitchState state) {
  if (tokens == nullptr) {
    return;
  }
  remove_switch_tokens(tokens, short_name);
  if (state == SwitchState::kEnabled) {
    tokens->append(QStringLiteral("-%1").arg(short_name));
  } else if (state == SwitchState::kDisabled) {
    tokens->append(QStringLiteral("-%1-").arg(short_name));
  }
}

bool is_presence_switch_token(const QString& token, const QString& short_name) {
  return canonical_switch_token(token) ==
         QStringLiteral("-%1").arg(short_name.toLower());
}

bool has_presence_switch(const QStringList& tokens, const QString& short_name) {
  return std::any_of(tokens.cbegin(), tokens.cend(), [&short_name](const QString& token) {
    return is_presence_switch_token(token, short_name);
  });
}

void remove_presence_switch_tokens(QStringList* tokens, const QString& short_name) {
  if (tokens == nullptr) {
    return;
  }
  tokens->erase(
      std::remove_if(tokens->begin(), tokens->end(), [&short_name](const QString& token) {
        return is_presence_switch_token(token, short_name);
      }),
      tokens->end());
}

void set_presence_switch(QStringList* tokens, const QString& short_name, bool enabled) {
  if (tokens == nullptr) {
    return;
  }
  remove_presence_switch_tokens(tokens, short_name);
  if (enabled) {
    tokens->append(QStringLiteral("-%1").arg(short_name));
  }
}

void set_checkbox_switch_state(QCheckBox* checkbox, SwitchState state) {
  if (checkbox == nullptr) {
    return;
  }
  checkbox->setTristate(true);
  if (state == SwitchState::kEnabled) {
    checkbox->setCheckState(Qt::Checked);
  } else if (state == SwitchState::kDisabled) {
    checkbox->setCheckState(Qt::Unchecked);
  } else {
    checkbox->setCheckState(Qt::PartiallyChecked);
  }
}

SwitchState checkbox_switch_state(const QCheckBox* checkbox) {
  if (checkbox == nullptr) {
    return SwitchState::kAuto;
  }
  if (checkbox->checkState() == Qt::Checked) {
    return SwitchState::kEnabled;
  }
  if (checkbox->checkState() == Qt::Unchecked) {
    return SwitchState::kDisabled;
  }
  return SwitchState::kAuto;
}

QString join_tokens(const QStringList& tokens) {
  QStringList normalized;
  normalized.reserve(tokens.size());
  for (const QString& token : tokens) {
    const QString trimmed = token.trimmed();
    if (!trimmed.isEmpty()) {
      normalized.push_back(trimmed);
    }
  }
  return normalized.join(QLatin1Char(' '));
}

QStringList normalized_advanced_tokens(const QString& text) {
  return QProcess::splitCommand(normalize_advanced_options_text(text));
}

void save_bool_switch_setting(z7::platform::qt::PortableSettings* settings,
                              const QString& key,
                              const QStringList& tokens,
                              const QString& short_name) {
  if (settings == nullptr || key.isEmpty()) {
    return;
  }

  bool found = false;
  bool enabled = false;
  for (const QString& token : tokens) {
    if (!is_bool_switch_token(token, short_name)) {
      continue;
    }
    found = true;
    enabled = bool_switch_enabled(token, short_name);
  }

  if (!found) {
    settings->remove(key);
    return;
  }
  settings->setValue(key, enabled);
}

void save_switch_state_setting(z7::platform::qt::PortableSettings* settings,
                               const QString& key,
                               SwitchState state) {
  if (settings == nullptr || key.isEmpty()) {
    return;
  }
  if (state == SwitchState::kAuto) {
    settings->remove(key);
    return;
  }
  settings->setValue(key, state == SwitchState::kEnabled);
}

void save_presence_switch_setting(z7::platform::qt::PortableSettings* settings,
                                  const QString& key,
                                  const QStringList& tokens,
                                  const QString& short_name) {
  if (settings == nullptr || key.isEmpty()) {
    return;
  }
  if (!has_presence_switch(tokens, short_name)) {
    settings->remove(key);
    return;
  }
  settings->setValue(key, true);
}

void append_switch_token_from_setting(
    QStringList* tokens,
    const z7::platform::qt::PortableSettings& settings,
    const QString& key,
    const QString& short_name) {
  if (tokens == nullptr || key.isEmpty() || !settings.contains(key)) {
    return;
  }
  tokens->push_back(
      QStringLiteral("-%1%2")
          .arg(short_name, settings.value(key).toBool() ? QString()
                                                        : QStringLiteral("-")));
}

void append_presence_switch_token_from_setting(
    QStringList* tokens,
    const z7::platform::qt::PortableSettings& settings,
    const QString& key,
    const QString& short_name) {
  if (tokens == nullptr || key.isEmpty() || !settings.value(key, false).toBool()) {
    return;
  }
  tokens->push_back(QStringLiteral("-%1").arg(short_name));
}

struct MethodPropertyToken {
  QString name;
  QString value;
  bool has_value = false;
};

MethodPropertyToken parse_method_property_token(const QString& raw_token) {
  MethodPropertyToken out;
  QString token = raw_token.trimmed();
  if (!token.startsWith(QLatin1Char('-'))) {
    return out;
  }
  token.remove(0, 1);
  if (!token.isEmpty() &&
      (token.front() == QLatin1Char('m') || token.front() == QLatin1Char('M'))) {
    token.remove(0, 1);
  }
  token = token.trimmed();
  if (token.isEmpty()) {
    return out;
  }

  const qsizetype separator = token.indexOf(QLatin1Char('='));
  if (separator < 0) {
    out.name = token.trimmed();
    return out;
  }

  out.name = token.left(separator).trimmed();
  out.value = token.mid(separator + 1).trimmed();
  out.has_value = true;
  return out;
}

bool method_property_name_equals(const MethodPropertyToken& token,
                                 const QString& name) {
  return !token.name.isEmpty() &&
         token.name.compare(name, Qt::CaseInsensitive) == 0;
}

bool valid_method_property_value_for_key(const MethodPropertyToken& token,
                                         const QString& name) {
  if (!token.has_value || token.value.isEmpty()) {
    return false;
  }
  if (name.compare(QStringLiteral("tp"), Qt::CaseInsensitive) == 0) {
    bool ok = false;
    token.value.toUInt(&ok);
    return ok;
  }
  return true;
}

void append_method_property_token_from_setting(
    QStringList* tokens,
    const z7::platform::qt::PortableSettings& settings,
    const QString& key,
    const QString& property_name) {
  if (tokens == nullptr || key.isEmpty() || !settings.contains(key)) {
    return;
  }
  const QString value = settings.value(key).toString().trimmed();
  if (value.isEmpty()) {
    return;
  }
  tokens->push_back(QStringLiteral("-m%1=%2").arg(property_name, value));
}

void save_method_property_setting(
    z7::platform::qt::PortableSettings* settings,
    const QString& key,
    const QStringList& tokens,
    const QString& property_name) {
  if (settings == nullptr || key.isEmpty()) {
    return;
  }

  QString value;
  bool found = false;
  for (const QString& raw_token : tokens) {
    const MethodPropertyToken token = parse_method_property_token(raw_token);
    if (!method_property_name_equals(token, property_name) ||
        !valid_method_property_value_for_key(token, property_name)) {
      continue;
    }
    found = true;
    value = token.value;
  }

  if (!found) {
    settings->remove(key);
    return;
  }
  if (property_name.compare(QStringLiteral("tp"), Qt::CaseInsensitive) == 0) {
    settings->setValue(key, value.toUInt());
    return;
  }
  settings->setValue(key, value);
}

bool is_structured_method_property_token(const QString& raw_token,
                                         const QString& property_name) {
  const MethodPropertyToken token = parse_method_property_token(raw_token);
  return method_property_name_equals(token, property_name) &&
         valid_method_property_value_for_key(token, property_name);
}

QString advanced_options_text_from_original_keys(
    const z7::platform::qt::PortableSettings& settings,
    const QString& format_id) {
  QStringList tokens;
  append_switch_token_from_setting(
      &tokens, settings, settings_key(kSettingsCompressSymLinks), QStringLiteral("snl"));
  append_switch_token_from_setting(
      &tokens, settings, settings_key(kSettingsCompressHardLinks), QStringLiteral("snh"));
  append_switch_token_from_setting(
      &tokens, settings, settings_key(kSettingsCompressAltStreams), QStringLiteral("sns"));
  append_switch_token_from_setting(
      &tokens, settings, settings_key(kSettingsCompressSecurity), QStringLiteral("sni"));
  append_presence_switch_token_from_setting(
      &tokens, settings, settings_key(kSettingsCompressPreserveATime), QStringLiteral("ssp"));
  append_switch_token_from_setting(
      &tokens, settings, format_settings_key(format_id, kFormatMTime), QStringLiteral("mtm"));
  append_switch_token_from_setting(
      &tokens, settings, format_settings_key(format_id, kFormatCTime), QStringLiteral("mtc"));
  append_switch_token_from_setting(
      &tokens, settings, format_settings_key(format_id, kFormatATime), QStringLiteral("mta"));
  append_switch_token_from_setting(
      &tokens, settings, format_settings_key(format_id, kFormatSetArcMTime), QStringLiteral("stl"));
  append_method_property_token_from_setting(
      &tokens, settings, format_settings_key(format_id, kFormatTimePrec), QStringLiteral("tp"));
  append_method_property_token_from_setting(
      &tokens, settings, format_settings_key(format_id, kFormatMemUse), QStringLiteral("memuse"));

  const QString options =
      saved_string(settings, format_settings_key(format_id, kFormatOptions));
  tokens.append(normalized_advanced_tokens(options));
  return join_tokens(tokens);
}

QString options_text_without_structured_switches(const QString& text) {
  QStringList tokens = normalized_advanced_tokens(text);
  for (const QString& short_name :
       {QStringLiteral("snl"),
        QStringLiteral("snh"),
        QStringLiteral("sns"),
        QStringLiteral("sni"),
        QStringLiteral("mtm"),
        QStringLiteral("mtc"),
        QStringLiteral("mta"),
        QStringLiteral("stl")}) {
    remove_switch_tokens(&tokens, short_name);
  }
  remove_presence_switch_tokens(&tokens, QStringLiteral("ssp"));
  tokens.erase(
      std::remove_if(tokens.begin(), tokens.end(), [](const QString& token) {
        return is_structured_method_property_token(token, QStringLiteral("tp")) ||
               is_structured_method_property_token(token, QStringLiteral("memuse"));
      }),
      tokens.end());
  return join_tokens(tokens);
}

void save_structured_advanced_options(
    z7::platform::qt::PortableSettings* settings,
    const QString& format_id,
    const QString& text) {
  if (settings == nullptr) {
    return;
  }
  const QStringList tokens = normalized_advanced_tokens(text);
  save_bool_switch_setting(
      settings, settings_key(kSettingsCompressSymLinks), tokens, QStringLiteral("snl"));
  save_bool_switch_setting(
      settings, settings_key(kSettingsCompressHardLinks), tokens, QStringLiteral("snh"));
  save_bool_switch_setting(
      settings, settings_key(kSettingsCompressAltStreams), tokens, QStringLiteral("sns"));
  save_bool_switch_setting(
      settings, settings_key(kSettingsCompressSecurity), tokens, QStringLiteral("sni"));
  save_presence_switch_setting(
      settings, settings_key(kSettingsCompressPreserveATime), tokens, QStringLiteral("ssp"));
  save_switch_state_setting(
      settings,
      format_settings_key(format_id, kFormatMTime),
      switch_state_from_tokens(tokens, QStringLiteral("mtm")));
  save_switch_state_setting(
      settings,
      format_settings_key(format_id, kFormatCTime),
      switch_state_from_tokens(tokens, QStringLiteral("mtc")));
  save_switch_state_setting(
      settings,
      format_settings_key(format_id, kFormatATime),
      switch_state_from_tokens(tokens, QStringLiteral("mta")));
  save_switch_state_setting(
      settings,
      format_settings_key(format_id, kFormatSetArcMTime),
      switch_state_from_tokens(tokens, QStringLiteral("stl")));
  save_method_property_setting(
      settings,
      format_settings_key(format_id, kFormatTimePrec),
      tokens,
      QStringLiteral("tp"));
  save_method_property_setting(
      settings,
      format_settings_key(format_id, kFormatMemUse),
      tokens,
      QStringLiteral("memuse"));
  set_string_or_remove(settings,
                       format_settings_key(format_id, kFormatOptions),
                       options_text_without_structured_switches(text));
}

}  // namespace

QString CompressDialog::default_encryption_method_for_current_format() const {
  return default_encryption_method_for_format(current_format_id());
}

QString CompressDialog::selected_encryption_method_spec() const {
  if (encryption_method_combo_ == nullptr || encryption_method_combo_->count() <= 0) {
    return QString();
  }

  const QString selected = encryption_method_combo_->currentData().toString().trimmed();
  if (selected.isEmpty()) {
    return QString();
  }

  return selected.compare(default_encryption_method_for_current_format(),
                          Qt::CaseInsensitive) == 0
             ? QString()
             : selected;
}

void CompressDialog::load_archive_path_history() {
  if (archive_name_combo_ == nullptr) {
    return;
  }

  z7::platform::qt::PortableSettings settings;
  const QStringList history = normalized_archive_history(
      settings.value(settings_key(kSettingsCompressArchiveHistory)).toStringList());
  for (const QString& entry : history) {
    const QString normalized =
        z7::ui::common::normalize_path_history_entry(entry);
    if (!normalized.isEmpty()) {
      archive_name_combo_->addItem(QDir::toNativeSeparators(normalized),
                                   normalized);
    }
  }
}

QString CompressDialog::initial_or_saved_archive_type(
    const CompressCommandOptions& initial) const {
  const QString initial_type = normalize_format_id(
      z7::ui::archive_support::from_native_string(initial.archive_type));
  if (!initial_type.isEmpty()) {
    return initial_type;
  }

  z7::platform::qt::PortableSettings settings;
  return normalize_format_id(
      settings.value(settings_key(kSettingsCompressArchiver)).toString());
}

bool CompressDialog::saved_show_password() const {
  z7::platform::qt::PortableSettings settings;
  return settings.value(settings_key(kSettingsCompressShowPassword), false)
      .toBool();
}

bool CompressDialog::initial_or_saved_encrypt_headers(
    const CompressCommandOptions& initial) const {
  if (!initial.password.empty()) {
    return initial.encrypt_headers;
  }

  z7::platform::qt::PortableSettings settings;
  return settings.value(settings_key(kSettingsCompressEncryptHeaders), false)
      .toBool();
}

void CompressDialog::apply_persistent_format_options(
    const QString& format_id,
    const CompressCommandOptions* explicit_options) {
  const QString normalized_format = normalize_format_id(format_id);
  if (normalized_format.isEmpty()) {
    return;
  }

  z7::platform::qt::PortableSettings settings;
  QString level = explicit_or_saved_string(
      settings,
      format_settings_key(normalized_format, kFormatLevel),
      explicit_options,
      &CompressCommandOptions::compression_level);
  if (level.isEmpty()) {
    level = saved_string(settings, settings_key(kSettingsCompressLevel));
  }
  const QString method = explicit_or_saved_string(
      settings,
      format_settings_key(normalized_format, kFormatMethod),
      explicit_options,
      &CompressCommandOptions::method);
  const QString dictionary = explicit_or_saved_string(
      settings,
      format_settings_key(normalized_format, kFormatDictionary),
      explicit_options,
      &CompressCommandOptions::dictionary_size);
  const QString word_size = explicit_or_saved_string(
      settings,
      format_settings_key(normalized_format, kFormatOrder),
      explicit_options,
      &CompressCommandOptions::word_size);
  const QString solid = explicit_or_saved_string(
      settings,
      format_settings_key(normalized_format, kFormatBlockSize),
      explicit_options,
      &CompressCommandOptions::solid_block_size);
  const QString threads = explicit_or_saved_string(
      settings,
      format_settings_key(normalized_format, kFormatNumThreads),
      explicit_options,
      &CompressCommandOptions::thread_count);
  const QString encryption_method = explicit_or_saved_string(
      settings,
      format_settings_key(normalized_format, kFormatEncryptionMethod),
      explicit_options,
      &CompressCommandOptions::encryption_method);

  set_combo_data_or_text(level_combo_, level);
  recompute_state(false, false, false, false, false);

  set_combo_data_or_text(method_combo_, method);
  recompute_state(true, false, false, false, false);

  set_combo_data_or_text(dictionary_combo_, dictionary);
  set_combo_data_or_text(word_size_combo_, word_size);
  set_combo_data_or_text(solid_combo_, solid);
  set_combo_data_or_text(threads_combo_, threads);

  if (explicit_options != nullptr &&
      !explicit_options->extra_parameters.empty()) {
    parameters_edit_->setText(
        joined_extra_parameters(explicit_options->extra_parameters));
  } else {
    parameters_edit_->setText(
        advanced_options_text_from_original_keys(settings, normalized_format));
  }

  set_combo_data_or_text(
      encryption_method_combo_,
      encryption_method.isEmpty()
          ? default_encryption_method_for_format(normalized_format)
          : encryption_method);
  recompute_state(true, true, true, true, true);
}

void CompressDialog::save_current_format_settings() const {
  save_format_settings(active_format_settings_id_);
}

void CompressDialog::save_format_settings(const QString& format_id) const {
  const QString normalized_format = normalize_format_id(format_id);
  if (normalized_format.isEmpty()) {
    return;
  }

  z7::platform::qt::PortableSettings settings;
  set_string_or_remove(&settings,
                       format_settings_key(normalized_format, kFormatLevel),
                       current_combo_data(level_combo_));
  set_string_or_remove(&settings,
                       format_settings_key(normalized_format, kFormatMethod),
                       current_combo_data(method_combo_));
  set_string_or_remove(&settings,
                       format_settings_key(normalized_format, kFormatDictionary),
                       current_combo_data(dictionary_combo_));
  set_string_or_remove(&settings,
                       format_settings_key(normalized_format, kFormatOrder),
                       current_combo_data(word_size_combo_));
  set_string_or_remove(&settings,
                       format_settings_key(normalized_format, kFormatBlockSize),
                       current_combo_data(solid_combo_));
  set_string_or_remove(&settings,
                       format_settings_key(normalized_format, kFormatNumThreads),
                       current_combo_data(threads_combo_));
  set_string_or_remove(
      &settings,
      format_settings_key(normalized_format, kFormatEncryptionMethod),
      selected_encryption_method_spec());
  save_structured_advanced_options(&settings,
                                   normalized_format,
                                   parameters_edit_->text());
}

void CompressDialog::save_persistent_settings() const {
  save_current_format_settings();

  z7::platform::qt::PortableSettings settings;
  settings.setValue(settings_key(kSettingsCompressArchiver),
                    current_format_id());
  set_string_or_remove(&settings,
                       settings_key(kSettingsCompressLevel),
                       current_combo_data(level_combo_));
  settings.setValue(settings_key(kSettingsCompressShowPassword),
                    show_password_checkbox_ != nullptr &&
                        show_password_checkbox_->isChecked());
  settings.setValue(settings_key(kSettingsCompressEncryptHeaders),
                    encrypt_headers_checkbox_ != nullptr &&
                        encrypt_headers_checkbox_->isVisible() &&
                        encrypt_headers_checkbox_->isChecked());

  const QStringList history =
      settings.value(settings_key(kSettingsCompressArchiveHistory))
          .toStringList();
  settings.setValue(settings_key(kSettingsCompressArchiveHistory),
                    normalized_archive_history(history,
                                               compose_archive_path()));
  settings.sync();
}

void CompressDialog::on_help_clicked() {}

void CompressDialog::on_options_clicked() {
  QDialog options_dialog(this);
#ifdef Z7_TESTING
  options_dialog.setObjectName(QStringLiteral("compressAdvancedOptionsDialog"));
#endif
  options_dialog.setWindowTitle(lang_or(2100));
  options_dialog.resize(680, 520);

  auto* options_layout = new QVBoxLayout(&options_dialog);

  // ID 115 in original resources is a control ID (IDG_COMPRESS_NTFS), not a Lang ID.
  // Original dialog keeps this group title as static "NTFS".
  const z7::ui::runtime_support::PlatformRestrictionUi ntfs_ui =
      z7::ui::runtime_support::platform_restriction_ui(
          QStringLiteral("NTFS"),
          z7::ui::runtime_support::PlatformSupport::kWindowsOnly);
  auto* ntfs_group = new QGroupBox(ntfs_ui.text, &options_dialog);
#ifdef Z7_TESTING
  ntfs_group->setObjectName(QStringLiteral("compressAdvancedNtfsGroup"));
#endif
  ntfs_group->setToolTip(ntfs_ui.tooltip);
  auto* ntfs_layout = new QVBoxLayout(ntfs_group);
  auto* sym_links = new QCheckBox(
      lang_or(4040), ntfs_group);
#ifdef Z7_TESTING
  sym_links->setObjectName(QStringLiteral("compressOptionsSymLinksCheckBox"));
#endif
  auto* hard_links = new QCheckBox(
      lang_or(4041), ntfs_group);
#ifdef Z7_TESTING
  hard_links->setObjectName(QStringLiteral("compressOptionsHardLinksCheckBox"));
#endif
  auto* alt_streams = new QCheckBox(
      lang_or(4042), ntfs_group);
#ifdef Z7_TESTING
  alt_streams->setObjectName(QStringLiteral("compressOptionsAltStreamsCheckBox"));
#endif
  auto* file_security = new QCheckBox(
      lang_or(4043), ntfs_group);
#ifdef Z7_TESTING
  file_security->setObjectName(QStringLiteral("compressOptionsSecurityCheckBox"));
#endif
  ntfs_layout->addWidget(sym_links);
  ntfs_layout->addWidget(hard_links);
  ntfs_layout->addWidget(alt_streams);
  ntfs_layout->addWidget(file_security);
  options_layout->addWidget(ntfs_group);

  auto* time_group = new QGroupBox(lang_or(4080), &options_dialog);
#ifdef Z7_TESTING
  time_group->setObjectName(QStringLiteral("compressAdvancedTimeGroup"));
#endif
  auto* time_layout = new QVBoxLayout(time_group);
  auto* mtime_checkbox =
      new QCheckBox(lang_or(4082), time_group);
#ifdef Z7_TESTING
  mtime_checkbox->setObjectName(QStringLiteral("compressOptionsMTimeCheckBox"));
#endif
  auto* ctime_checkbox =
      new QCheckBox(lang_or(4083), time_group);
#ifdef Z7_TESTING
  ctime_checkbox->setObjectName(QStringLiteral("compressOptionsCTimeCheckBox"));
#endif
  auto* atime_checkbox =
      new QCheckBox(lang_or(4084), time_group);
#ifdef Z7_TESTING
  atime_checkbox->setObjectName(QStringLiteral("compressOptionsATimeCheckBox"));
#endif
  auto* archive_time_checkbox =
      new QCheckBox(lang_or(4085), time_group);
#ifdef Z7_TESTING
  archive_time_checkbox->setObjectName(
      QStringLiteral("compressOptionsSetArchiveTimeCheckBox"));
#endif
  auto* preserve_atime_checkbox =
      new QCheckBox(lang_or(4086), time_group);
#ifdef Z7_TESTING
  preserve_atime_checkbox->setObjectName(
      QStringLiteral("compressOptionsPreserveATimeCheckBox"));
#endif
  time_layout->addWidget(mtime_checkbox);
  time_layout->addWidget(ctime_checkbox);
  time_layout->addWidget(atime_checkbox);
  time_layout->addWidget(archive_time_checkbox);
  time_layout->addWidget(preserve_atime_checkbox);
  options_layout->addWidget(time_group);

  auto* options_label = new QLabel(lang_or(4010), &options_dialog);
  options_layout->addWidget(options_label);

  auto* options_edit = new QPlainTextEdit(&options_dialog);
#ifdef Z7_TESTING
  options_edit->setObjectName(QStringLiteral("compressAdvancedOptionsEdit"));
#endif
  options_edit->setPlainText(parameters_edit_->text());
  options_layout->addWidget(options_edit, 1);

  const QStringList initial_tokens =
      QProcess::splitCommand(parameters_edit_->text().trimmed());
  sym_links->setChecked(has_enabled_switch(initial_tokens, QStringLiteral("snl")));
  hard_links->setChecked(has_enabled_switch(initial_tokens, QStringLiteral("snh")));
  alt_streams->setChecked(has_enabled_switch(initial_tokens, QStringLiteral("sns")));
  file_security->setChecked(has_enabled_switch(initial_tokens, QStringLiteral("sni")));
  set_checkbox_switch_state(
      mtime_checkbox, switch_state_from_tokens(initial_tokens, QStringLiteral("mtm")));
  set_checkbox_switch_state(
      ctime_checkbox, switch_state_from_tokens(initial_tokens, QStringLiteral("mtc")));
  set_checkbox_switch_state(
      atime_checkbox, switch_state_from_tokens(initial_tokens, QStringLiteral("mta")));
  set_checkbox_switch_state(
      archive_time_checkbox, switch_state_from_tokens(initial_tokens, QStringLiteral("stl")));
  preserve_atime_checkbox->setChecked(
      has_presence_switch(initial_tokens, QStringLiteral("ssp")));
  if (!ntfs_ui.supported) {
    sym_links->setChecked(false);
    hard_links->setChecked(false);
    alt_streams->setChecked(false);
    file_security->setChecked(false);
    sym_links->setToolTip(ntfs_ui.tooltip);
    hard_links->setToolTip(ntfs_ui.tooltip);
    alt_streams->setToolTip(ntfs_ui.tooltip);
    file_security->setToolTip(ntfs_ui.tooltip);
    ntfs_group->setEnabled(false);
  }

  auto* options_buttons =
      new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                           Qt::Horizontal,
                           &options_dialog);
#ifdef Z7_TESTING
  options_buttons->setObjectName(QStringLiteral("compressAdvancedOptionsButtons"));
#endif
  if (QPushButton* ok = options_buttons->button(QDialogButtonBox::Ok)) {
    ok->setText(L(401));
#ifdef Z7_TESTING
    ok->setObjectName(QStringLiteral("compressAdvancedOptionsOkButton"));
#endif
  }
  if (QPushButton* cancel = options_buttons->button(QDialogButtonBox::Cancel)) {
    cancel->setText(L(402));
#ifdef Z7_TESTING
    cancel->setObjectName(QStringLiteral("compressAdvancedOptionsCancelButton"));
#endif
  }
  QPushButton* options_help =
      options_buttons->addButton(lang_or(409), QDialogButtonBox::HelpRole);
#ifdef Z7_TESTING
  options_help->setObjectName(QStringLiteral("compressAdvancedOptionsHelpButton"));
#endif
  options_help->setEnabled(false);
  connect(options_buttons, &QDialogButtonBox::accepted, &options_dialog, &QDialog::accept);
  connect(options_buttons, &QDialogButtonBox::rejected, &options_dialog, &QDialog::reject);
  options_layout->addWidget(options_buttons);

  if (options_dialog.exec() != QDialog::Accepted) {
    return;
  }

  QStringList tokens = QProcess::splitCommand(
      normalize_advanced_options_text(options_edit->toPlainText()));
  if (!ntfs_ui.supported) {
    remove_switch_tokens(&tokens, QStringLiteral("snl"));
    remove_switch_tokens(&tokens, QStringLiteral("snh"));
    remove_switch_tokens(&tokens, QStringLiteral("sns"));
    remove_switch_tokens(&tokens, QStringLiteral("sni"));
  } else {
    set_bool_switch(&tokens, QStringLiteral("snl"), sym_links->isChecked());
    set_bool_switch(&tokens, QStringLiteral("snh"), hard_links->isChecked());
    set_bool_switch(&tokens, QStringLiteral("sns"), alt_streams->isChecked());
    set_bool_switch(&tokens, QStringLiteral("sni"), file_security->isChecked());
  }
  set_switch_state(
      &tokens, QStringLiteral("mtm"), checkbox_switch_state(mtime_checkbox));
  set_switch_state(
      &tokens, QStringLiteral("mtc"), checkbox_switch_state(ctime_checkbox));
  set_switch_state(
      &tokens, QStringLiteral("mta"), checkbox_switch_state(atime_checkbox));
  set_switch_state(
      &tokens, QStringLiteral("stl"), checkbox_switch_state(archive_time_checkbox));
  set_presence_switch(
      &tokens, QStringLiteral("ssp"), preserve_atime_checkbox->isChecked());
  parameters_edit_->setText(normalize_advanced_options_text(join_tokens(tokens)));
}

}  // namespace z7::ui::gui
