#include "compress_format_catalog.h"

#include <algorithm>
#include <string>
#include <unordered_set>
#include <vector>

#include "common/ascii_text.h"
#include "common/archive_type_normalization.h"
#include "third_party_adapter/third_party_adapter.h"

#ifdef Z7_EXTERNAL_CODECS
HRESULT LoadGlobalCodecs();
#endif

namespace z7::app {
namespace {

std::string ustring_to_utf8(const UString& value) {
  AString utf8;
  ConvertUnicodeToUTF8(value, utf8);
  return std::string(utf8.Ptr(), static_cast<size_t>(utf8.Len()));
}

std::string normalize_type_id(std::string name) {
  return z7::common::canonical_archive_type_token_copy(name);
}

void sort_entries(std::vector<CompressFormatCatalogEntry>& entries) {
  std::sort(entries.begin(),
            entries.end(),
            [](const CompressFormatCatalogEntry& lhs,
               const CompressFormatCatalogEntry& rhs) {
              const std::string lhs_display =
                  z7::common::to_lower_ascii_copy(lhs.display_name);
              const std::string rhs_display =
                  z7::common::to_lower_ascii_copy(rhs.display_name);
              if (lhs_display != rhs_display) {
                return lhs_display < rhs_display;
              }
              return lhs.type_id < rhs.type_id;
            });
}

}  // namespace

std::vector<CompressFormatCatalogEntry> list_update_archive_formats() {
  std::vector<CompressFormatCatalogEntry> entries;

#ifdef Z7_EXTERNAL_CODECS
  const HRESULT load_global_res = ::LoadGlobalCodecs();
  if (load_global_res != S_OK) {
    return {};
  }
#endif

  CCodecs codecs;
  const HRESULT load_codecs_res = codecs.Load();
  if (load_codecs_res != S_OK) {
    return {};
  }

  std::unordered_set<std::string> seen_ids;
  seen_ids.reserve(static_cast<size_t>(codecs.Formats.Size()));

  for (unsigned i = 0; i < codecs.Formats.Size(); ++i) {
    const CArcInfoEx& info = codecs.Formats[i];
    if (!info.UpdateEnabled) {
      continue;
    }

    std::string display_name = ustring_to_utf8(info.Name);
    if (display_name.empty()) {
      continue;
    }
    const std::string type_id = normalize_type_id(display_name);
    if (type_id.empty()) {
      continue;
    }

    if (!seen_ids.insert(type_id).second) {
      continue;
    }
    entries.push_back({type_id, display_name, info.Flags_KeepName()});
  }

  sort_entries(entries);
  return entries;
}

}  // namespace z7::app
