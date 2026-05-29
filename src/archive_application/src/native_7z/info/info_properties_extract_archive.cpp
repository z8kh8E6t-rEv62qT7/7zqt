// src/archive_application/src/native_7z/info/info_properties_extract_archive.cpp
// Role: Archive-link level property extraction and emission.

#include "third_party_adapter/info_properties_extract_internal.h"

#include <array>

namespace z7::app::info_properties_detail {

namespace {

HRESULT get_arc_prop_value(const CCodecs& codecs,
                           const CArchiveLink& archive_link,
                           const UInt32 level,
                           const PROPID prop_id,
                           NWindows::NCOM::CPropVariant& out_prop) {
  out_prop.Clear();
  if (level > static_cast<UInt32>(archive_link.Arcs.Size())) {
    return E_INVALIDARG;
  }

  if (level == static_cast<UInt32>(archive_link.Arcs.Size())) {
    switch (prop_id) {
      case kpidPath:
        if (!archive_link.NonOpen_ArcPath.IsEmpty()) {
          out_prop = archive_link.NonOpen_ArcPath;
        }
        return S_OK;
      case kpidErrorType:
        if (archive_link.NonOpen_ErrorInfo.ErrorFormatIndex >= 0 &&
            static_cast<unsigned>(archive_link.NonOpen_ErrorInfo.ErrorFormatIndex) <
                codecs.Formats.Size()) {
          const wchar_t* format_name = codecs.GetFormatNamePtr(
              static_cast<unsigned>(archive_link.NonOpen_ErrorInfo.ErrorFormatIndex));
          if (format_name != nullptr && format_name[0] != 0) {
            out_prop = UString(format_name);
          }
        }
        return S_OK;
      case kpidErrorFlags: {
        const UInt32 flags = archive_link.NonOpen_ErrorInfo.GetErrorFlags();
        if (flags != 0) {
          out_prop = flags;
        }
        return S_OK;
      }
      case kpidWarningFlags: {
        const UInt32 flags = archive_link.NonOpen_ErrorInfo.GetWarningFlags();
        if (flags != 0) {
          out_prop = flags;
        }
        return S_OK;
      }
      default:
        return S_OK;
    }
  }

  const CArc& arc = archive_link.Arcs[level];
  switch (prop_id) {
    case kpidType:
      if (arc.FormatIndex >= 0 &&
          static_cast<unsigned>(arc.FormatIndex) < codecs.Formats.Size()) {
        const wchar_t* format_name =
            codecs.GetFormatNamePtr(static_cast<unsigned>(arc.FormatIndex));
        if (format_name != nullptr && format_name[0] != 0) {
          out_prop = UString(format_name);
        }
      }
      return S_OK;
    case kpidPath:
      out_prop = arc.Path;
      return S_OK;
    case kpidErrorType:
      if (arc.ErrorInfo.ErrorFormatIndex >= 0 &&
          static_cast<unsigned>(arc.ErrorInfo.ErrorFormatIndex) < codecs.Formats.Size()) {
        const wchar_t* format_name = codecs.GetFormatNamePtr(
            static_cast<unsigned>(arc.ErrorInfo.ErrorFormatIndex));
        if (format_name != nullptr && format_name[0] != 0) {
          out_prop = UString(format_name);
        }
      }
      return S_OK;
    case kpidErrorFlags: {
      const UInt32 flags = arc.ErrorInfo.GetErrorFlags();
      if (flags != 0) {
        out_prop = flags;
      }
      return S_OK;
    }
    case kpidWarningFlags: {
      const UInt32 flags = arc.ErrorInfo.GetWarningFlags();
      if (flags != 0) {
        out_prop = flags;
      }
      return S_OK;
    }
    case kpidOffset: {
      const Int64 offset = arc.GetGlobalOffset();
      if (offset != 0) {
        out_prop.Set_Int64(offset);
      }
      return S_OK;
    }
    case kpidTailSize:
      if (arc.ErrorInfo.TailSize != 0) {
        out_prop = arc.ErrorInfo.TailSize;
      }
      return S_OK;
    default:
      if (arc.Archive == nullptr) {
        return E_FAIL;
      }
      return arc.Archive->GetArchiveProperty(prop_id, &out_prop);
  }
}

HRESULT get_arc_num_props(const CArchiveLink& archive_link,
                          const UInt32 level,
                          UInt32& out_num_props) {
  out_num_props = 0;
  if (level >= static_cast<UInt32>(archive_link.Arcs.Size())) {
    return E_INVALIDARG;
  }
  IInArchive* archive = archive_link.Arcs[level].Archive;
  return archive == nullptr ? E_FAIL : archive->GetNumberOfArchiveProperties(&out_num_props);
}

HRESULT get_arc_prop_info(const CArchiveLink& archive_link,
                          const UInt32 level,
                          const UInt32 index,
                          CMyComBSTR& out_name,
                          PROPID& out_prop_id,
                          VARTYPE& out_var_type) {
  if (level >= static_cast<UInt32>(archive_link.Arcs.Size())) {
    return E_INVALIDARG;
  }
  IInArchive* archive = archive_link.Arcs[level].Archive;
  return archive == nullptr
             ? E_FAIL
             : archive->GetArchivePropertyInfo(index, &out_name, &out_prop_id, &out_var_type);
}

HRESULT get_arc_prop2(const CArchiveLink& archive_link,
                      const UInt32 level,
                      const PROPID prop_id,
                      NWindows::NCOM::CPropVariant& out_prop) {
  out_prop.Clear();
  if (level == 0 || level >= static_cast<UInt32>(archive_link.Arcs.Size())) {
    return E_INVALIDARG;
  }
  const CArc& child = archive_link.Arcs[level];
  const CArc& parent = archive_link.Arcs[level - 1];
  if (parent.Archive == nullptr || child.SubfileIndex == static_cast<UInt32>(-1)) {
    return E_FAIL;
  }
  return parent.Archive->GetProperty(child.SubfileIndex, prop_id, &out_prop);
}

HRESULT get_arc_num_props2(const CArchiveLink& archive_link,
                           const UInt32 level,
                           UInt32& out_num_props) {
  out_num_props = 0;
  if (level == 0 || level >= static_cast<UInt32>(archive_link.Arcs.Size())) {
    return E_INVALIDARG;
  }
  IInArchive* parent = archive_link.Arcs[level - 1].Archive;
  return parent == nullptr ? E_FAIL : parent->GetNumberOfProperties(&out_num_props);
}

HRESULT get_arc_prop_info2(const CArchiveLink& archive_link,
                           const UInt32 level,
                           const UInt32 index,
                           CMyComBSTR& out_name,
                           PROPID& out_prop_id,
                           VARTYPE& out_var_type) {
  if (level == 0 || level >= static_cast<UInt32>(archive_link.Arcs.Size())) {
    return E_INVALIDARG;
  }
  IInArchive* parent = archive_link.Arcs[level - 1].Archive;
  return parent == nullptr
             ? E_FAIL
             : parent->GetPropertyInfo(index, &out_name, &out_prop_id, &out_var_type);
}

std::optional<UInt32> resolve_parent_entry_index(const CArc& parent_arc,
                                                 const std::string& child_entry_path) {
  if (parent_arc.Archive == nullptr) {
    return std::nullopt;
  }

  UInt32 num_items = 0;
  if (parent_arc.Archive->GetNumberOfItems(&num_items) != S_OK) {
    return std::nullopt;
  }

  const std::string needle = normalize_archive_item_path(child_entry_path);
  for (UInt32 i = 0; i < num_items; ++i) {
    const std::string candidate = normalize_archive_item_path(
        archive_get_prop_text(parent_arc.Archive, i, kpidPath));
    if (candidate == needle) {
      return i;
    }
  }
  return std::nullopt;
}

}  // namespace

void append_archive_link_properties_with_offset(
    const CCodecs& codecs,
    const CArchiveLink& archive_link,
    const uint32_t level_offset,
    const bool include_non_open_error,
    std::vector<ArchivePropertyLine>& out_lines,
    const std::atomic<bool>* cancel_requested) {
  const UInt32 num_levels = static_cast<UInt32>(archive_link.Arcs.Size());

  const std::array<PROPID, 10> kSpecProps = {
      kpidPath,
      kpidType,
      kpidErrorType,
      kpidError,
      kpidErrorFlags,
      kpidWarning,
      kpidWarningFlags,
      kpidOffset,
      kpidPhySize,
      kpidTailSize};

  for (UInt32 level2 = 0; level2 < num_levels; ++level2) {
    if (cancel_requested_now(cancel_requested)) {
      return;
    }

    const UInt32 level = num_levels - 1 - level2;
    const std::optional<uint32_t> line_level = level_offset + level;
    UInt32 num_props = 0;
    if (get_arc_num_props(archive_link, level, num_props) != S_OK) {
      continue;
    }

    append_separator(out_lines,
                     PropertyLineKind::kSeparator,
                     ArchivePropertySection::kArchiveProps,
                     ArchivePropertyDisplayGroup::kArchiveProperties,
                     line_level);

    const Int32 spec_count = static_cast<Int32>(kSpecProps.size());
    for (Int32 i = -spec_count; i < static_cast<Int32>(num_props); ++i) {
      if (cancel_requested_now(cancel_requested)) {
        return;
      }

      CMyComBSTR name;
      PROPID prop_id = kpidNoProperty;
      VARTYPE var_type = VT_EMPTY;
      if (i < 0) {
        prop_id = kSpecProps[static_cast<size_t>(i + spec_count)];
      } else if (get_arc_prop_info(
                     archive_link, level, static_cast<UInt32>(i), name, prop_id, var_type) !=
                 S_OK) {
        continue;
      }

      NWindows::NCOM::CPropVariant prop;
      if (get_arc_prop_value(codecs, archive_link, level, prop_id, prop) != S_OK) {
        continue;
      }
      append_property_variant_original(out_lines,
                                       ArchivePropertySection::kArchiveProps,
                                       ArchivePropertyDisplayGroup::kArchiveProperties,
                                       line_level,
                                       prop_id,
                                       name,
                                       prop);
    }

    if (level2 >= num_levels - 1) {
      continue;
    }

    UInt32 num_props2 = 0;
    if (get_arc_num_props2(archive_link, level, num_props2) != S_OK) {
      continue;
    }

    append_separator(out_lines,
                     PropertyLineKind::kSeparatorSmall,
                     ArchivePropertySection::kArchiveProps2,
                     ArchivePropertyDisplayGroup::kArchiveProperties2,
                     line_level);
    for (UInt32 i = 0; i < num_props2; ++i) {
      if (cancel_requested_now(cancel_requested)) {
        return;
      }

      CMyComBSTR name;
      PROPID prop_id = kpidNoProperty;
      VARTYPE var_type = VT_EMPTY;
      if (get_arc_prop_info2(archive_link, level, i, name, prop_id, var_type) != S_OK) {
        continue;
      }

      NWindows::NCOM::CPropVariant prop;
      if (get_arc_prop2(archive_link, level, prop_id, prop) != S_OK) {
        continue;
      }
      append_property_variant_original(out_lines,
                                       ArchivePropertySection::kArchiveProps2,
                                       ArchivePropertyDisplayGroup::kArchiveProperties2,
                                       line_level,
                                       prop_id,
                                       name,
                                       prop);
    }
  }

  if (!include_non_open_error) {
    return;
  }

  bool need_separator = true;
  const std::optional<uint32_t> non_open_level = level_offset + num_levels;
  for (const PROPID prop_id : kSpecProps) {
    if (cancel_requested_now(cancel_requested)) {
      return;
    }

    NWindows::NCOM::CPropVariant prop;
    if (get_arc_prop_value(codecs, archive_link, num_levels, prop_id, prop) != S_OK) {
      continue;
    }
    if (prop.vt == VT_EMPTY || is_zero_error_flags_prop(prop_id, prop)) {
      continue;
    }

    if (need_separator) {
      append_separator(out_lines,
                       PropertyLineKind::kSeparator,
                       ArchivePropertySection::kNonOpenError,
                       ArchivePropertyDisplayGroup::kNonOpenError,
                       non_open_level);
      append_separator(out_lines,
                       PropertyLineKind::kSeparator,
                       ArchivePropertySection::kNonOpenError,
                       ArchivePropertyDisplayGroup::kNonOpenError,
                       non_open_level);
      need_separator = false;
    }
    append_property_variant_original(out_lines,
                                     ArchivePropertySection::kNonOpenError,
                                     ArchivePropertyDisplayGroup::kNonOpenError,
                                     non_open_level,
                                     prop_id,
                                     nullptr,
                                     prop);
  }
}

void append_archive_props2_for_parent_entry(
    const CArc& parent_arc,
    const std::string& child_entry_path,
    const std::optional<uint32_t> level,
    std::vector<ArchivePropertyLine>& out_lines,
    const std::atomic<bool>* cancel_requested) {
  const std::optional<UInt32> resolved_index =
      resolve_parent_entry_index(parent_arc, child_entry_path);
  if (!resolved_index.has_value() || parent_arc.Archive == nullptr) {
    return;
  }

  UInt32 num_props = 0;
  if (parent_arc.Archive->GetNumberOfProperties(&num_props) != S_OK) {
    return;
  }

  append_separator(out_lines,
                   PropertyLineKind::kSeparatorSmall,
                   ArchivePropertySection::kArchiveProps2,
                   ArchivePropertyDisplayGroup::kArchiveProperties2,
                   level);

  for (UInt32 i = 0; i < num_props; ++i) {
    if (cancel_requested_now(cancel_requested)) {
      return;
    }

    CMyComBSTR name;
    PROPID prop_id = kpidNoProperty;
    VARTYPE var_type = VT_EMPTY;
    if (parent_arc.Archive->GetPropertyInfo(i, &name, &prop_id, &var_type) != S_OK) {
      continue;
    }

    NWindows::NCOM::CPropVariant prop;
    if (parent_arc.Archive->GetProperty(*resolved_index, prop_id, &prop) != S_OK) {
      continue;
    }

    append_property_variant_original(out_lines,
                                     ArchivePropertySection::kArchiveProps2,
                                     ArchivePropertyDisplayGroup::kArchiveProperties2,
                                     level,
                                     prop_id,
                                     name,
                                     prop);
  }
}

void append_archive_link_properties(const CCodecs& codecs,
                                    const CArchiveLink& archive_link,
                                    std::vector<ArchivePropertyLine>& out_lines,
                                    const std::atomic<bool>* cancel_requested) {
  append_archive_link_properties_with_offset(
      codecs, archive_link, 0, true, out_lines, cancel_requested);
}

}  // namespace z7::app::info_properties_detail
