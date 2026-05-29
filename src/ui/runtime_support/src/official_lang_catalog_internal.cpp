#include "official_lang_catalog_internal.h"

#include <QLocale>

namespace z7::ui::runtime_support::official_lang_catalog_internal {

bool is_blank_line(const QString& line) {
  for (const QChar ch : line) {
    if (ch != QLatin1Char(' ') && ch != QLatin1Char('\t')) {
      return false;
    }
  }
  return true;
}

bool is_decimal_number(const QString& line) {
  if (line.isEmpty()) {
    return false;
  }
  for (const QChar ch : line) {
    if (!ch.isDigit()) {
      return false;
    }
  }
  return true;
}

QString decode_lang2_text_line(const QString& line) {
  QString out;
  out.reserve(line.size());

  for (int i = 0; i < line.size(); ++i) {
    const QChar ch = line[i];
    if (ch != QLatin1Char('\\')) {
      out.append(ch);
      continue;
    }

    if (i + 1 >= line.size()) {
      out.append(ch);
      continue;
    }

    const QChar next = line[i + 1];
    ++i;
    if (next == QLatin1Char('n')) {
      out.append(QLatin1Char('\n'));
    } else if (next == QLatin1Char('t')) {
      out.append(QLatin1Char('\t'));
    } else if (next == QLatin1Char('\\')) {
      out.append(QLatin1Char('\\'));
    } else {
      out.append(QLatin1Char('\\'));
      out.append(next);
    }
  }

  return out;
}

QString normalize_locale_name(const QString& locale_name) {
  QString normalized = locale_name.trimmed().toLower();
  normalized.replace(QLatin1Char('_'), QLatin1Char('-'));

  const int at_pos = normalized.indexOf(QLatin1Char('@'));
  if (at_pos >= 0) {
    normalized.truncate(at_pos);
  }
  const int dot_pos = normalized.indexOf(QLatin1Char('.'));
  if (dot_pos >= 0) {
    normalized.truncate(dot_pos);
  }
  return normalized;
}

QString primary_subtag(const QString& lang_id) {
  const QString normalized = normalize_locale_name(lang_id);
  const int dash = normalized.indexOf(QLatin1Char('-'));
  if (dash < 0) {
    return normalized;
  }
  return normalized.left(dash);
}

QString format_line_with_id(uint32_t id, const QString& text) {
  return QStringLiteral("%1 : %2").arg(id).arg(text);
}

z7::platform::qt::PortableSettings make_settings() {
  return z7::platform::qt::PortableSettings();
}

void assign_language_marks(QList<LangInfo>* languages) {
  if (languages == nullptr || languages->isEmpty()) {
    return;
  }

  int english_index = -1;
  for (int i = 0; i < languages->size(); ++i) {
    (*languages)[i].mark.clear();
    if ((*languages)[i].id == QStringLiteral("-")) {
      english_index = i;
    }
  }

  if (english_index >= 0) {
    (*languages)[english_index].mark = QStringLiteral("---");
  }

  QStringList preferred;
  const QStringList ui_languages = QLocale::system().uiLanguages();
  for (const QString& raw : ui_languages) {
    const QString normalized = normalize_locale_name(raw);
    if (!normalized.isEmpty() && !preferred.contains(normalized)) {
      preferred.append(normalized);
    }
    const QString primary = primary_subtag(normalized);
    if (!primary.isEmpty() && !preferred.contains(primary)) {
      preferred.append(primary);
    }
  }

  if (preferred.isEmpty()) {
    return;
  }

  int starred_index = -1;
  for (const QString& wanted : preferred) {
    for (int i = 0; i < languages->size(); ++i) {
      if (i == english_index) {
        continue;
      }
      if ((*languages)[i].id == wanted) {
        starred_index = i;
        break;
      }
    }
    if (starred_index >= 0) {
      break;
    }
  }

  const QString primary = primary_subtag(preferred.front());
  if (starred_index < 0 && !primary.isEmpty()) {
    for (int i = 0; i < languages->size(); ++i) {
      if (i == english_index) {
        continue;
      }
      if (primary_subtag((*languages)[i].id) == primary) {
        starred_index = i;
        break;
      }
    }
  }

  if (starred_index >= 0) {
    (*languages)[starred_index].mark = QStringLiteral("***");
  }

  if (primary.isEmpty()) {
    return;
  }

  for (int i = 0; i < languages->size(); ++i) {
    if (i == english_index || i == starred_index) {
      continue;
    }
    if (primary_subtag((*languages)[i].id) == primary) {
      (*languages)[i].mark = QStringLiteral("+++");
    }
  }
}

}  // namespace z7::ui::runtime_support::official_lang_catalog_internal
