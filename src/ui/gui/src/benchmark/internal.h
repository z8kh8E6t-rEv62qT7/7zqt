#pragma once

#include <cstdint>
#include <string>

#include <QString>

#include "archive_string_codec_qt.h"

class QGridLayout;
class QLabel;
class QPlainTextEdit;
class QWidget;

namespace z7::ui::gui::benchmark_internal {

constexpr uint32_t kMinDicLogSize = 18;
constexpr uint64_t kDefaultDictBytes = 32ULL << 20;

QString lang_or(uint32_t id);
QLabel* make_metric_label(QWidget* parent);
void set_metrics(QLabel* size,
                 QLabel* speed,
                 QLabel* usage,
                 QLabel* rpu,
                 QLabel* rating,
                 const QString& size_text,
                 const QString& speed_text,
                 const QString& usage_text,
                 const QString& rpu_text,
                 const QString& rating_text);
void add_metric_header(QGridLayout* grid, int row);
bool parse_uint64_decimal(const QString& text, uint64_t& out);
uint64_t parse_size_to_bytes_or_default(const QString& raw, uint64_t default_value);
uint32_t parse_uint32_or_default(const QString& raw, uint32_t default_value);
QString bytes_to_display_size(uint64_t bytes);
QString bytes_to_switch_size(uint64_t bytes);
QString mips_to_gips(const QString& mips);
double parse_double_or_zero(const QString& text);
QString mb_text(uint64_t bytes);
void apply_log_view_style(QPlainTextEdit* edit);
void apply_log_label_style(QLabel* label);
QString format_elapsed_text(qint64 elapsed_ms, bool finished);

}  // namespace z7::ui::gui::benchmark_internal
