// src/ui/gui/src/compress/layout_memory.cpp
// Role: Dictionary/solid/thread and memory estimation helpers for CompressDialog.

#include "internal.h"

#include <algorithm>
#include <array>
#include <cstdint>

#include <QComboBox>
namespace z7::ui::gui::compress_internal {

QStringList method_list_for_rule(const FormatRule& rule,
                                 int level,
                                 bool sfx_enabled) {
  if (level == 0 && !rule.is_hash && rule.id != QStringLiteral("tar")) {
    return {};
  }

  QStringList methods = rule.methods;
  if (sfx_enabled) {
    methods.erase(
        std::remove_if(methods.begin(), methods.end(), [](const QString& method) {
          return !supports_sfx_method(method);
        }),
        methods.end());
  }
  return methods;
}

uint64_t parse_size_to_bytes(const QString& value) {
  QString trimmed = value.trimmed().toLower();
  if (trimmed.isEmpty() || trimmed == QStringLiteral("off")) {
    return 0;
  }
  if (trimmed == QStringLiteral("on")) {
    return static_cast<uint64_t>(1) << 30;
  }
  if (trimmed.endsWith('*')) {
    trimmed.chop(1);
    trimmed = trimmed.trimmed();
  }

  int i = 0;
  while (i < trimmed.size() && (trimmed[i].isDigit() || trimmed[i] == '.')) {
    ++i;
  }
  if (i == 0) {
    return 0;
  }

  bool ok = false;
  const double number = trimmed.left(i).toDouble(&ok);
  if (!ok || number <= 0.0) {
    return 0;
  }

  QString unit = trimmed.mid(i).trimmed();
  uint64_t mult = 1;
  if (unit == QStringLiteral("k") || unit == QStringLiteral("kb")) {
    mult = static_cast<uint64_t>(1) << 10;
  } else if (unit == QStringLiteral("m") || unit == QStringLiteral("mb")) {
    mult = static_cast<uint64_t>(1) << 20;
  } else if (unit == QStringLiteral("g") || unit == QStringLiteral("gb")) {
    mult = static_cast<uint64_t>(1) << 30;
  } else if (unit == QStringLiteral("t") || unit == QStringLiteral("tb")) {
    mult = static_cast<uint64_t>(1) << 40;
  } else if (unit == QStringLiteral("b") || unit.isEmpty()) {
    mult = 1;
  }
  return static_cast<uint64_t>(number * static_cast<double>(mult));
}

QString format_bytes(uint64_t bytes) {
  if (bytes == 0) {
    return QStringLiteral("N/A");
  }

  const double gb = static_cast<double>(bytes) / static_cast<double>(1ull << 30);
  if (gb >= 1.0) {
    return QStringLiteral("%1 GB").arg(QString::number(gb, 'f', 1));
  }

  const double mb = static_cast<double>(bytes) / static_cast<double>(1ull << 20);
  return QStringLiteral("%1 MB").arg(QString::number(mb, 'f', 0));
}

int method_level_or_default(const QComboBox* level_combo) {
  bool ok = false;
  const int level = level_combo->currentData().toInt(&ok);
  if (!ok) {
    return 5;
  }
  return std::clamp(level, 0, 9);
}

QString effective_method_for_format(const QString& format_id, const QString& method) {
  if (method == QStringLiteral("PPMd") && format_id == QStringLiteral("zip")) {
    return QStringLiteral("PPMdZip");
  }
  return method;
}

uint64_t lzma_auto_dict_for_level(int level) {
  level = std::clamp(level, 0, 9);
  if (level <= 4) {
    return static_cast<uint64_t>(1) << (level * 2 + 16);
  }

  const int middle_level_limit = static_cast<int>(sizeof(size_t)) / 2 + 4;
  if (level <= middle_level_limit) {
    return static_cast<uint64_t>(1) << (level + 20);
  }
  return static_cast<uint64_t>(1)
         << (static_cast<int>(sizeof(size_t)) / 2 + 24);
}

uint64_t bzip2_auto_dict_for_level(int level) {
  level = std::clamp(level, 0, 9);
  if (level >= 5) {
    return static_cast<uint64_t>(900) << 10;
  }
  if (level >= 3) {
    return static_cast<uint64_t>(500) << 10;
  }
  return static_cast<uint64_t>(100) << 10;
}

QString dictionary_size_data(uint64_t bytes) {
  if (bytes == 0) {
    return QStringLiteral("0");
  }
  if ((bytes & ((static_cast<uint64_t>(1) << 20) - 1)) == 0) {
    return QStringLiteral("%1m").arg(bytes >> 20);
  }
  if ((bytes & ((static_cast<uint64_t>(1) << 10) - 1)) == 0) {
    return QStringLiteral("%1k").arg(bytes >> 10);
  }
  return QString::number(bytes);
}

QString dictionary_size_label(uint64_t bytes, bool is_default) {
  QString text;
  if (bytes == 0) {
    text = QStringLiteral("0 B");
  } else if ((bytes & ((static_cast<uint64_t>(1) << 20) - 1)) == 0) {
    text = QStringLiteral("%1 MB").arg(bytes >> 20);
  } else if ((bytes & ((static_cast<uint64_t>(1) << 10) - 1)) == 0) {
    text = QStringLiteral("%1 KB").arg(bytes >> 10);
  } else {
    text = QStringLiteral("%1 B").arg(bytes);
  }

  return is_default ? QStringLiteral("* %1").arg(text) : text;
}

uint64_t lzma2_chunk_size(uint64_t dictionary_size) {
  uint64_t chunk_size = dictionary_size << 2;
  const uint64_t min_size = static_cast<uint64_t>(1) << 20;
  const uint64_t max_size = static_cast<uint64_t>(1) << 28;
  if (chunk_size < min_size) {
    chunk_size = min_size;
  }
  if (chunk_size > max_size) {
    chunk_size = max_size;
  }
  if (chunk_size < dictionary_size) {
    chunk_size = dictionary_size;
  }
  chunk_size += min_size - 1;
  chunk_size &= ~(min_size - 1);
  return chunk_size;
}

QString size_token_for_bytes(uint64_t bytes) {
  if (bytes == 0) {
    return QStringLiteral("0");
  }
  if ((bytes & ((static_cast<uint64_t>(1) << 30) - 1)) == 0) {
    return QStringLiteral("%1g").arg(bytes >> 30);
  }
  if ((bytes & ((static_cast<uint64_t>(1) << 20) - 1)) == 0) {
    return QStringLiteral("%1m").arg(bytes >> 20);
  }
  if ((bytes & ((static_cast<uint64_t>(1) << 10) - 1)) == 0) {
    return QStringLiteral("%1k").arg(bytes >> 10);
  }
  return QString::number(bytes);
}

QString size_label_for_bytes(uint64_t bytes) {
  if ((bytes & ((static_cast<uint64_t>(1) << 30) - 1)) == 0) {
    return QStringLiteral("%1 GB").arg(bytes >> 30);
  }
  if ((bytes & ((static_cast<uint64_t>(1) << 20) - 1)) == 0) {
    return QStringLiteral("%1 MB").arg(bytes >> 20);
  }
  if ((bytes & ((static_cast<uint64_t>(1) << 10) - 1)) == 0) {
    return QStringLiteral("%1 KB").arg(bytes >> 10);
  }
  return QStringLiteral("%1 B").arg(bytes);
}

uint64_t auto_solid_size_bytes(const FormatRule& rule,
                               const QString& method,
                               int level,
                               uint64_t dictionary_size) {
  if (!rule.solid || level == 0) {
    return 0;
  }

  uint64_t dict = dictionary_size;
  if (dict == 0) {
    dict = static_cast<uint64_t>(1) << 25;
  }

  const uint64_t chunk_size = lzma2_chunk_size(dict);
  uint64_t block_size = chunk_size;

  if (rule.id == QStringLiteral("7z")) {
    uint64_t max_size = static_cast<uint64_t>(1) << 32;
    if (method == QStringLiteral("LZMA2")) {
      block_size = chunk_size << 6;
      max_size = static_cast<uint64_t>(1) << 34;
    } else {
      uint64_t dict_for_method = dict;
      if (method == QStringLiteral("BZip2")) {
        dict_for_method /= 100000;
        if (dict_for_method < 1) {
          dict_for_method = 1;
        }
        dict_for_method *= 100000;
      }
      block_size = dict_for_method << 7;
    }

    const uint64_t min_size = static_cast<uint64_t>(1) << 24;
    if (block_size < min_size) {
      block_size = min_size;
    }
    if (block_size > max_size) {
      block_size = max_size;
    }
  }

  return block_size;
}

}  // namespace z7::ui::gui::compress_internal
