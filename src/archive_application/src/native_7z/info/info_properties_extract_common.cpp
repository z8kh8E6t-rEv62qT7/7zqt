// src/archive_application/src/native_7z/info/info_properties_extract_common.cpp
// Role: Shared helper logic for split info_properties_extract units.

#include "third_party_adapter/info_properties_extract_internal.h"

namespace z7::app::info_properties_detail {

bool cancel_requested_now(const std::atomic<bool>* cancel_requested) {
  return cancel_requested != nullptr &&
         cancel_requested->load(std::memory_order_relaxed);
}

bool is_zero_error_flags_prop(const PROPID prop_id,
                              const NWindows::NCOM::CPropVariant& prop) {
  if (prop_id != kpidErrorFlags && prop_id != kpidWarningFlags) {
    return false;
  }
  return GetOpenArcErrorFlags(prop) == 0;
}

bool append_property_variant_original(std::vector<ArchivePropertyLine>& out_lines,
                                      const ArchivePropertySection section,
                                      const ArchivePropertyDisplayGroup display_group,
                                      const std::optional<uint32_t> level,
                                      const PROPID prop_id,
                                      const wchar_t* name,
                                      const NWindows::NCOM::CPropVariant& prop) {
  if (prop.vt == VT_EMPTY || is_zero_error_flags_prop(prop_id, prop)) {
    return false;
  }

  bool appended = false;
  if (prop_id == kpidErrorType) {
    UString converted;
    ConvertPropertyToString2(converted, prop, prop_id, 9);
    if (!z7::common::trim_ascii_space_copy(ustring_to_utf8(converted)).empty()) {
      append_line(out_lines,
                  PropertyLineKind::kPair,
                  section,
                  display_group,
                  level,
                  std::nullopt,
                  "Open WARNING:",
                  "Cannot open the file as expected archive type");
      appended = true;
    }
  }

  const size_t before = out_lines.size();
  append_property_variant_line(out_lines, section, display_group, level, prop_id, name, prop);
  return appended || out_lines.size() != before;
}

}  // namespace z7::app::info_properties_detail
