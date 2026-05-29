// src/ui/filemanager/src/main_window/state/state_names_utils.cpp
// Role: Menu text, capability keys, icon helpers, and error formatting.

#include "main_window/deps.h"
#include "main_window/internal.h"

#include "archive_suffix_catalog.h"

namespace z7::ui::filemanager {

void reduce_menu_string(QString* value) {
  if (value == nullptr) {
    return;
  }
  constexpr int kMax = 64;
  if (value->size() <= kMax) {
    return;
  }
  const int mid = kMax / 2;
  value->remove(mid, value->size() - kMax);
  value->insert(mid, QStringLiteral(" ... "));
}

QString quoted_reduced_menu_name(QString value) {
  reduce_menu_string(&value);
  value.replace(QStringLiteral("&"), QStringLiteral("&&"));
  return QStringLiteral("\"%1\"").arg(value);
}

QString format_menu_name(uint32_t lang_id,
                         const QString& value) {
  const QString quoted = quoted_reduced_menu_name(value);
  return z7::ui::runtime_support::LF(lang_id, {quoted});
}

bool is_archive_file(const QString& path) {
  return z7::ui::common::archive_name_has_known_suffix(path);
}

const QSet<QString>& always_start_extensions() {
  static const QSet<QString> kExts = [] {
    QSet<QString> out;
    const QString joined =
        QStringLiteral(
            "exe bat ps1 com lnk "
            "chm "
            "msi doc dot xls ppt pps wps wpt wks xlr wdb vsd pub "
            "docx docm dotx dotm xlsx xlsm xltx xltm xlsb xps "
            "xlam pptx pptm potx potm ppam ppsx ppsm vsdx xsn "
            "mpp "
            "msg "
            "dwf "
            "flv swf "
            "epub "
            "odt ods "
            "wb3 "
            "pdf "
            "ps "
            "txt "
            "xml xsd xsl xslt hxk hxc htm html xhtml xht mht mhtml htw asp aspx css cgi jsp shtml "
            "h hpp hxx c cpp cxx m mm go swift "
            "awk sed hta js json php php3 php4 php5 phptml pl pm py pyo rb tcl ts vbs "
            "asm "
            "mak clw csproj vcproj sln dsp dsw");
    const QStringList parts =
        joined.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    for (const QString& item : parts) {
      out.insert(item.toLower());
    }
    return out;
  }();
  return kExts;
}

bool should_always_start_externally(const QString& path) {
  const QString ext = QFileInfo(path).suffix().toLower();
  return !ext.isEmpty() && always_start_extensions().contains(ext);
}

QString action_capability_key(ActionCapabilityKey key) {
  switch (key) {
    case ActionCapabilityKey::kSplit:
      return QStringLiteral("Split");
    case ActionCapabilityKey::kCombine:
      return QStringLiteral("Combine");
    case ActionCapabilityKey::kComment:
      return QStringLiteral("Comment");
    case ActionCapabilityKey::kLink:
      return QStringLiteral("Link");
    case ActionCapabilityKey::kAlternateStreams:
      return QStringLiteral("AlternateStreams");
    case ActionCapabilityKey::kSelect:
      return QStringLiteral("Select");
    case ActionCapabilityKey::kDeselect:
      return QStringLiteral("Deselect");
    case ActionCapabilityKey::kSelectByType:
      return QStringLiteral("SelectByType");
    case ActionCapabilityKey::kDeselectByType:
      return QStringLiteral("DeselectByType");
    case ActionCapabilityKey::kAddToFavorites:
      return QStringLiteral("AddToFavorites");
    case ActionCapabilityKey::kContents:
      return QStringLiteral("Contents");
    case ActionCapabilityKey::kTempFiles:
      return QStringLiteral("TempFiles");
    case ActionCapabilityKey::kSevenZipOpenAs:
      return QStringLiteral("SevenZipOpenAs");
  }

  return QStringLiteral("Unknown");
}

QIcon load_masked_toolbar_icon(const QString& resource_path) {
  QImage image(resource_path);
  if (image.isNull()) {
    return QIcon(resource_path);
  }

  image = image.convertToFormat(QImage::Format_ARGB32);
  constexpr QRgb kMaskColor = 0x00FF00FFu;  // RGB(255,0,255), like original AddMasked()

  for (int y = 0; y < image.height(); ++y) {
    QRgb* scanline = reinterpret_cast<QRgb*>(image.scanLine(y));
    for (int x = 0; x < image.width(); ++x) {
      const QRgb pixel = scanline[x];
      if ((pixel & 0x00FFFFFFu) == kMaskColor) {
        scanline[x] = pixel & 0x00FFFFFFu;
      }
    }
  }

  return QIcon(QPixmap::fromImage(image));
}

QString stable_error_message(int error_domain) {
  const auto domain =
      static_cast<z7::app::ArchiveErrorDomain>(error_domain);
  switch (domain) {
    case z7::app::ArchiveErrorDomain::kPassword:
      return QStringLiteral("Password required or incorrect.");
    case z7::app::ArchiveErrorDomain::kUnsupportedFormat:
      return QStringLiteral("Archive format is not supported.");
    case z7::app::ArchiveErrorDomain::kIo:
      return QStringLiteral("File I/O error.");
    case z7::app::ArchiveErrorDomain::kPartialSuccess:
      return QStringLiteral("Operation completed with warnings.");
    case z7::app::ArchiveErrorDomain::kInvalidArguments:
      return QStringLiteral("Invalid request arguments.");
    case z7::app::ArchiveErrorDomain::kBackendUnavailable:
      return QStringLiteral("Archive backend is unavailable.");
    case z7::app::ArchiveErrorDomain::kBudgetExceeded:
      return QStringLiteral("Extract budget exceeded.");
    case z7::app::ArchiveErrorDomain::kCanceled:
      return z7::ui::runtime_support::L(402);
    case z7::app::ArchiveErrorDomain::kNone:
    case z7::app::ArchiveErrorDomain::kUnknown:
      return QStringLiteral("Unknown archive error.");
  }

  return QStringLiteral("Unknown archive error.");
}

}  // namespace z7::ui::filemanager
