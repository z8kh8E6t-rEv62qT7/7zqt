// Role: Shared archive suffix catalog for UI-layer archive detection.

#include "archive_suffix_catalog.h"

#include <QFileInfo>
#include <QSet>

namespace z7::ui::common {
    namespace {

        QStringList const& simple_archive_suffixes() {
            static QStringList const kSuffixes = {QStringLiteral("7z"),
                                                  QStringLiteral("zip"),
                                                  QStringLiteral("rar"),
                                                  QStringLiteral("001"),
                                                  QStringLiteral("cab"),
                                                  QStringLiteral("iso"),
                                                  QStringLiteral("xz"),
                                                  QStringLiteral("txz"),
                                                  QStringLiteral("lzma"),
                                                  QStringLiteral("lz"),
                                                  QStringLiteral("tar"),
                                                  QStringLiteral("cpio"),
                                                  QStringLiteral("bz2"),
                                                  QStringLiteral("bzip2"),
                                                  QStringLiteral("tbz2"),
                                                  QStringLiteral("tbz"),
                                                  QStringLiteral("gz"),
                                                  QStringLiteral("gzip"),
                                                  QStringLiteral("tgz"),
                                                  QStringLiteral("tpz"),
                                                  QStringLiteral("zst"),
                                                  QStringLiteral("tzst"),
                                                  QStringLiteral("z"),
                                                  QStringLiteral("taz"),
                                                  QStringLiteral("lzh"),
                                                  QStringLiteral("lha"),
                                                  QStringLiteral("rpm"),
                                                  QStringLiteral("deb"),
                                                  QStringLiteral("arj"),
                                                  QStringLiteral("wim"),
                                                  QStringLiteral("swm"),
                                                  QStringLiteral("esd"),
                                                  QStringLiteral("xar"),
                                                  QStringLiteral("pkg"),
                                                  QStringLiteral("dmg"),
                                                  QStringLiteral("hfs"),
                                                  QStringLiteral("apfs"),
                                                  QStringLiteral("fat"),
                                                  QStringLiteral("ntfs"),
                                                  QStringLiteral("squashfs"),
                                                  QStringLiteral("vhd"),
                                                  QStringLiteral("vhdx")};
            return kSuffixes;
        }

        QSet<QString> const& simple_archive_suffix_set() {
            static QSet<QString> const kSet = [] {
                QSet<QString> out;
                for (QString const& suffix : simple_archive_suffixes()) {
                    out.insert(suffix);
                }
                return out;
            }();
            return kSet;
        }

        QSet<QString> const& archive_suffix_aliases() {
            static QSet<QString> const kAliases = {QStringLiteral("tar.gz"),
                                                   QStringLiteral("tar.bz2"),
                                                   QStringLiteral("tar.xz"),
                                                   QStringLiteral("tar.zst"),
                                                   QStringLiteral("tar.z"),
                                                   QStringLiteral("tar.lzma"),
                                                   QStringLiteral("tar.lz")};
            return kAliases;
        }

        QString normalized_suffix_token(QString const& value) {
            return value.trimmed().toLower();
        }

    } // namespace

    QStringList const& ordered_archive_suffixes() {
        return simple_archive_suffixes();
    }

    bool is_archive_suffix(QString const& suffix) {
        QString const normalized = normalized_suffix_token(suffix);
        return !normalized.isEmpty() && simple_archive_suffix_set().contains(normalized);
    }

    bool is_archive_suffix_or_alias(QString const& suffix_or_alias) {
        QString const normalized = normalized_suffix_token(suffix_or_alias);
        return !normalized.isEmpty()
            && (simple_archive_suffix_set().contains(normalized) || archive_suffix_aliases().contains(normalized));
    }

    bool archive_name_has_known_suffix(QString const& name) {
        QFileInfo const info(name);
        if (is_archive_suffix(info.suffix())) {
            return true;
        }

        QString const complete_suffix = normalized_suffix_token(info.completeSuffix());
        return !complete_suffix.isEmpty() && is_archive_suffix_or_alias(complete_suffix);
    }

} // namespace z7::ui::common
