#pragma once

#include <cstdint>
#include <string>

#include <QString>
#include <QStringList>

#include "archive_string_codec_qt.h"

class QComboBox;

namespace z7::ui::gui::compress_internal {

struct FormatRule {
  QString id;
  QString display_name;
  uint32_t levels_mask = 0;
  QStringList methods;
  QString default_method;
  bool filter = false;
  bool solid = false;
  bool multi_thread = false;
  bool encrypt = false;
  bool encrypt_file_names = false;
  bool mem_use = false;
  bool sfx = false;
  bool is_hash = false;
};

int find_combo_data(const QComboBox* combo, const QString& data);
QString normalize_format_id(QString value);
void set_combo_data_or_text(QComboBox* combo, const QString& value);
void set_format_combo_data_or_text(QComboBox* combo, const QString& value);
void set_combo_data_or_default(QComboBox* combo,
                               const QString& preferred,
                               const QString& default_value);
void add_combo_item(QComboBox* combo, const QString& text, const QString& data);
QString level_caption(const QString& level);
QStringList level_values_for_mask(uint32_t levels_mask);
bool supports_sfx_method(const QString& method);
FormatRule rule_for_format_id(const QString& format_id);
QStringList method_list_for_rule(const FormatRule& rule, int level, bool sfx_enabled);
uint64_t parse_size_to_bytes(const QString& value);
QString format_bytes(uint64_t bytes);
int method_level_or_default(const QComboBox* level_combo);
QString effective_method_for_format(const QString& format_id, const QString& method);
uint64_t lzma_auto_dict_for_level(int level);
uint64_t bzip2_auto_dict_for_level(int level);
QString dictionary_size_data(uint64_t bytes);
QString dictionary_size_label(uint64_t bytes, bool is_default = false);
uint64_t lzma2_chunk_size(uint64_t dictionary_size);
QString size_token_for_bytes(uint64_t bytes);
QString size_label_for_bytes(uint64_t bytes);
uint64_t auto_solid_size_bytes(const FormatRule& rule,
                               const QString& method,
                               int level,
                               uint64_t dictionary_size);

}  // namespace z7::ui::gui::compress_internal
