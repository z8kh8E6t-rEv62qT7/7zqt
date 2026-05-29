// src/archive_application/src/native_7z/info/info_properties_extract_summary.cpp
// Role: Multi-selection and folder-summary property emission.

#include "third_party_adapter/info_properties_extract_internal.h"

namespace z7::app::info_properties_detail {

void append_multi_selection_summary(
    const CArc& arc,
    const std::vector<SelectedPropertyItem>& selected_items,
    const bool flat_view,
    std::vector<ArchivePropertyLine>& out_lines,
    const std::atomic<bool>* cancel_requested) {
  if (selected_items.empty() || cancel_requested_now(cancel_requested)) {
    return;
  }

  uint64_t num_files = 0;
  uint64_t num_dirs = 0;
  uint64_t unpack_size = 0;
  uint64_t pack_size = 0;

  append_line(out_lines,
              PropertyLineKind::kPair,
              ArchivePropertySection::kSelectionSummary,
              ArchivePropertyDisplayGroup::kSelectionSummary,
              std::nullopt,
              std::nullopt,
              {},
              std::to_string(selected_items.size()));

  for (const SelectedPropertyItem& item : selected_items) {
    if (cancel_requested_now(cancel_requested)) {
      return;
    }

    if (item.is_dir) {
      num_dirs += 1 + static_cast<uint64_t>(item.num_sub_dirs);
      num_files += static_cast<uint64_t>(item.num_sub_files);
    } else {
      ++num_files;
    }

    unpack_size += get_selected_item_unpacked_size(arc, item, flat_view);
    pack_size += get_selected_item_pack_size(arc, item, flat_view);
  }

  if (num_dirs != 0) {
    NWindows::NCOM::CPropVariant prop = static_cast<UInt64>(num_dirs);
    append_property_variant_original(out_lines,
                                     ArchivePropertySection::kSelectionSummary,
                                     ArchivePropertyDisplayGroup::kSelectionSummary,
                                     std::nullopt,
                                     kpidNumSubDirs,
                                     nullptr,
                                     prop);
  }
  if (num_files != 0) {
    NWindows::NCOM::CPropVariant prop = static_cast<UInt64>(num_files);
    append_property_variant_original(out_lines,
                                     ArchivePropertySection::kSelectionSummary,
                                     ArchivePropertyDisplayGroup::kSelectionSummary,
                                     std::nullopt,
                                     kpidNumSubFiles,
                                     nullptr,
                                     prop);
  }

  {
    NWindows::NCOM::CPropVariant prop = static_cast<UInt64>(unpack_size);
    append_property_variant_original(out_lines,
                                     ArchivePropertySection::kSelectionSummary,
                                     ArchivePropertyDisplayGroup::kSelectionSummary,
                                     std::nullopt,
                                     kpidSize,
                                     nullptr,
                                     prop);
  }
  {
    NWindows::NCOM::CPropVariant prop = static_cast<UInt64>(pack_size);
    append_property_variant_original(out_lines,
                                     ArchivePropertySection::kSelectionSummary,
                                     ArchivePropertyDisplayGroup::kSelectionSummary,
                                     std::nullopt,
                                     kpidPackSize,
                                     nullptr,
                                     prop);
  }
}

void append_folder_properties(const FolderPropertyContext& folder_context,
                              std::vector<ArchivePropertyLine>& out_lines) {
  if (!folder_context.valid) {
    return;
  }

  if (!folder_context.path_prefix.empty()) {
    const UString path_u = utf8_to_ustring(folder_context.path_prefix);
    NWindows::NCOM::CPropVariant path_prop(path_u.Ptr());
    append_property_variant_original(out_lines,
                                     ArchivePropertySection::kCurrentFolder,
                                     ArchivePropertyDisplayGroup::kCurrentFolderPath,
                                     std::nullopt,
                                     kpidName,
                                     L"Path",
                                     path_prop);
  }

  {
    NWindows::NCOM::CPropVariant prop = static_cast<UInt64>(folder_context.size);
    append_property_variant_original(out_lines,
                                     ArchivePropertySection::kCurrentFolder,
                                     ArchivePropertyDisplayGroup::kCurrentFolderProperties,
                                     std::nullopt,
                                     kpidSize,
                                     nullptr,
                                     prop);
  }
  {
    NWindows::NCOM::CPropVariant prop = static_cast<UInt64>(folder_context.pack_size);
    append_property_variant_original(out_lines,
                                     ArchivePropertySection::kCurrentFolder,
                                     ArchivePropertyDisplayGroup::kCurrentFolderProperties,
                                     std::nullopt,
                                     kpidPackSize,
                                     nullptr,
                                     prop);
  }
  {
    NWindows::NCOM::CPropVariant prop =
        static_cast<UInt64>(folder_context.num_sub_dirs);
    append_property_variant_original(out_lines,
                                     ArchivePropertySection::kCurrentFolder,
                                     ArchivePropertyDisplayGroup::kCurrentFolderProperties,
                                     std::nullopt,
                                     kpidNumSubDirs,
                                     nullptr,
                                     prop);
  }
  {
    NWindows::NCOM::CPropVariant prop =
        static_cast<UInt64>(folder_context.num_sub_files);
    append_property_variant_original(out_lines,
                                     ArchivePropertySection::kCurrentFolder,
                                     ArchivePropertyDisplayGroup::kCurrentFolderProperties,
                                     std::nullopt,
                                     kpidNumSubFiles,
                                     nullptr,
                                     prop);
  }
  if (folder_context.crc_defined) {
    NWindows::NCOM::CPropVariant prop = folder_context.crc;
    append_property_variant_original(out_lines,
                                     ArchivePropertySection::kCurrentFolder,
                                     ArchivePropertyDisplayGroup::kCurrentFolderProperties,
                                     std::nullopt,
                                     kpidCRC,
                                     nullptr,
                                     prop);
  }
}

}  // namespace z7::app::info_properties_detail
