// src/archive_application/src/native_7z/info/info_properties_extract_selected.cpp
// Role: Selected-item property extraction and raw-prop expansion.

#include "third_party_adapter/info_properties_extract_internal.h"

#include <array>

namespace z7::app::info_properties_detail {

namespace {

bool get_selected_item_property(const CArc& arc,
                                const SelectedPropertyItem& item,
                                const bool flat_view,
                                const PROPID prop_id,
                                NWindows::NCOM::CPropVariant& out_prop) {
  out_prop.Clear();

  if (!item.is_dir) {
    switch (prop_id) {
      case kpidIsDir:
        out_prop = false;
        return true;
      case kpidName:
        out_prop = utf8_to_ustring(item.name);
        return true;
      case kpidPrefix:
        if (!flat_view) {
          return false;
        }
        out_prop = utf8_to_ustring(item.path_prefix);
        return true;
      default:
        if (arc.Archive == nullptr || !item.has_arc_index) {
          return false;
        }
        return arc.Archive->GetProperty(item.arc_index, prop_id, &out_prop) == S_OK;
    }
  }

  switch (prop_id) {
    case kpidIsDir:
      out_prop = true;
      return true;
    case kpidName:
      out_prop = utf8_to_ustring(item.name);
      return true;
    case kpidPrefix:
      if (!flat_view) {
        return false;
      }
      out_prop = utf8_to_ustring(item.path_prefix);
      return true;
    case kpidSize:
      if (!flat_view) {
        out_prop = static_cast<UInt64>(item.size);
        return true;
      }
      if (!item.is_leaf_dir) {
        return false;
      }
      break;
    case kpidPackSize:
      if (!flat_view) {
        out_prop = static_cast<UInt64>(item.pack_size);
        return true;
      }
      if (!item.is_leaf_dir) {
        return false;
      }
      break;
    case kpidNumSubDirs:
      out_prop = static_cast<UInt64>(item.num_sub_dirs);
      return true;
    case kpidNumSubFiles:
      out_prop = static_cast<UInt64>(item.num_sub_files);
      return true;
    case kpidCRC: {
      if (arc.Archive != nullptr && item.has_arc_index && item.allow_archive_item_props &&
          arc.Archive->GetProperty(item.arc_index, prop_id, &out_prop) == S_OK &&
          out_prop.vt != VT_EMPTY) {
        return true;
      }
      if (item.crc_defined) {
        out_prop = item.crc;
        return true;
      }
      return false;
    }
    default:
      if (arc.Archive == nullptr || !item.has_arc_index || !item.allow_archive_item_props) {
        return false;
      }
      return arc.Archive->GetProperty(item.arc_index, prop_id, &out_prop) == S_OK;
  }

  if (arc.Archive == nullptr || !item.has_arc_index || !item.allow_archive_item_props) {
    return false;
  }
  return arc.Archive->GetProperty(item.arc_index, prop_id, &out_prop) == S_OK;
}

bool get_selected_item_uint64(const CArc& arc,
                              const SelectedPropertyItem& item,
                              const bool flat_view,
                              const PROPID prop_id,
                              uint64_t& out_value) {
  NWindows::NCOM::CPropVariant prop;
  if (!get_selected_item_property(arc, item, flat_view, prop_id, prop)) {
    return false;
  }
  UInt64 converted = 0;
  if (!ConvertPropVariantToUInt64(prop, converted)) {
    return false;
  }
  out_value = static_cast<uint64_t>(converted);
  return true;
}

void append_all_raw_item_properties(const CArc& arc,
                                    const UInt32 arc_index,
                                    std::vector<ArchivePropertyLine>& out_lines,
                                    const std::atomic<bool>* cancel_requested) {
  if (arc.GetRawProps == nullptr || arc_index == kInvalidArcIndex) {
    return;
  }

  UInt32 num_raw_props = 0;
  if (arc.GetRawProps->GetNumRawProps(&num_raw_props) != S_OK) {
    return;
  }

  for (UInt32 i = 0; i < num_raw_props; ++i) {
    if (cancel_requested_now(cancel_requested)) {
      return;
    }
    CMyComBSTR name;
    PROPID raw_prop_id = kpidNoProperty;
    if (arc.GetRawProps->GetRawPropInfo(i, &name, &raw_prop_id) != S_OK) {
      continue;
    }

    const void* data = nullptr;
    UInt32 data_size = 0;
    UInt32 prop_type = 0;
    if (arc.GetRawProps->GetRawProp(
            arc_index, raw_prop_id, &data, &data_size, &prop_type) != S_OK) {
      continue;
    }

    const std::string value = raw_property_data_to_text(raw_prop_id, data, data_size);
    if (value.empty()) {
      continue;
    }

    append_line(out_lines,
                PropertyLineKind::kPair,
                ArchivePropertySection::kSelectedItemRaw,
                ArchivePropertyDisplayGroup::kSelectedItemRawProperties,
                std::nullopt,
                static_cast<uint32_t>(raw_prop_id),
                property_name_utf8(raw_prop_id, name),
                value);
  }
}

}  // namespace

uint64_t get_selected_item_unpacked_size(const CArc& arc,
                                         const SelectedPropertyItem& item,
                                         const bool flat_view) {
  if (item.is_dir && !flat_view) {
    return item.size;
  }

  uint64_t value = 0;
  if (get_selected_item_uint64(arc, item, flat_view, kpidSize, value)) {
    return value;
  }

  if (!item.has_arc_index) {
    return 0;
  }

  UInt64 item_size = 0;
  bool size_defined = false;
  if (arc.GetItem_Size(item.arc_index, item_size, size_defined) == S_OK && size_defined) {
    return static_cast<uint64_t>(item_size);
  }
  return 0;
}

uint64_t get_selected_item_pack_size(const CArc& arc,
                                     const SelectedPropertyItem& item,
                                     const bool flat_view) {
  if (item.is_dir && !flat_view) {
    return item.pack_size;
  }

  uint64_t value = 0;
  if (get_selected_item_uint64(arc, item, flat_view, kpidPackSize, value)) {
    return value;
  }
  return 0;
}

void append_selected_item_properties(const CArc& arc,
                                     const SelectedPropertyItem& selected_item,
                                     const bool flat_view,
                                     std::vector<ArchivePropertyLine>& out_lines,
                                     const std::atomic<bool>* cancel_requested) {
  if (arc.Archive == nullptr || cancel_requested_now(cancel_requested)) {
    return;
  }

  UInt32 num_props = 0;
  bool there_is_path_prop = false;
  if (arc.Archive->GetNumberOfProperties(&num_props) == S_OK) {
    for (UInt32 i = 0; i < num_props; ++i) {
      if (cancel_requested_now(cancel_requested)) {
        return;
      }
      CMyComBSTR name;
      PROPID prop_id = kpidNoProperty;
      VARTYPE var_type = VT_EMPTY;
      if (arc.Archive->GetPropertyInfo(i, &name, &prop_id, &var_type) != S_OK) {
        continue;
      }
      if (prop_id == kpidPath) {
        there_is_path_prop = true;
        break;
      }
    }
  }

  if (!there_is_path_prop) {
    const UString name_u = utf8_to_ustring(selected_item.name);
    NWindows::NCOM::CPropVariant name_prop(name_u.Ptr());
    append_property_variant_original(out_lines,
                                     ArchivePropertySection::kSelectedItem,
                                     ArchivePropertyDisplayGroup::kSelectedItemProperties,
                                     std::nullopt,
                                     kpidName,
                                     nullptr,
                                     name_prop);
  }

  if (arc.Archive->GetNumberOfProperties(&num_props) == S_OK) {
    for (UInt32 i = 0; i < num_props; ++i) {
      if (cancel_requested_now(cancel_requested)) {
        return;
      }
      CMyComBSTR name;
      PROPID prop_id = kpidNoProperty;
      VARTYPE var_type = VT_EMPTY;
      if (arc.Archive->GetPropertyInfo(i, &name, &prop_id, &var_type) != S_OK) {
        continue;
      }

      const PROPID mapped_prop_id = prop_id == kpidPath ? kpidName : prop_id;
      NWindows::NCOM::CPropVariant prop;
      if (!get_selected_item_property(arc, selected_item, flat_view, mapped_prop_id, prop)) {
        continue;
      }
      append_property_variant_original(out_lines,
                                       ArchivePropertySection::kSelectedItem,
                                       ArchivePropertyDisplayGroup::kSelectedItemProperties,
                                       std::nullopt,
                                       mapped_prop_id,
                                       name,
                                       prop);
    }
  }

  const std::array<PROPID, 3> kAdditionalProps = {
      kpidNumSubDirs,
      kpidNumSubFiles,
      kpidPrefix};
  for (const PROPID prop_id : kAdditionalProps) {
    if (cancel_requested_now(cancel_requested)) {
      return;
    }
    NWindows::NCOM::CPropVariant prop;
    if (!get_selected_item_property(arc, selected_item, flat_view, prop_id, prop)) {
      continue;
    }
    append_property_variant_original(out_lines,
                                     ArchivePropertySection::kSelectedItem,
                                     ArchivePropertyDisplayGroup::kSelectedItemProperties,
                                     std::nullopt,
                                     prop_id,
                                     nullptr,
                                     prop);
  }

  if (selected_item.has_arc_index) {
    append_all_raw_item_properties(arc, selected_item.arc_index, out_lines, cancel_requested);
  }
}

}  // namespace z7::app::info_properties_detail
