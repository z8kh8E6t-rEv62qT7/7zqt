#pragma once

#include <QString>
#include <QStringList>
#include <cstdint>
#include <string>

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

    int find_combo_data(QComboBox const* combo, QString const& data);
    QString normalize_format_id(QString value);
    void set_combo_data_or_text(QComboBox* combo, QString const& value);
    void set_format_combo_data_or_text(QComboBox* combo, QString const& value);
    void set_combo_data_or_default(QComboBox* combo, QString const& preferred, QString const& default_value);
    void add_combo_item(QComboBox* combo, QString const& text, QString const& data);
    QString level_caption(QString const& level);
    QStringList level_values_for_mask(uint32_t levels_mask);
    bool supports_sfx_method(QString const& method);
    FormatRule rule_for_format_id(QString const& format_id);
    QStringList method_list_for_rule(FormatRule const& rule, int level, bool sfx_enabled);
    uint64_t parse_size_to_bytes(QString const& value);
    QString format_bytes(uint64_t bytes);
    int method_level_or_default(QComboBox const* level_combo);
    QString effective_method_for_format(QString const& format_id, QString const& method);
    uint64_t lzma_auto_dict_for_level(int level);
    uint64_t bzip2_auto_dict_for_level(int level);
    QString dictionary_size_data(uint64_t bytes);
    QString dictionary_size_label(uint64_t bytes, bool is_default = false);
    uint64_t lzma2_chunk_size(uint64_t dictionary_size);
    QString size_token_for_bytes(uint64_t bytes);
    QString size_label_for_bytes(uint64_t bytes);
    uint64_t auto_solid_size_bytes(FormatRule const& rule, QString const& method, int level, uint64_t dictionary_size);

} // namespace z7::ui::gui::compress_internal
