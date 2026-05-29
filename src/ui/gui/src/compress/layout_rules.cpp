// src/ui/gui/src/compress/layout_rules.cpp
// Role: Format rules and combo helpers for CompressDialog.

#include "internal.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

#include <QComboBox>

#include "common/archive_type_normalization.h"
#include "official_lang_catalog.h"

namespace z7::ui::gui::compress_internal {

using z7::ui::runtime_support::L;
using z7::ui::runtime_support::strip_mnemonic;

constexpr uint32_t kLevelsMaskAll = (1u << 10) - 1;

int find_combo_data(const QComboBox* combo, const QString& data) {
  for (int i = 0; i < combo->count(); ++i) {
    if (combo->itemData(i).toString() == data) {
      return i;
    }
  }
  return -1;
}

QString normalize_format_id(QString value) {
  return QString::fromStdString(
      z7::common::normalize_archive_type_token_copy(value.toStdString()));
}

void set_combo_data_or_text(QComboBox* combo, const QString& value) {
  if (combo == nullptr) {
    return;
  }
  if (value.isEmpty()) {
    return;
  }

  const int index = find_combo_data(combo, value);
  if (index >= 0) {
    combo->setCurrentIndex(index);
  } else if (combo->isEditable()) {
    combo->setEditText(value);
  }
}

void set_format_combo_data_or_text(QComboBox* combo, const QString& value) {
  set_combo_data_or_text(combo, normalize_format_id(value));
}

void set_combo_data_or_default(QComboBox* combo,
                               const QString& preferred,
                               const QString& default_value) {
  if (combo == nullptr || combo->count() == 0) {
    return;
  }

  const int preferred_index = find_combo_data(combo, preferred);
  if (preferred_index >= 0) {
    combo->setCurrentIndex(preferred_index);
    return;
  }

  const int default_index = find_combo_data(combo, default_value);
  if (default_index >= 0) {
    combo->setCurrentIndex(default_index);
    return;
  }

  combo->setCurrentIndex(0);
}

void add_combo_item(QComboBox* combo, const QString& text, const QString& data) {
  combo->addItem(text, data);
}

QString level_caption(const QString& level) {
  uint32_t level_id = 0;
  if (level == QStringLiteral("0")) {
    level_id = 4050;
  } else if (level == QStringLiteral("1")) {
    level_id = 4051;
  } else if (level == QStringLiteral("3")) {
    level_id = 4052;
  } else if (level == QStringLiteral("5")) {
    level_id = 4053;
  } else if (level == QStringLiteral("7")) {
    level_id = 4054;
  } else if (level == QStringLiteral("9")) {
    level_id = 4055;
  }

  if (level_id == 0) {
    return level;
  }

  return QStringLiteral("%1 - %2").arg(level, strip_mnemonic(L(level_id)));
}

QStringList level_values_for_mask(uint32_t levels_mask) {
  QStringList values;
  for (uint32_t i = 0; i <= 9; ++i) {
    if ((levels_mask & (1u << i)) == 0) {
      continue;
    }
    values.push_back(QString::number(i));
  }
  return values;
}

bool supports_sfx_method(const QString& method) {
  static const QStringList kSfxMethods = {
      QStringLiteral("Copy"),
      QStringLiteral("LZMA"),
      QStringLiteral("LZMA2"),
      QStringLiteral("PPMd")};
  return kSfxMethods.contains(method);
}

FormatRule rule_for_format_id(const QString& format_id) {
  const QString id = normalize_format_id(format_id);

  if (id == QStringLiteral("7z")) {
    return {id,
            QStringLiteral("7z"),
            kLevelsMaskAll,
            {QStringLiteral("LZMA2"),
             QStringLiteral("LZMA"),
             QStringLiteral("PPMd"),
             QStringLiteral("BZip2")},
            QStringLiteral("LZMA2"),
            true,
            true,
            true,
            true,
            true,
            true,
            true,
            false};
  }

  if (id == QStringLiteral("zip")) {
    return {id,
            QStringLiteral("Zip"),
            (1u << 0) | (1u << 1) | (1u << 3) | (1u << 5) | (1u << 7) |
                (1u << 9),
            {QStringLiteral("Deflate"),
             QStringLiteral("Deflate64"),
             QStringLiteral("BZip2"),
             QStringLiteral("LZMA"),
             QStringLiteral("PPMd")},
            QStringLiteral("Deflate"),
            false,
            false,
            true,
            true,
            false,
            true,
            false,
            false};
  }

  if (id == QStringLiteral("gzip")) {
    return {id,
            QStringLiteral("GZip"),
            (1u << 1) | (1u << 5) | (1u << 7) | (1u << 9),
            {QStringLiteral("Deflate")},
            QStringLiteral("Deflate"),
            false,
            false,
            false,
            false,
            false,
            true,
            false,
            false};
  }

  if (id == QStringLiteral("bzip2")) {
    return {id,
            QStringLiteral("BZip2"),
            (1u << 1) | (1u << 3) | (1u << 5) | (1u << 7) | (1u << 9),
            {QStringLiteral("BZip2")},
            QStringLiteral("BZip2"),
            false,
            false,
            true,
            false,
            false,
            true,
            false,
            false};
  }

  if (id == QStringLiteral("xz")) {
    return {id,
            QStringLiteral("xz"),
            kLevelsMaskAll - (1u << 0),
            {QStringLiteral("LZMA2")},
            QStringLiteral("LZMA2"),
            false,
            true,
            true,
            false,
            false,
            true,
            false,
            false};
  }

  if (id == QStringLiteral("tar")) {
    return {id,
            QStringLiteral("Tar"),
            (1u << 0),
            {QStringLiteral("GNU"), QStringLiteral("POSIX")},
            QStringLiteral("GNU"),
            false,
            false,
            false,
            false,
            false,
            false,
            false,
            false};
  }

  if (id == QStringLiteral("wim")) {
    return {id,
            QStringLiteral("wim"),
            (1u << 0),
            {},
            {},
            false,
            false,
            false,
            false,
            false,
            false,
            false,
            false};
  }

  if (id == QStringLiteral("hash")) {
    return {id,
            QStringLiteral("Hash"),
            0,
            {QStringLiteral("SHA256"), QStringLiteral("SHA1")},
            QStringLiteral("SHA256"),
            false,
            false,
            false,
            false,
            false,
            false,
            false,
            true};
  }

  FormatRule unsupported;
  unsupported.id = id;
  unsupported.display_name = id;
  return unsupported;
}

}  // namespace z7::ui::gui::compress_internal
