// src/ui/gui/src/benchmark/layout_helpers.cpp
// Role: Shared formatting and metric helper routines for BenchmarkDialog.

#include "internal.h"

#include <algorithm>
#include <cstdint>
#include <string>

#include <QFontDatabase>
#include <QGridLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QTextOption>
#include <QWidget>

#include "official_lang_catalog.h"

namespace z7::ui::gui::benchmark_internal {

using z7::ui::runtime_support::L;
using z7::ui::runtime_support::strip_mnemonic;

QString lang_or(uint32_t id) {
  return strip_mnemonic(L(id));
}

QLabel* make_metric_label(QWidget* parent) {
  auto* label = new QLabel(QStringLiteral("..."), parent);
  label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
  return label;
}

void set_metrics(QLabel* size,
                 QLabel* speed,
                 QLabel* usage,
                 QLabel* rpu,
                 QLabel* rating,
                 const QString& size_text,
                 const QString& speed_text,
                 const QString& usage_text,
                 const QString& rpu_text,
                 const QString& rating_text) {
  if (size != nullptr) {
    size->setText(size_text);
  }
  if (speed != nullptr) {
    speed->setText(speed_text);
  }
  if (usage != nullptr) {
    usage->setText(usage_text);
  }
  if (rpu != nullptr) {
    rpu->setText(rpu_text);
  }
  if (rating != nullptr) {
    rating->setText(rating_text);
  }
}

void add_metric_header(QGridLayout* grid, int row) {
  const QStringList columns = {
      lang_or(1007),
      lang_or(3903),
      lang_or(7608),
      lang_or(7609),
      lang_or(7604)};
  for (int i = 0; i < columns.size(); ++i) {
    auto* label = new QLabel(columns.at(i));
    label->setAlignment(Qt::AlignCenter);
    grid->addWidget(label, row, i + 1);
  }
}

bool parse_uint64_decimal(const QString& text, uint64_t& out) {
  const QString t = text.trimmed();
  if (t.isEmpty()) {
    return false;
  }
  uint64_t value = 0;
  for (const QChar c : t) {
    if (!c.isDigit()) {
      return false;
    }
    value = value * 10 + static_cast<uint64_t>(c.unicode() - u'0');
  }
  out = value;
  return true;
}

uint64_t parse_size_to_bytes_or_default(const QString& raw, uint64_t default_value) {
  QString t = raw.trimmed().toLower();
  if (t.isEmpty()) {
    return default_value;
  }

  uint64_t mult = 1;
  const QChar suffix = t.back();
  if (suffix == QLatin1Char('k') || suffix == QLatin1Char('m') ||
      suffix == QLatin1Char('g') || suffix == QLatin1Char('t')) {
    t.chop(1);
    if (suffix == QLatin1Char('k')) {
      mult = 1ULL << 10;
    } else if (suffix == QLatin1Char('m')) {
      mult = 1ULL << 20;
    } else if (suffix == QLatin1Char('g')) {
      mult = 1ULL << 30;
    } else if (suffix == QLatin1Char('t')) {
      mult = 1ULL << 40;
    }
  }

  uint64_t base = 0;
  if (!parse_uint64_decimal(t, base) || base == 0) {
    return default_value;
  }
  return base * mult;
}

uint32_t parse_uint32_or_default(const QString& raw, uint32_t default_value) {
  uint64_t parsed = 0;
  if (!parse_uint64_decimal(raw, parsed) || parsed == 0 ||
      parsed > static_cast<uint64_t>(std::numeric_limits<uint32_t>::max())) {
    return default_value;
  }
  return static_cast<uint32_t>(parsed);
}

QString bytes_to_display_size(uint64_t bytes) {
  if (bytes >= (1ULL << 30)) {
    return QStringLiteral("%1 GB").arg(bytes >> 30);
  }
  if (bytes >= (1ULL << 20)) {
    return QStringLiteral("%1 MB").arg(bytes >> 20);
  }
  return QStringLiteral("%1 KB").arg(bytes >> 10);
}

QString bytes_to_switch_size(uint64_t bytes) {
  if (bytes >= (1ULL << 30) && (bytes % (1ULL << 30) == 0)) {
    return QStringLiteral("%1g").arg(bytes >> 30);
  }
  if (bytes >= (1ULL << 20) && (bytes % (1ULL << 20) == 0)) {
    return QStringLiteral("%1m").arg(bytes >> 20);
  }
  return QStringLiteral("%1k").arg(bytes >> 10);
}

QString mips_to_gips(const QString& mips) {
  bool ok = false;
  const qulonglong value = mips.toULongLong(&ok);
  if (!ok) {
    return mips;
  }
  const qulonglong whole = value / 1000ULL;
  const qulonglong rem = value % 1000ULL;
  return QStringLiteral("%1.%2")
      .arg(whole)
      .arg(rem, 3, 10, QLatin1Char('0'));
}

double parse_double_or_zero(const QString& text) {
  bool ok = false;
  const double value = text.toDouble(&ok);
  return ok ? value : 0.0;
}

QString mb_text(uint64_t bytes) {
  return QStringLiteral("%1 MB").arg((bytes + (1ULL << 20) - 1) >> 20);
}

void apply_log_view_style(QPlainTextEdit* edit) {
  if (edit == nullptr) {
    return;
  }
  edit->setLineWrapMode(QPlainTextEdit::NoWrap);
  edit->setWordWrapMode(QTextOption::NoWrap);
  edit->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
  edit->setStyleSheet(QStringLiteral("QPlainTextEdit { background: transparent; border: none; }"));
}

void apply_log_label_style(QLabel* label) {
  if (label == nullptr) {
    return;
  }
  label->setTextFormat(Qt::PlainText);
  label->setWordWrap(false);
  label->setAlignment(Qt::AlignLeft | Qt::AlignTop);
  label->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
  label->setStyleSheet(QStringLiteral("QLabel { background: transparent; border: none; }"));
}

QString format_elapsed_text(qint64 elapsed_ms, bool finished) {
  if (elapsed_ms < 0) {
    elapsed_ms = 0;
  }
  if (!finished) {
    return QStringLiteral("%1 s").arg(elapsed_ms / 1000);
  }
  const qint64 secs = elapsed_ms / 1000;
  const qint64 rem = elapsed_ms % 1000;
  return QStringLiteral("%1.%2 s")
      .arg(secs)
      .arg(rem, 3, 10, QLatin1Char('0'));
}

}  // namespace z7::ui::gui::benchmark_internal
