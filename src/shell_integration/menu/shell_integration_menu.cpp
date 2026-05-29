#include "shell_integration_menu.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>

#include "common/archive_type_normalization.h"

namespace z7::shell_integration {
namespace {

struct ContextMenuFlagMapping {
  const char* action_id;
  std::uint32_t flags;
};

constexpr std::uint32_t kContextMenuFlagExtract = 1u << 0;
constexpr std::uint32_t kContextMenuFlagExtractHere = 1u << 1;
constexpr std::uint32_t kContextMenuFlagExtractTo = 1u << 2;
constexpr std::uint32_t kContextMenuFlagTest = 1u << 4;
constexpr std::uint32_t kContextMenuFlagOpen = 1u << 5;
constexpr std::uint32_t kContextMenuFlagOpenAs = 1u << 6;
constexpr std::uint32_t kContextMenuFlagCompress = 1u << 8;
constexpr std::uint32_t kContextMenuFlagCompressTo7z = 1u << 9;
constexpr std::uint32_t kContextMenuFlagCompressToZip = 1u << 12;
constexpr std::uint32_t kContextMenuFlagCrcCascaded = 1u << 30;
constexpr std::uint32_t kContextMenuFlagCrcTopLevel = 1u << 31;

const std::array<ContextMenuFlagMapping, 10>& context_menu_flag_mappings() {
  static constexpr std::array<ContextMenuFlagMapping, 10> kMappings = {{
      {kActionOpen, kContextMenuFlagOpen},
      {kActionOpenAsMenu, kContextMenuFlagOpenAs},
      {kActionExtractFiles, kContextMenuFlagExtract},
      {kActionExtractHere, kContextMenuFlagExtractHere},
      {kActionExtractTo, kContextMenuFlagExtractTo},
      {kActionTestArchive, kContextMenuFlagTest},
      {kActionAddToArchive, kContextMenuFlagCompress},
      {kActionAddTo7z, kContextMenuFlagCompressTo7z},
      {kActionAddToZip, kContextMenuFlagCompressToZip},
      {kActionCrcShaMenu,
       kContextMenuFlagCrcTopLevel | kContextMenuFlagCrcCascaded},
  }};
  return kMappings;
}

}  // namespace

const std::array<ShellIntegrationHashMethodDef, 11>&
shell_integration_hash_method_defs() {
  static const std::array<ShellIntegrationHashMethodDef, 11> kDefs = {{
      {"CRC-32", "CRC32"},
      {"CRC-64", "CRC64"},
      {"XXH64", "XXH64"},
      {"MD5", "MD5"},
      {"SHA-1", "SHA1"},
      {"SHA-256", "SHA256"},
      {"SHA-384", "SHA384"},
      {"SHA-512", "SHA512"},
      {"SHA3-256", "SHA3-256"},
      {"BLAKE2sp", "BLAKE2sp"},
      {"*", "*"},
  }};
  return kDefs;
}

QString shell_integration_normalize_fs_name(QString name) {
  static const QRegularExpression kInvalidChars(
      QStringLiteral(R"([\\/:*?\"<>|])"));

  name.replace(kInvalidChars, QStringLiteral("_"));
  name = name.trimmed();
  if (name.isEmpty()) {
    return QStringLiteral("Archive");
  }
  return name;
}

bool shell_integration_do_need_extract_name(const QString& name) {
  static const QSet<QString> kExtractExcluded = [] {
    QSet<QString> out;
    const QString joined =
        QStringLiteral(
            "3gp aac ans ape asc asm asp aspx avi awk "
            "bas bat bmp "
            "c cs cls clw cmd cpp csproj css ctl cxx "
            "def dep dlg dsp dsw "
            "eps "
            "f f77 f90 f95 fla flac frm "
            "gif "
            "h hpp hta htm html hxx "
            "ico idl inc ini inl "
            "java jpeg jpg js "
            "la lnk log "
            "mak manifest wmv mov mp3 mp4 mpe mpeg mpg m4a "
            "ofr ogg "
            "pac pas pdf php php3 php4 php5 phptml pl pm png ps py pyo "
            "ra rb rc reg rka rm rtf "
            "sed sh shn shtml sln sql srt swa "
            "tcl tex tiff tta txt "
            "vb vcproj vbs "
            "mkv wav webm wma wv "
            "xml xsd xsl xslt");
    for (const QString& part :
         joined.split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
      out.insert(part.toLower());
    }
    return out;
  }();

  const QString ext = QFileInfo(name).suffix().toLower();
  if (ext.isEmpty()) {
    return true;
  }
  return !kExtractExcluded.contains(ext);
}

QString shell_integration_extract_subfolder_name(const QString& archive_name) {
  auto is_extract_arc_ext = [](const QString& ext) {
    static const QSet<QString> kArcExts = {
        QStringLiteral("7z"),
        QStringLiteral("rar"),
        QStringLiteral("zip")};
    const QString lowered = ext.toLower();
    return kArcExts.contains(lowered) ||
           z7::common::is_gzip_family_archive_suffix(lowered.toStdString()) ||
           z7::common::is_bzip2_family_archive_suffix(lowered.toStdString());
  };

  const int dot_pos = archive_name.lastIndexOf(QLatin1Char('.'));
  if (dot_pos < 0) {
    return shell_integration_normalize_fs_name(archive_name) +
           QStringLiteral("~");
  }

  const QString ext = archive_name.mid(dot_pos + 1);
  QString base = archive_name.left(dot_pos).trimmed();
  const int dot2 = base.lastIndexOf(QLatin1Char('.'));
  if (dot2 > 0) {
    const QString ext2 = base.mid(dot2 + 1);
    const bool remove_prev =
        (ext.compare(QStringLiteral("001"), Qt::CaseInsensitive) == 0 &&
         is_extract_arc_ext(ext2)) ||
        (ext.compare(QStringLiteral("rar"), Qt::CaseInsensitive) == 0 &&
         (ext2.compare(QStringLiteral("part001"), Qt::CaseInsensitive) == 0 ||
          ext2.compare(QStringLiteral("part01"), Qt::CaseInsensitive) == 0 ||
          ext2.compare(QStringLiteral("part1"), Qt::CaseInsensitive) == 0));
    if (remove_prev) {
      base = base.left(dot2).trimmed();
    }
  }
  return shell_integration_normalize_fs_name(base);
}

QString shell_integration_create_archive_name_from_paths(
    const QStringList& paths,
    bool is_hash,
    QString* base_name_out) {
  bool keep_name = is_hash;
  QString name = QStringLiteral("Archive");

  QFileInfo single_info;
  bool has_single_info = false;

  if (paths.size() == 1) {
    single_info = QFileInfo(paths.front());
    has_single_info = single_info.exists();
  } else if (!paths.isEmpty()) {
    const QFileInfo first(paths.front());
    const QDir parent = first.absoluteDir();
    QString parent_name = parent.dirName();
#ifdef Q_OS_WIN
    if (parent_name.isEmpty()) {
      const QString root = parent.absolutePath();
      if (!root.isEmpty()) {
        parent_name = root.left(1);
      }
    }
#endif
    if (!parent_name.isEmpty()) {
      name = parent_name;
    }
  }

  if (has_single_info) {
    name = single_info.fileName();
    if (!single_info.isDir() && !keep_name) {
      const int first_dot = name.indexOf(QLatin1Char('.'));
      if (first_dot > 0 && name.indexOf(QLatin1Char('.'), first_dot + 1) < 0) {
        name = name.left(first_dot);
      }
    }
  }

  name = shell_integration_normalize_fs_name(name);
  if (base_name_out != nullptr) {
    *base_name_out = name;
  }

  const QSet<QString> valid_exts =
      is_hash
          ? QSet<QString>{QStringLiteral("sha256")}
          : QSet<QString>{QStringLiteral("7z"),
                          QStringLiteral("zip"),
                          QStringLiteral("tar"),
                          QStringLiteral("wim")};

  bool simple_allowed = true;
  QSet<int> used_ids;
  for (const QString& path : paths) {
    const QString file_name = QFileInfo(path).fileName();
    if (!file_name.startsWith(name, Qt::CaseInsensitive)) {
      continue;
    }

    const QString suffix = file_name.mid(name.size());
    const int dot_pos = suffix.lastIndexOf(QLatin1Char('.'));
    if (dot_pos < 0) {
      continue;
    }

    const QString ext = suffix.mid(dot_pos + 1).toLower();
    if (!valid_exts.contains(ext)) {
      continue;
    }

    const QString marker = suffix.left(dot_pos);
    if (marker.isEmpty()) {
      simple_allowed = false;
      continue;
    }

    if (!marker.startsWith(QLatin1Char('_'))) {
      continue;
    }

    bool ok = false;
    const int value = marker.mid(1).toInt(&ok);
    if (ok && value > 0) {
      used_ids.insert(value);
    }
  }

  if (!simple_allowed) {
    int candidate = 2;
    while (used_ids.contains(candidate)) {
      ++candidate;
    }
    name += QStringLiteral("_%1").arg(candidate);
  }

  return name;
}

QStringList shell_integration_context_menu_action_ids() {
  return {
      QString::fromLatin1(kActionOpen),
      QString::fromLatin1(kActionOpenAsMenu),
      QString::fromLatin1(kActionExtractFiles),
      QString::fromLatin1(kActionExtractHere),
      QString::fromLatin1(kActionExtractTo),
      QString::fromLatin1(kActionTestArchive),
      QString::fromLatin1(kActionAddToArchive),
      QString::fromLatin1(kActionAddTo7z),
      QString::fromLatin1(kActionAddToZip),
      QString::fromLatin1(kActionCrcShaMenu)};
}

QStringList normalize_shell_integration_visible_actions(const QStringList& action_ids) {
  QSet<QString> requested;
  for (const QString& action_id : action_ids) {
    const QString normalized = action_id.trimmed();
    if (!normalized.isEmpty()) {
      requested.insert(normalized);
    }
  }

  QStringList out;
  for (const QString& action_id : shell_integration_context_menu_action_ids()) {
    if (requested.contains(action_id)) {
      out.push_back(action_id);
    }
  }
  return out;
}

QStringList default_shell_integration_visible_actions() {
  return shell_integration_context_menu_action_ids();
}

std::uint32_t shell_integration_context_menu_flags_from_visible_actions(
    const QStringList& action_ids) {
  const QStringList normalized =
      normalize_shell_integration_visible_actions(action_ids);
  const QSet<QString> visible(normalized.cbegin(), normalized.cend());

  std::uint32_t flags = 0;
  for (const ContextMenuFlagMapping& mapping : context_menu_flag_mappings()) {
    if (visible.contains(QString::fromLatin1(mapping.action_id))) {
      flags |= mapping.flags;
    }
  }
  return flags;
}

QStringList shell_integration_visible_actions_from_context_menu_flags(
    std::uint32_t flags) {
  QStringList actions;
  for (const ContextMenuFlagMapping& mapping : context_menu_flag_mappings()) {
    if ((flags & mapping.flags) != 0) {
      actions.push_back(QString::fromLatin1(mapping.action_id));
    }
  }
  return normalize_shell_integration_visible_actions(actions);
}

}  // namespace z7::shell_integration
